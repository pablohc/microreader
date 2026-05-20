#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include "microreader/content/ImageDecoder.h"
#include "microreader/content/ZipReader.h"
#include "microreader/display/DrawBuffer.h"

using namespace microreader;

// ---------------------------------------------------------------------------
// Helper: load a file from disk into a vector
// ---------------------------------------------------------------------------
static std::vector<uint8_t> load_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  EXPECT_TRUE(f.good()) << "Cannot open: " << path;
  f.seekg(0, std::ios::end);
  auto sz = f.tellg();
  f.seekg(0);
  std::vector<uint8_t> buf(static_cast<size_t>(sz));
  f.read(reinterpret_cast<char*>(buf.data()), sz);
  return buf;
}

// Helper: extract a file from an epub and return as vector
static std::vector<uint8_t> extract_from_epub(const std::string& epub_path, const std::string& inner_path) {
  StdioZipFile zf;
  EXPECT_TRUE(zf.open(epub_path.c_str())) << "Cannot open epub: " << epub_path;
  ZipReader zip;
  EXPECT_EQ(zip.open(zf), ZipError::Ok);
  auto* entry = zip.find(inner_path.c_str());
  EXPECT_NE(entry, nullptr) << "Cannot find in epub: " << inner_path;
  if (!entry)
    return {};
  std::vector<uint8_t> buf;
  EXPECT_EQ(zip.extract(zf, *entry, buf), ZipError::Ok);
  return buf;
}

static std::string fixtures_dir() {
  return TEST_FIXTURES_DIR;
}

// ===========================================================================
// Format detection tests
// ===========================================================================

TEST(ImageFormat, GuessFromFilename) {
  EXPECT_EQ(guess_format("image.jpg"), ImageFormat::Jpeg);
  EXPECT_EQ(guess_format("image.jpeg"), ImageFormat::Jpeg);
  EXPECT_EQ(guess_format("image.JPG"), ImageFormat::Jpeg);
  EXPECT_EQ(guess_format("image.JPEG"), ImageFormat::Jpeg);
  EXPECT_EQ(guess_format("image.png"), ImageFormat::Png);
  EXPECT_EQ(guess_format("image.PNG"), ImageFormat::Png);
  EXPECT_EQ(guess_format("image.gif"), ImageFormat::Unknown);
  EXPECT_EQ(guess_format("image.bmp"), ImageFormat::Unknown);
  EXPECT_EQ(guess_format("noext"), ImageFormat::Unknown);
  EXPECT_EQ(guess_format(nullptr), ImageFormat::Unknown);
  EXPECT_EQ(guess_format(""), ImageFormat::Unknown);
  EXPECT_EQ(guess_format("path/to/photo.jpg"), ImageFormat::Jpeg);
  EXPECT_EQ(guess_format("dir.png/file.jpg"), ImageFormat::Jpeg);
}

TEST(ImageFormat, GuessFromMagic_Jpeg) {
  uint8_t jpeg_magic[] = {0xFF, 0xD8, 0xFF, 0xE0};
  EXPECT_EQ(guess_format_from_magic(jpeg_magic, sizeof(jpeg_magic)), ImageFormat::Jpeg);
}

TEST(ImageFormat, GuessFromMagic_Png) {
  uint8_t png_magic[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  EXPECT_EQ(guess_format_from_magic(png_magic, sizeof(png_magic)), ImageFormat::Png);
}

TEST(ImageFormat, GuessFromMagic_Unknown) {
  uint8_t gif_magic[] = {0x47, 0x49, 0x46, 0x38};
  EXPECT_EQ(guess_format_from_magic(gif_magic, sizeof(gif_magic)), ImageFormat::Unknown);
}

TEST(ImageFormat, GuessFromMagic_TooShort) {
  uint8_t one_byte[] = {0xFF};
  EXPECT_EQ(guess_format_from_magic(one_byte, 1), ImageFormat::Unknown);
}

// ===========================================================================
// Image size reading tests (JPEG)
// ===========================================================================

TEST(ReadImageSize, Jpeg1x1FromFixture) {
  auto data = extract_from_epub(fixtures_dir() + "/with_images.epub", "OEBPS/images/test.jpg");
  ASSERT_FALSE(data.empty());
  uint16_t w = 0, h = 0;
  EXPECT_EQ(read_image_size(data.data(), data.size(), w, h), ImageError::Ok);
  EXPECT_EQ(w, 1);
  EXPECT_EQ(h, 1);
}

TEST(ReadImageSize, Png1x1FromFixture) {
  auto data = extract_from_epub(fixtures_dir() + "/with_images.epub", "OEBPS/images/test.png");
  ASSERT_FALSE(data.empty());
  uint16_t w = 0, h = 0;
  EXPECT_EQ(read_image_size(data.data(), data.size(), w, h), ImageError::Ok);
  EXPECT_EQ(w, 1);
  EXPECT_EQ(h, 1);
}

TEST(ReadImageSize, InvalidMagic) {
  uint8_t garbage[] = {0x00, 0x00, 0x00, 0x00};
  uint16_t w, h;
  EXPECT_EQ(read_image_size(garbage, sizeof(garbage), w, h), ImageError::UnsupportedFormat);
}

TEST(ReadImageSize, JpegTruncated) {
  // Just the SOI marker, no SOF
  uint8_t data[] = {0xFF, 0xD8};
  uint16_t w, h;
  EXPECT_EQ(read_image_size(data, sizeof(data), w, h), ImageError::InvalidData);
}

TEST(ReadImageSize, PngTruncated) {
  // Just the PNG signature, no IHDR
  uint8_t data[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00};
  uint16_t w, h;
  EXPECT_EQ(read_image_size(data, sizeof(data), w, h), ImageError::InvalidData);
}

// ===========================================================================
// Manually constructed JPEG header for size reading
// ===========================================================================

TEST(ReadImageSize, JpegSof0_320x240) {
  // Construct minimal JPEG: SOI + APP0 (minimal) + SOF0 with 320x240
  std::vector<uint8_t> data;
  // SOI
  data.push_back(0xFF);
  data.push_back(0xD8);
  // APP0 marker (minimal JFIF header)
  data.push_back(0xFF);
  data.push_back(0xE0);
  data.push_back(0x00);
  data.push_back(0x10);  // length 16
  // "JFIF\0" + version + padding to fill 16-2=14 bytes of payload
  const char* jfif = "JFIF";
  for (int i = 0; i < 5; ++i)
    data.push_back(jfif[i]);
  data.push_back(0x01);
  data.push_back(0x01);  // version 1.1
  for (int i = 0; i < 7; ++i)
    data.push_back(0x00);  // padding

  // SOF0 marker: height=240, width=320, 1 component
  data.push_back(0xFF);
  data.push_back(0xC0);
  data.push_back(0x00);
  data.push_back(0x0B);  // length 11
  data.push_back(0x08);  // precision 8
  data.push_back(0x00);
  data.push_back(0xF0);  // height = 240
  data.push_back(0x01);
  data.push_back(0x40);  // width = 320
  data.push_back(0x01);  // 1 component
  data.push_back(0x01);
  data.push_back(0x11);
  data.push_back(0x00);

  uint16_t w = 0, h = 0;
  EXPECT_EQ(read_image_size(data.data(), data.size(), w, h), ImageError::Ok);
  EXPECT_EQ(w, 320);
  EXPECT_EQ(h, 240);
}

// ===========================================================================
// Manually constructed PNG for size reading
// ===========================================================================

TEST(ReadImageSize, PngIhdr_800x600) {
  // Minimal PNG: signature + IHDR with 800x600
  std::vector<uint8_t> data;
  // PNG signature
  uint8_t sig[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  data.insert(data.end(), sig, sig + 8);
  // IHDR chunk: length=13
  data.push_back(0x00);
  data.push_back(0x00);
  data.push_back(0x00);
  data.push_back(0x0D);
  // "IHDR"
  data.push_back('I');
  data.push_back('H');
  data.push_back('D');
  data.push_back('R');
  // width = 800
  data.push_back(0x00);
  data.push_back(0x00);
  data.push_back(0x03);
  data.push_back(0x20);
  // height = 600
  data.push_back(0x00);
  data.push_back(0x00);
  data.push_back(0x02);
  data.push_back(0x58);
  // bit depth=8, color_type=2 (RGB), compression=0, filter=0, interlace=0
  data.push_back(0x08);
  data.push_back(0x02);
  data.push_back(0x00);
  data.push_back(0x00);
  data.push_back(0x00);
  // CRC (placeholder, read_image_size doesn't check it)
  data.push_back(0x00);
  data.push_back(0x00);
  data.push_back(0x00);
  data.push_back(0x00);

  uint16_t w = 0, h = 0;
  EXPECT_EQ(read_image_size(data.data(), data.size(), w, h), ImageError::Ok);
  EXPECT_EQ(w, 800);
  EXPECT_EQ(h, 600);
}

// ===========================================================================
// Scaled size tests
// ===========================================================================

TEST(ScaledSize, NoScalingNeeded) {
  uint16_t w, h;
  scaled_size(100, 50, 200, 200, w, h);
  EXPECT_EQ(w, 100);
  EXPECT_EQ(h, 50);
}

TEST(ScaledSize, ExactFit) {
  uint16_t w, h;
  scaled_size(200, 200, 200, 200, w, h);
  EXPECT_EQ(w, 200);
  EXPECT_EQ(h, 200);
}

TEST(ScaledSize, ScaleDown_WidthBound) {
  uint16_t w, h;
  scaled_size(400, 200, 200, 200, w, h);
  EXPECT_EQ(w, 200);
  EXPECT_EQ(h, 100);
}

TEST(ScaledSize, ScaleDown_HeightBound) {
  uint16_t w, h;
  scaled_size(200, 400, 200, 200, w, h);
  EXPECT_EQ(w, 100);
  EXPECT_EQ(h, 200);
}

TEST(ScaledSize, ScaleDown_BothLarger) {
  uint16_t w, h;
  scaled_size(1000, 500, 200, 200, w, h);
  EXPECT_EQ(w, 200);
  EXPECT_EQ(h, 100);
}

TEST(ScaledSize, AspectRatioPreserved_3to2) {
  uint16_t w, h;
  scaled_size(600, 400, 300, 300, w, h);
  EXPECT_EQ(w, 300);
  EXPECT_EQ(h, 200);
}

TEST(ScaledSize, AspectRatioPreserved_Square) {
  uint16_t w, h;
  scaled_size(500, 500, 100, 100, w, h);
  EXPECT_EQ(w, 100);
  EXPECT_EQ(h, 100);
}

// ===========================================================================
// Floyd-Steinberg dithering tests
// ===========================================================================

TEST(Dither, AllWhite) {
  // 8x1 pixels, all 255 (white)
  uint8_t gray[8] = {255, 255, 255, 255, 255, 255, 255, 255};
  uint8_t out[1] = {0};
  floyd_steinberg_dither(gray, 8, 1, out);
  EXPECT_EQ(out[0], 0xFF);  // all bits set = all white
}

TEST(Dither, AllBlack) {
  uint8_t gray[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint8_t out[1] = {0xFF};
  floyd_steinberg_dither(gray, 8, 1, out);
  EXPECT_EQ(out[0], 0x00);  // all bits clear = all black
}

TEST(Dither, AllMidgray_EvenDistribution) {
  // 8x8 pixels, all 128. Should produce roughly 50% black, 50% white.
  const int w = 8, h = 8;
  std::vector<uint8_t> gray(w * h, 128);
  size_t stride = (w + 7) / 8;
  std::vector<uint8_t> out(stride * h, 0);
  floyd_steinberg_dither(gray.data(), w, h, out.data());

  int white_count = 0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (out[y * stride + x / 8] & (0x80 >> (x % 8)))
        ++white_count;
    }
  }
  // Should be roughly 50% white (32 out of 64)
  EXPECT_GE(white_count, 20);
  EXPECT_LE(white_count, 44);
}

TEST(Dither, GradientHasIncreasingWhitePixels) {
  // 8-pixel wide, 1 row: gradient from 0 to 255
  const int w = 8, h = 1;
  uint8_t gray[8];
  for (int i = 0; i < 8; ++i)
    gray[i] = static_cast<uint8_t>(i * 255 / 7);
  uint8_t out[1] = {0};
  floyd_steinberg_dither(gray, w, h, out);
  // First pixel should be black (value 0), last should be white (value 255)
  EXPECT_FALSE(out[0] & 0x80);  // pixel 0 = black
  EXPECT_TRUE(out[0] & 0x01);   // pixel 7 = white
}

TEST(Dither, SinglePixelBlack) {
  uint8_t gray[1] = {0};
  uint8_t out[1] = {0xFF};
  floyd_steinberg_dither(gray, 1, 1, out);
  EXPECT_EQ(out[0] & 0x80, 0);  // first bit = black
}

TEST(Dither, SinglePixelWhite) {
  uint8_t gray[1] = {255};
  uint8_t out[1] = {0};
  floyd_steinberg_dither(gray, 1, 1, out);
  EXPECT_EQ(out[0] & 0x80, 0x80);  // first bit = white
}

TEST(Dither, OutputStride) {
  // Test that stride is calculated correctly for non-multiple-of-8 widths
  const int w = 10, h = 2;
  std::vector<uint8_t> gray(w * h, 255);
  size_t stride = (w + 7) / 8;  // = 2 bytes per row
  EXPECT_EQ(stride, 2u);
  std::vector<uint8_t> out(stride * h, 0);
  floyd_steinberg_dither(gray.data(), w, h, out.data());
  // All white: first 10 bits should be set in each row, last 6 bits undefined
  EXPECT_EQ(out[0], 0xFF);         // first 8 bits of row 0
  EXPECT_EQ(out[1] & 0xC0, 0xC0);  // bits 8-9 of row 0
  EXPECT_EQ(out[2], 0xFF);         // first 8 bits of row 1
  EXPECT_EQ(out[3] & 0xC0, 0xC0);  // bits 8-9 of row 1
}

// ===========================================================================
// DecodedImage struct tests
// ===========================================================================

TEST(DecodedImage, StrideCalculation) {
  DecodedImage img;
  img.width = 8;
  img.height = 1;
  EXPECT_EQ(img.stride(), 1u);
  EXPECT_EQ(img.data_size(), 1u);

  img.width = 9;
  EXPECT_EQ(img.stride(), 2u);
  EXPECT_EQ(img.data_size(), 2u);

  img.width = 16;
  EXPECT_EQ(img.stride(), 2u);
  img.height = 10;
  EXPECT_EQ(img.data_size(), 20u);
}

TEST(DecodedImage, PixelAccess) {
  DecodedImage img;
  img.width = 8;
  img.height = 1;
  img.data = {0b10110010};  // pixels: 1,0,1,1,0,0,1,0
  EXPECT_TRUE(img.pixel(0, 0));
  EXPECT_FALSE(img.pixel(1, 0));
  EXPECT_TRUE(img.pixel(2, 0));
  EXPECT_TRUE(img.pixel(3, 0));
  EXPECT_FALSE(img.pixel(4, 0));
  EXPECT_FALSE(img.pixel(5, 0));
  EXPECT_TRUE(img.pixel(6, 0));
  EXPECT_FALSE(img.pixel(7, 0));
}

// ===========================================================================
// Full decode tests (using fixture images)
// ===========================================================================

TEST(DecodeImage, Jpeg1x1FromEpub) {
  auto data = extract_from_epub(fixtures_dir() + "/with_images.epub", "OEBPS/images/test.jpg");
  ASSERT_FALSE(data.empty());

  DecodedImage img;
  auto err = decode_image(data.data(), data.size(), 1024, 768, img);
  EXPECT_EQ(err, ImageError::Ok);
  EXPECT_EQ(img.width, 1);
  EXPECT_EQ(img.height, 1);
  EXPECT_EQ(img.data.size(), 1u);
  // A 1x1 white JPEG should decode to a white pixel
  EXPECT_TRUE(img.pixel(0, 0));
}

TEST(DecodeImage, Png1x1FromEpub) {
  auto data = extract_from_epub(fixtures_dir() + "/with_images.epub", "OEBPS/images/test.png");
  ASSERT_FALSE(data.empty());

  DecodedImage img;
  auto err = decode_image(data.data(), data.size(), 1024, 768, img);
  EXPECT_EQ(err, ImageError::Ok);
  EXPECT_EQ(img.width, 1);
  EXPECT_EQ(img.height, 1);
  EXPECT_EQ(img.data.size(), 1u);
  // A 1x1 red PNG (RGB 255,0,0): when converted to grayscale by stb_image
  // using luminance formula (0.299R + 0.587G + 0.114B), result is ~76,
  // which is below 128, so should be black.
  EXPECT_FALSE(img.pixel(0, 0));
}

TEST(DecodeImage, InvalidData) {
  uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03};
  DecodedImage img;
  auto err = decode_image(garbage, sizeof(garbage), 1024, 768, img);
  EXPECT_EQ(err, ImageError::UnsupportedFormat);
}

// ===========================================================================
// Full decode with synthetic images (generated in-memory)
// ===========================================================================

// Minimal valid PNG for testing: generated at compile time would be complex,
// so we use the fixture-based tests above. Here we test with larger
// programmatic grayscale images by going through the dither path directly.

TEST(DecodeImage, DitherIntegration_Checkerboard) {
  // Create a 4x4 checkerboard grayscale pattern
  const int w = 4, h = 4;
  uint8_t gray[16];
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      gray[y * w + x] = ((x + y) % 2 == 0) ? 255 : 0;
    }
  }

  size_t stride = (w + 7) / 8;  // = 1 byte
  std::vector<uint8_t> out(stride * h, 0);
  floyd_steinberg_dither(gray, w, h, out.data());

  // First row: W B W B -> bit pattern 1010xxxx = 0xA0 (with error diffusion)
  // Due to error diffusion, exact values may shift slightly but the pattern
  // should be recognizable. The first pixel (255) should be white.
  EXPECT_TRUE(out[0] & 0x80);  // pixel (0,0) = white (input 255)
}

// ===========================================================================
// Test with generated test PNG (from fixture generator)
// ===========================================================================

// These tests use the larger test images generated by the fixture script
class ImageFixtureTest : public ::testing::Test {
 protected:
  void SetUp() override {
    epub_path_ = fixtures_dir() + "/with_images.epub";
  }
  std::string epub_path_;
};

TEST_F(ImageFixtureTest, JpegMagicDetection) {
  auto data = extract_from_epub(epub_path_, "OEBPS/images/test.jpg");
  ASSERT_GE(data.size(), 2u);
  EXPECT_EQ(data[0], 0xFF);
  EXPECT_EQ(data[1], 0xD8);
  EXPECT_EQ(guess_format_from_magic(data.data(), data.size()), ImageFormat::Jpeg);
}

TEST_F(ImageFixtureTest, PngMagicDetection) {
  auto data = extract_from_epub(epub_path_, "OEBPS/images/test.png");
  ASSERT_GE(data.size(), 4u);
  EXPECT_EQ(data[0], 0x89);
  EXPECT_EQ(data[1], 'P');
  EXPECT_EQ(data[2], 'N');
  EXPECT_EQ(data[3], 'G');
  EXPECT_EQ(guess_format_from_magic(data.data(), data.size()), ImageFormat::Png);
}

TEST_F(ImageFixtureTest, RoundTrip_ExtractAndDecode) {
  // Full round-trip: open epub -> find image -> extract -> decode
  StdioZipFile zf;
  ASSERT_TRUE(zf.open(epub_path_.c_str()));
  ZipReader zip;
  ASSERT_EQ(zip.open(zf), ZipError::Ok);

  auto* jpg_entry = zip.find("OEBPS/images/test.jpg");
  ASSERT_NE(jpg_entry, nullptr);
  std::vector<uint8_t> jpg;
  ASSERT_EQ(zip.extract(zf, *jpg_entry, jpg), ZipError::Ok);

  DecodedImage img;
  EXPECT_EQ(decode_image(jpg.data(), jpg.size(), 200, 200, img), ImageError::Ok);
  EXPECT_GT(img.width, 0);
  EXPECT_GT(img.height, 0);
  EXPECT_FALSE(img.data.empty());
}

// ===========================================================================
// Progressive JPEG decode tests (non-interleaved scan)
// ===========================================================================

#include "TestBooks.h"

// The Bobiverse German cover is a progressive JPEG (SOF2) with 4:2:0
// subsampling (Y h_samp=2, v_samp=2).  The first scan is non-interleaved
// (single Y component).  This previously caused a duplication bug because
// the decoder used interleaved MCU structure for non-interleaved scans.
TEST(DecodeImage, ProgressiveJpeg_BobiverseGerman_NoDuplication) {
  std::string root = test_books::workspace_root();
  std::string epub = root +
                     "/microreader2/test/books/other/Bobiverse 1 Ich "
                     "bin viele (Dennis E. Taylor) (Z-Library).epub";
  if (!std::filesystem::exists(epub)) {
    GTEST_SKIP() << "Bobiverse German epub not found";
  }

  // Extract the cover JPEG
  StdioZipFile zf;
  ASSERT_TRUE(zf.open(epub.c_str()));
  ZipReader zip;
  ASSERT_EQ(zip.open(zf), ZipError::Ok);
  auto* entry = zip.find("OEBPS/cover.jpg");
  ASSERT_NE(entry, nullptr) << "cover.jpg not found in epub";

  // Decode via streaming path (same as device uses)
  DecodedImage img;
  auto err = decode_jpeg_from_entry(zf, *entry, 479, 732, img);
  ASSERT_EQ(err, ImageError::Ok) << "Failed to decode progressive JPEG";

  EXPECT_GT(img.width, 0);
  EXPECT_GT(img.height, 0);
  printf("  Decoded: %dx%d, stride=%zu, data=%zu bytes\n", img.width, img.height, img.stride(), img.data.size());

  // Verify no horizontal duplication: the left quarter and right quarter
  // of the image should NOT be identical.  With the old bug, the image
  // was rendered as two squished copies side by side.
  int quarter_w = img.width / 4;
  int check_h = std::min(static_cast<int>(img.height), 200);
  int matching_rows = 0;
  for (int y = 0; y < check_h; ++y) {
    bool row_matches = true;
    for (int x = 0; x < quarter_w; ++x) {
      int right_x = img.width / 2 + x;
      if (right_x >= img.width)
        break;
      if (img.pixel(x, y) != img.pixel(right_x, y)) {
        row_matches = false;
        break;
      }
    }
    if (row_matches)
      ++matching_rows;
  }
  // Allow some rows to match (solid color regions), but not most.
  // With the duplication bug, nearly ALL rows match.
  float match_ratio = static_cast<float>(matching_rows) / check_h;
  printf("  Left/right half match ratio: %.1f%% (%d/%d rows)\n", match_ratio * 100, matching_rows, check_h);
  EXPECT_LT(match_ratio, 0.5f) << "Image appears duplicated horizontally (left/right halves too similar)";
}

// ===========================================================================
// scale_to_fill upscaling tests
// ---------------------------------------------------------------------------
// When scale_to_fill=true the decoders must emit enough output rows to reach
// the requested height — the previous bug only ever emitted one row per source
// row, which truncated upscaled images to a single row.
// ===========================================================================

TEST_F(ImageFixtureTest, ScaleToFill_False_Jpeg_NaturalSize) {
  // With scale_to_fill=false the image should be returned at natural size
  // (i.e. not enlarged to fill the target slot).
  StdioZipFile zf;
  ASSERT_TRUE(zf.open(epub_path_.c_str()));
  ZipReader zip;
  ASSERT_EQ(zip.open(zf), ZipError::Ok);
  auto* entry = zip.find("OEBPS/images/test.jpg");
  ASSERT_NE(entry, nullptr);

  DecodedImage natural, large_target;
  ASSERT_EQ(decode_jpeg_from_entry(zf, *entry, DrawBuffer::kWidth, DrawBuffer::kHeight, natural, nullptr, 0, false),
            ImageError::Ok);
  ASSERT_EQ(
      decode_jpeg_from_entry(zf, *entry, DrawBuffer::kWidth, DrawBuffer::kHeight, large_target, nullptr, 0, false),
      ImageError::Ok);

  // Both calls with scale_to_fill=false should yield the same (natural) dims.
  EXPECT_EQ(large_target.width, natural.width);
  EXPECT_EQ(large_target.height, natural.height);
  EXPECT_EQ(large_target.data.size(), static_cast<size_t>(large_target.height) * large_target.stride());
}

TEST_F(ImageFixtureTest, ScaleToFill_True_Jpeg_FillsTarget) {
  // With scale_to_fill=true the decoder must upscale the image so that at
  // least one output dimension reaches the requested target.
  StdioZipFile zf;
  ASSERT_TRUE(zf.open(epub_path_.c_str()));
  ZipReader zip;
  ASSERT_EQ(zip.open(zf), ZipError::Ok);
  auto* entry = zip.find("OEBPS/images/test.jpg");
  ASSERT_NE(entry, nullptr);

  // First determine natural size so we can pick a larger target.
  DecodedImage natural;
  ASSERT_EQ(decode_jpeg_from_entry(zf, *entry, DrawBuffer::kWidth, DrawBuffer::kHeight, natural, nullptr, 0, false),
            ImageError::Ok);
  ASSERT_GT(natural.width, 0);
  ASSERT_GT(natural.height, 0);

  const uint16_t target_w = static_cast<uint16_t>(natural.width + 10);
  const uint16_t target_h = static_cast<uint16_t>(natural.height + 10);

  DecodedImage filled;
  ASSERT_EQ(decode_jpeg_from_entry(zf, *entry, target_w, target_h, filled, nullptr, 0, /*scale_to_fill=*/true),
            ImageError::Ok);

  ASSERT_FALSE(filled.data.empty());

  // Output must be larger than natural size.
  EXPECT_GT(filled.width, natural.width);
  EXPECT_GT(filled.height, natural.height);

  // At least one dimension should reach the target (aspect-preserving fill).
  EXPECT_TRUE(filled.width == target_w || filled.height == target_h)
      << "expected one dim to reach target; got " << filled.width << "x" << filled.height << " (target " << target_w
      << "x" << target_h << ")";

  // Data size must be consistent: no truncated rows.
  EXPECT_EQ(filled.data.size(), static_cast<size_t>(filled.height) * filled.stride());
}

TEST_F(ImageFixtureTest, ScaleToFill_True_Png_FillsTarget) {
  // Same as the JPEG test but for the PNG decoder.
  StdioZipFile zf;
  ASSERT_TRUE(zf.open(epub_path_.c_str()));
  ZipReader zip;
  ASSERT_EQ(zip.open(zf), ZipError::Ok);
  auto* entry = zip.find("OEBPS/images/test.png");
  ASSERT_NE(entry, nullptr);

  DecodedImage natural;
  ASSERT_EQ(decode_png_from_entry(zf, *entry, DrawBuffer::kWidth, DrawBuffer::kHeight, natural, nullptr, 0, false),
            ImageError::Ok);
  ASSERT_GT(natural.width, 0);
  ASSERT_GT(natural.height, 0);

  const uint16_t target_w = static_cast<uint16_t>(natural.width + 10);
  const uint16_t target_h = static_cast<uint16_t>(natural.height + 10);

  DecodedImage filled;
  ASSERT_EQ(decode_png_from_entry(zf, *entry, target_w, target_h, filled, nullptr, 0, /*scale_to_fill=*/true),
            ImageError::Ok);

  ASSERT_FALSE(filled.data.empty());

  EXPECT_GT(filled.width, natural.width);
  EXPECT_GT(filled.height, natural.height);

  EXPECT_TRUE(filled.width == target_w || filled.height == target_h)
      << "expected one dim to reach target; got " << filled.width << "x" << filled.height << " (target " << target_w
      << "x" << target_h << ")";

  EXPECT_EQ(filled.data.size(), static_cast<size_t>(filled.height) * filled.stride());
}

// ===========================================================================
// ImageRowSink tests — verify the streaming sink path produces correct output
// ===========================================================================

// Helper: collect rows emitted by ImageRowSink into a DecodedImage.
struct SinkCollector {
  DecodedImage img;

  static void on_row(void* ctx, uint16_t y, const uint8_t* data, uint16_t width) {
    auto* self = static_cast<SinkCollector*>(ctx);
    if (self->img.width == 0) {
      self->img.width = width;
    }
    size_t stride = (width + 7) / 8;
    size_t needed = static_cast<size_t>(y + 1) * stride;
    if (self->img.data.size() < needed)
      self->img.data.resize(needed, 0);
    std::memcpy(self->img.data.data() + static_cast<size_t>(y) * stride, data, stride);
    if (y + 1 > self->img.height)
      self->img.height = y + 1;
  }

  ImageRowSink sink() {
    return ImageRowSink{&SinkCollector::on_row, this};
  }
};

// Decode via read_local_entry (same path as ReaderScreen): this failed before
// the fix because the local entry has no filename, causing format detection to
// fail for deflated entries.
TEST_F(ImageFixtureTest, DecodeFromLocalEntry_Jpeg_WithSink) {
  StdioZipFile zf;
  ASSERT_TRUE(zf.open(epub_path_.c_str()));
  ZipReader zip;
  ASSERT_EQ(zip.open(zf), ZipError::Ok);

  auto* cd_entry = zip.find("OEBPS/images/test.jpg");
  ASSERT_NE(cd_entry, nullptr);
  uint32_t offset = cd_entry->local_header_offset;

  ZipEntry local_entry;
  ASSERT_EQ(ZipReader::read_local_entry(zf, offset, local_entry), ZipError::Ok);
  EXPECT_TRUE(local_entry.name.empty()) << "Local entry should have no filename";

  SinkCollector collector;
  auto s = collector.sink();
  DecodedImage dims;
  auto err =
      decode_image_from_entry(zf, local_entry, DrawBuffer::kWidth, DrawBuffer::kHeight, dims, nullptr, 0, false, &s);
  ASSERT_EQ(err, ImageError::Ok) << "decode_image_from_entry failed for local entry with empty name — "
                                    "this is the black squares bug";
  EXPECT_GT(dims.width, 0);
  EXPECT_GT(dims.height, 0);
}

TEST_F(ImageFixtureTest, DecodeFromLocalEntry_Png_WithSink) {
  StdioZipFile zf;
  ASSERT_TRUE(zf.open(epub_path_.c_str()));
  ZipReader zip;
  ASSERT_EQ(zip.open(zf), ZipError::Ok);

  auto* cd_entry = zip.find("OEBPS/images/test.png");
  ASSERT_NE(cd_entry, nullptr);
  uint32_t offset = cd_entry->local_header_offset;

  ZipEntry local_entry;
  ASSERT_EQ(ZipReader::read_local_entry(zf, offset, local_entry), ZipError::Ok);

  SinkCollector collector;
  auto s = collector.sink();
  DecodedImage dims;
  auto err =
      decode_image_from_entry(zf, local_entry, DrawBuffer::kWidth, DrawBuffer::kHeight, dims, nullptr, 0, false, &s);
  ASSERT_EQ(err, ImageError::Ok) << "decode_image_from_entry failed for PNG local entry with empty name";
  EXPECT_GT(dims.width, 0);
  EXPECT_GT(dims.height, 0);
}

// Verify that the sink path produces identical output to the buffer path.
TEST_F(ImageFixtureTest, SinkOutput_MatchesBufferOutput_Jpeg) {
  StdioZipFile zf;
  ASSERT_TRUE(zf.open(epub_path_.c_str()));
  ZipReader zip;
  ASSERT_EQ(zip.open(zf), ZipError::Ok);

  auto* entry = zip.find("OEBPS/images/test.jpg");
  ASSERT_NE(entry, nullptr);

  DecodedImage buf_img;
  ASSERT_EQ(decode_jpeg_from_entry(zf, *entry, DrawBuffer::kWidth, DrawBuffer::kHeight, buf_img), ImageError::Ok);
  ASSERT_GT(buf_img.width, 0);
  ASSERT_FALSE(buf_img.data.empty());

  SinkCollector collector;
  auto s = collector.sink();
  DecodedImage sink_dims;
  ASSERT_EQ(
      decode_jpeg_from_entry(zf, *entry, DrawBuffer::kWidth, DrawBuffer::kHeight, sink_dims, nullptr, 0, false, &s),
      ImageError::Ok);

  EXPECT_EQ(sink_dims.width, buf_img.width);
  EXPECT_EQ(sink_dims.height, buf_img.height);
  EXPECT_EQ(collector.img.width, buf_img.width);
  EXPECT_EQ(collector.img.height, buf_img.height);

  size_t stride = buf_img.stride();
  ASSERT_EQ(collector.img.data.size(), buf_img.data.size());
  for (uint16_t y = 0; y < buf_img.height; ++y) {
    EXPECT_EQ(std::memcmp(collector.img.data.data() + y * stride, buf_img.data.data() + y * stride, stride), 0)
        << "Row " << y << " differs between sink and buffer paths";
  }
}

TEST_F(ImageFixtureTest, SinkOutput_MatchesBufferOutput_Png) {
  StdioZipFile zf;
  ASSERT_TRUE(zf.open(epub_path_.c_str()));
  ZipReader zip;
  ASSERT_EQ(zip.open(zf), ZipError::Ok);

  auto* entry = zip.find("OEBPS/images/test.png");
  ASSERT_NE(entry, nullptr);

  DecodedImage buf_img;
  ASSERT_EQ(decode_png_from_entry(zf, *entry, DrawBuffer::kWidth, DrawBuffer::kHeight, buf_img), ImageError::Ok);
  ASSERT_GT(buf_img.width, 0);
  ASSERT_FALSE(buf_img.data.empty());

  SinkCollector collector;
  auto s = collector.sink();
  DecodedImage sink_dims;
  ASSERT_EQ(
      decode_png_from_entry(zf, *entry, DrawBuffer::kWidth, DrawBuffer::kHeight, sink_dims, nullptr, 0, false, &s),
      ImageError::Ok);

  EXPECT_EQ(sink_dims.width, buf_img.width);
  EXPECT_EQ(sink_dims.height, buf_img.height);

  size_t stride = buf_img.stride();
  ASSERT_EQ(collector.img.data.size(), buf_img.data.size());
  for (uint16_t y = 0; y < buf_img.height; ++y) {
    EXPECT_EQ(std::memcmp(collector.img.data.data() + y * stride, buf_img.data.data() + y * stride, stride), 0)
        << "Row " << y << " differs between sink and buffer paths";
  }
}

// ===========================================================================
// Deflated image decode via read_local_entry — the "black squares" bug
// ===========================================================================
// The bobiverse EPUB has OEBPS/cover.jpg stored with deflate compression.
// When decoded via read_local_entry (empty filename), format detection via
// a small peek buffer failed for deflated entries, causing ReadError.

TEST(DecodeImage, DeflatedJpeg_LocalEntry_NoFilename) {
  // Use the bobiverse book which has a deflated cover.jpg.
  std::string root = test_books::workspace_root();
  std::string epub = root + "/microreader2/test/books/other/bobiverse one.epub";
  if (!std::filesystem::exists(epub))
    GTEST_SKIP() << "bobiverse one.epub not found";

  // Find cover.jpg via central directory to get its offset.
  StdioZipFile zf;
  ASSERT_TRUE(zf.open(epub.c_str()));
  ZipReader zip;
  ASSERT_EQ(zip.open(zf), ZipError::Ok);
  auto* cd_entry = zip.find("OEBPS/cover.jpg");
  ASSERT_NE(cd_entry, nullptr);
  // Verify it's actually deflated (compression method 8).
  ASSERT_EQ(cd_entry->compression, 8u) << "Expected deflated entry for this test";
  uint32_t offset = cd_entry->local_header_offset;

  // Simulate ReaderScreen: read_local_entry (no filename), then decode.
  ZipEntry local_entry;
  ASSERT_EQ(ZipReader::read_local_entry(zf, offset, local_entry), ZipError::Ok);
  EXPECT_TRUE(local_entry.name.empty());
  EXPECT_EQ(local_entry.compression, 8u) << "Local entry should inherit deflate compression";

  // Decode with sink (same as ReaderScreen pipeline but scale_to_fill=false
  // since we're passing screen bounds, not pre-computed target dimensions).
  SinkCollector collector;
  auto s = collector.sink();
  DecodedImage dims;
  auto err =
      decode_image_from_entry(zf, local_entry, DrawBuffer::kWidth, DrawBuffer::kHeight, dims, nullptr, 0, false, &s);
  ASSERT_EQ(err, ImageError::Ok) << "Deflated JPEG with empty filename should decode via magic peek — "
                                    "this was the black squares bug (err="
                                 << (int)err << ")";

  EXPECT_GT(dims.width, 0);
  EXPECT_GT(dims.height, 0);
  EXPECT_GT(collector.img.height, 0);

  // Verify image is not all-black.
  size_t stride = (collector.img.width + 7) / 8;
  size_t white = 0, total = 0;
  for (uint16_t y = 0; y < collector.img.height; ++y) {
    for (uint16_t x = 0; x < collector.img.width; ++x) {
      bool is_white = (collector.img.data[y * stride + x / 8] >> (7 - (x & 7))) & 1;
      if (is_white)
        white++;
      total++;
    }
  }
  float white_pct = total > 0 ? 100.0f * white / total : 0;
  printf("  Decoded deflated JPEG: %ux%u  white=%.1f%%\n", dims.width, dims.height, white_pct);
  EXPECT_GT(white, 0u) << "Decoded image is completely black";
}
