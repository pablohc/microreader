#pragma once

// Asset blob: read-only access to binary resources appended after the IDF
// image inside the running app partition.  See tools/build_assets.py for the
// blob format.
//
// The blob lives entirely *outside* any image segment, so it consumes zero
// MMU pages at boot.  Consumers can:
//   - look up an asset by name (cheap, all metadata is cached in RAM)
//   - memory-map it on demand (for code that expects a flat pointer)
//   - stream it via esp_partition_read
//
// Call asset_blob::g_assets.init() once at startup before any consumer.

#include <cstddef>
#include <cstdint>

#include "esp_partition.h"

namespace asset_blob {

struct Entry {
  char name[32];
  uint32_t offset;  // offset within assets.bin (relative to manifest start)
  uint32_t length;
  uint32_t crc32;
};
static_assert(sizeof(Entry) == 44, "Entry layout must match build_assets.py");

class Blob {
 public:
  bool init();

  // Look up an asset by name.  Returns false if not found.
  // `partition_offset` is the absolute offset within the running partition
  // where the data starts (suitable for esp_partition_read / mmap).
  bool find(const char* name, uint32_t& partition_offset, uint32_t& length, uint32_t& crc32) const;

  // Convenience: total CRC of the named asset (0 if not found).
  uint32_t crc(const char* name) const;
  uint32_t size(const char* name) const;

  // Memory-map an asset on demand.  The returned pointer is valid until
  // `unmap(handle)` is called.  Returns nullptr on failure.
  const void* map(const char* name, size_t& size_out, esp_partition_mmap_handle_t& handle_out);
  void unmap(esp_partition_mmap_handle_t handle);

  // Stream-read raw bytes from an asset.
  bool read(const char* name, uint32_t offset, void* dst, size_t len) const;

  // Direct partition access (for callers that have already resolved an offset).
  const esp_partition_t* partition() const {
    return part_;
  }

 private:
  const esp_partition_t* part_ = nullptr;
  uint32_t manifest_offset_ = 0;
  Entry* entries_ = nullptr;
  uint32_t count_ = 0;
};

extern Blob g_assets;

}  // namespace asset_blob
