#include "asset_blob.h"

#include <cstdlib>
#include <cstring>

#include "esp_log.h"
#include "esp_ota_ops.h"

namespace asset_blob {

static constexpr const char* kTag = "asset";
static constexpr uint8_t kMagic[4] = {'A', 'S', 'T', 'S'};
static constexpr uint32_t kVersion = 1;
static constexpr size_t kHeaderFixed = 16;  // magic + version + count + total_size

Blob g_assets;

// Parse the IDF image header in the running partition to find the byte
// offset immediately after the last image segment (and SHA-256, if present).
static uint32_t parse_image_end(const esp_partition_t* part) {
  uint8_t hdr[24];
  if (esp_partition_read(part, 0, hdr, sizeof(hdr)) != ESP_OK)
    return 0;
  if (hdr[0] != 0xE9) {
    ESP_LOGE(kTag, "image magic mismatch: 0x%02x", hdr[0]);
    return 0;
  }
  uint8_t seg_count = hdr[1];
  uint8_t hash_appended = hdr[23];

  uint32_t off = 24;
  for (int i = 0; i < seg_count; i++) {
    uint8_t sh[8];
    if (esp_partition_read(part, off, sh, 8) != ESP_OK)
      return 0;
    uint32_t data_len = static_cast<uint32_t>(sh[4]) | (static_cast<uint32_t>(sh[5]) << 8) |
                        (static_cast<uint32_t>(sh[6]) << 16) | (static_cast<uint32_t>(sh[7]) << 24);
    off += 8 + data_len;
  }
  // Pad to next 16-byte boundary (covers checksum byte + any trailing pad).
  off = (off + 16) & ~15u;
  if (hash_appended)
    off += 32;
  return off;
}

bool Blob::init() {
  part_ = esp_ota_get_running_partition();
  if (!part_) {
    ESP_LOGE(kTag, "no running partition");
    return false;
  }

  uint32_t image_end = parse_image_end(part_);
  if (image_end == 0) {
    ESP_LOGE(kTag, "could not parse image header");
    return false;
  }
  // Manifest is 4-KB-aligned after image end (matches copy_firmware.py).
  manifest_offset_ = (image_end + 0xFFFu) & ~0xFFFu;

  uint8_t hdr[kHeaderFixed];
  if (esp_partition_read(part_, manifest_offset_, hdr, sizeof(hdr)) != ESP_OK) {
    ESP_LOGE(kTag, "manifest read failed at 0x%lx", static_cast<unsigned long>(manifest_offset_));
    return false;
  }
  if (memcmp(hdr, kMagic, 4) != 0) {
    ESP_LOGW(kTag, "no asset blob at 0x%lx (image_end=0x%lx)", static_cast<unsigned long>(manifest_offset_),
             static_cast<unsigned long>(image_end));
    return false;
  }
  uint32_t version = static_cast<uint32_t>(hdr[4]) | (static_cast<uint32_t>(hdr[5]) << 8) |
                     (static_cast<uint32_t>(hdr[6]) << 16) | (static_cast<uint32_t>(hdr[7]) << 24);
  count_ = static_cast<uint32_t>(hdr[8]) | (static_cast<uint32_t>(hdr[9]) << 8) |
           (static_cast<uint32_t>(hdr[10]) << 16) | (static_cast<uint32_t>(hdr[11]) << 24);
  if (version != kVersion || count_ == 0 || count_ > 64) {
    ESP_LOGE(kTag, "bad manifest: version=%lu count=%lu", static_cast<unsigned long>(version),
             static_cast<unsigned long>(count_));
    return false;
  }

  size_t bytes = count_ * sizeof(Entry);
  entries_ = static_cast<Entry*>(malloc(bytes));
  if (!entries_) {
    ESP_LOGE(kTag, "malloc(%u) failed", static_cast<unsigned>(bytes));
    return false;
  }
  if (esp_partition_read(part_, manifest_offset_ + kHeaderFixed, entries_, bytes) != ESP_OK) {
    free(entries_);
    entries_ = nullptr;
    return false;
  }

  ESP_LOGI(kTag, "blob @ part+0x%lx, %u entries", static_cast<unsigned long>(manifest_offset_),
           static_cast<unsigned>(count_));
  for (uint32_t i = 0; i < count_; i++) {
    // Ensure the name string is NUL-terminated for logging.
    char safe_name[33];
    memcpy(safe_name, entries_[i].name, 32);
    safe_name[32] = '\0';
    ESP_LOGI(kTag, "  %-24s off=0x%06lx len=%lu crc=0x%08lx", safe_name, static_cast<unsigned long>(entries_[i].offset),
             static_cast<unsigned long>(entries_[i].length), static_cast<unsigned long>(entries_[i].crc32));
  }
  return true;
}

bool Blob::find(const char* name, uint32_t& partition_offset, uint32_t& length, uint32_t& crc32) const {
  if (!entries_)
    return false;
  for (uint32_t i = 0; i < count_; i++) {
    if (strncmp(entries_[i].name, name, 32) == 0) {
      partition_offset = manifest_offset_ + entries_[i].offset;
      length = entries_[i].length;
      crc32 = entries_[i].crc32;
      return true;
    }
  }
  return false;
}

uint32_t Blob::crc(const char* name) const {
  uint32_t o, l, c;
  return find(name, o, l, c) ? c : 0u;
}

uint32_t Blob::size(const char* name) const {
  uint32_t o, l, c;
  return find(name, o, l, c) ? l : 0u;
}

const void* Blob::map(const char* name, size_t& size_out, esp_partition_mmap_handle_t& handle_out) {
  uint32_t off, len, crc;
  if (!find(name, off, len, crc))
    return nullptr;
  const void* ptr = nullptr;
  esp_err_t err = esp_partition_mmap(part_, off, len, ESP_PARTITION_MMAP_DATA, &ptr, &handle_out);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "mmap(%s, off=0x%lx, len=%lu) failed: %d", name, static_cast<unsigned long>(off),
             static_cast<unsigned long>(len), (int)err);
    return nullptr;
  }
  size_out = len;
  return ptr;
}

void Blob::unmap(esp_partition_mmap_handle_t handle) {
  if (handle)
    esp_partition_munmap(handle);
}

bool Blob::read(const char* name, uint32_t offset, void* dst, size_t len) const {
  uint32_t off, total, crc;
  if (!find(name, off, total, crc))
    return false;
  if (offset > total || offset + len > total)
    return false;
  return esp_partition_read(part_, off + offset, dst, len) == ESP_OK;
}

}  // namespace asset_blob
