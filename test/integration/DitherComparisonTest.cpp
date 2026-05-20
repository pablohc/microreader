// DitherComparisonTest.cpp — Writes BMP comparison images for visual inspection.
// Decodes representative images from alice-illustrated and ohler EPUBs using
// all three dither modes and saves as 1-bit BMP files.
//
// Run with:
//   microreader_tests.exe --gtest_filter="DitherComparison.*"
// Output appears in:  test/output/dither/

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "TestBooks.h"
#include "gtest/gtest.h"
#include "microreader/content/ImageDecoder.h"
#include "microreader/content/ZipReader.h"

using namespace microreader;
namespace fs = std::filesystem;

#ifndef TEST_FIXTURES_DIR
#define TEST_FIXTURES_DIR "."
#endif

// ---------------------------------------------------------------------------
// 1-bit BMP writer (top-down via negative biHeight — supported by all modern
// Windows and browser viewers, avoids reversing row order).
// ---------------------------------------------------------------------------

static void write_1bit_bmp(const std::string& path, const DecodedImage& img) {
  if (img.data.empty() || img.width == 0 || img.height == 0)
    return;

  const int W = img.width;
  const int H = img.height;
  // BMP 1-bit rows must be padded to a multiple of 4 bytes.
  const int row_stride = ((W + 31) / 32) * 4;
  const int pixel_bytes = row_stride * H;
  // File: BITMAPFILEHEADER(14) + BITMAPINFOHEADER(40) + palette(8) + pixels
  const int file_size = 14 + 40 + 8 + pixel_bytes;

  std::vector<uint8_t> buf(static_cast<size_t>(file_size), 0);
  uint8_t* p = buf.data();

  auto w16 = [](uint8_t* d, uint16_t v) {
    d[0] = v & 0xFF;
    d[1] = (v >> 8) & 0xFF;
  };
  auto w32 = [](uint8_t* d, uint32_t v) {
    d[0] = v & 0xFF;
    d[1] = (v >> 8) & 0xFF;
    d[2] = (v >> 16) & 0xFF;
    d[3] = (v >> 24) & 0xFF;
  };
  auto i32 = [](uint8_t* d, int32_t v) {
    uint32_t u = static_cast<uint32_t>(v);
    d[0] = u & 0xFF;
    d[1] = (u >> 8) & 0xFF;
    d[2] = (u >> 16) & 0xFF;
    d[3] = (u >> 24) & 0xFF;
  };

  // BITMAPFILEHEADER
  p[0] = 'B';
  p[1] = 'M';
  w32(p + 2, static_cast<uint32_t>(file_size));
  // reserved bytes 6–9: already 0
  w32(p + 10, 14 + 40 + 8);  // offset to pixel data
  p += 14;

  // BITMAPINFOHEADER
  w32(p, 40);      // biSize
  i32(p + 4, W);   // biWidth
  i32(p + 8, -H);  // biHeight: negative = top-down raster
  w16(p + 12, 1);  // biPlanes = 1
  w16(p + 14, 1);  // biBitCount = 1
  // biCompression = 0 (BI_RGB), already 0
  w32(p + 20, static_cast<uint32_t>(pixel_bytes));  // biSizeImage
  // biXPelsPerMeter, biYPelsPerMeter, biClrUsed, biClrImportant: 0 is fine
  p += 40;

  // Color table — RGBQUAD[0] = black (ink), RGBQUAD[1] = white (paper)
  p[3] = 0;  // RGBQUAD[0].rgbReserved
  p[4] = 255;
  p[5] = 255;
  p[6] = 255;  // RGBQUAD[1] = white
  p[7] = 0;
  p += 8;

  // Pixel data: copy rows directly; our stride may be < row_stride when W is
  // not a multiple of 32, but the rest is already zero-initialised.
  const size_t src_stride = img.stride();
  for (int y = 0; y < H; ++y)
    std::memcpy(p + y * row_stride, img.data.data() + y * src_stride, src_stride);

  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool is_image_entry(const std::string& name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  const size_t n = lower.size();
  if (n >= 4 && lower.substr(n - 4) == ".jpg")
    return true;
  if (n >= 4 && lower.substr(n - 4) == ".png")
    return true;
  if (n >= 5 && lower.substr(n - 5) == ".jpeg")
    return true;
  return false;
}

static std::string output_dir() {
  std::string fixtures = TEST_FIXTURES_DIR;
  // fixtures = .../microreader2/test/fixtures → output = .../microreader2/test/output
  auto pos = fixtures.rfind('/');
  if (pos == std::string::npos)
    pos = fixtures.rfind('\\');
  return fixtures.substr(0, pos) + "/output/dither";
}

// ---------------------------------------------------------------------------
// Core comparison helper: decodes first max_images images from epub, saves BMP
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Scan all EPUBs in sd/books and generate dither comparison BMPs.
// Books without images are skipped silently.
TEST(DitherComparison, AllSdBooks) {
  std::string root = test_books::workspace_root();
  std::string books_dir = root + "/microreader2/sd/books";

  if (!fs::exists(books_dir)) {
    GTEST_SKIP() << "sd/books directory not found: " << books_dir;
  }

  // Collect all EPUBs, sorted for reproducible output
  std::vector<fs::path> epubs;
  for (auto& entry : fs::directory_iterator(books_dir)) {
    if (entry.is_regular_file()) {
      auto ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext == ".epub")
        epubs.push_back(entry.path());
    }
  }
  std::sort(epubs.begin(), epubs.end());

  ASSERT_FALSE(epubs.empty()) << "No EPUBs found in " << books_dir;

  int books_with_images = 0;
  for (auto& epub : epubs) {
    // Derive a clean prefix from the filename stem
    std::string stem = epub.stem().string();
    // Replace spaces with underscores
    std::replace(stem.begin(), stem.end(), ' ', '_');

    const std::string epub_path = epub.string();
    static constexpr int kMaxImages = 4;

    printf("\n[book] %s\n", stem.c_str());

    StdioZipFile zf;
    if (!zf.open(epub_path.c_str())) {
      printf("  SKIP: cannot open ZIP\n");
      continue;
    }

    ZipReader zip;
    if (zip.open(zf) != ZipError::Ok) {
      printf("  SKIP: cannot read ZIP directory\n");
      continue;
    }

    // Collect image entry indices
    std::vector<size_t> img_indices;
    for (size_t i = 0; i < zip.entry_count(); ++i) {
      if (is_image_entry(std::string(zip.entry(i).name)))
        img_indices.push_back(i);
      if (static_cast<int>(img_indices.size()) >= kMaxImages)
        break;
    }

    if (img_indices.empty()) {
      printf("  (no images)\n");
      continue;
    }

    ++books_with_images;
    const std::string out = output_dir();
    fs::create_directories(out);

    for (int idx = 0; idx < static_cast<int>(img_indices.size()); ++idx) {
      const ZipEntry& entry = zip.entry(img_indices[static_cast<size_t>(idx)]);
      printf("  img[%d]: %s\n", idx, std::string(entry.name).c_str());

      DecodedImage img;
      auto err = decode_image_from_entry(zf, entry, 480, 788, img, nullptr, 0,
                                         /*scale_to_fill=*/false);
      if (err != ImageError::Ok) {
        printf("    [FAIL] err=%d\n", static_cast<int>(err));
        continue;
      }

      char fname[512];
      std::snprintf(fname, sizeof(fname), "%s/%s_img%02d_%dx%d.bmp", out.c_str(), stem.c_str(), idx, img.width,
                    img.height);
      write_1bit_bmp(fname, img);
      printf("    [OK]   %dx%d\n", img.width, img.height);
    }
  }

  printf("\n  %d book(s) with images processed.\n", books_with_images);
}
