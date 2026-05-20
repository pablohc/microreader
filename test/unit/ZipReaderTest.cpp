#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include "microreader/content/ZipReader.h"

using namespace microreader;

// Helper: path to a test fixture file.
static std::string fixture(const char* name) {
  return std::string(TEST_FIXTURES_DIR) + "/" + name;
}

// ---------------------------------------------------------------------------
// StdioZipFile basics
// ---------------------------------------------------------------------------

TEST(StdioZipFile, OpenNonExistent) {
  StdioZipFile f;
  EXPECT_FALSE(f.open("this_file_does_not_exist.epub"));
  EXPECT_FALSE(f.is_open());
}

TEST(StdioZipFile, OpenValid) {
  StdioZipFile f;
  ASSERT_TRUE(f.open(fixture("basic.epub").c_str()));
  EXPECT_TRUE(f.is_open());
  f.close();
  EXPECT_FALSE(f.is_open());
}

// ---------------------------------------------------------------------------
// ZipReader::open — parse central directory
// ---------------------------------------------------------------------------

class ZipReaderTest : public ::testing::Test {
 protected:
  StdioZipFile file;
  ZipReader reader;

  void open_fixture(const char* name) {
    ASSERT_TRUE(file.open(fixture(name).c_str())) << "Failed to open " << name;
    ASSERT_EQ(reader.open(file), ZipError::Ok) << "Failed to parse " << name;
  }
};

TEST_F(ZipReaderTest, BasicEpubEntries) {
  open_fixture("basic.epub");
  // basic.epub: mimetype, META-INF/container.xml, OEBPS/content.opf, OEBPS/chapter1.xhtml
  EXPECT_GE(reader.entry_count(), 3u);

  // mimetype must be the first entry
  EXPECT_EQ(reader.entry(0).name, "mimetype");
  EXPECT_EQ(reader.entry(0).compression, 0);  // stored
}

TEST_F(ZipReaderTest, FindByName) {
  open_fixture("basic.epub");

  auto* mime = reader.find("mimetype");
  ASSERT_NE(mime, nullptr);
  EXPECT_EQ(mime->name, "mimetype");

  auto* container = reader.find("META-INF/container.xml");
  ASSERT_NE(container, nullptr);

  auto* missing = reader.find("does_not_exist.txt");
  EXPECT_EQ(missing, nullptr);
}

TEST_F(ZipReaderTest, StoredEpubAllStored) {
  open_fixture("stored.epub");
  // All entries (except maybe mimetype which is always stored) should be stored
  for (size_t i = 0; i < reader.entry_count(); ++i) {
    EXPECT_EQ(reader.entry(i).compression, 0) << "Entry " << reader.entry(i).name << " should be stored";
  }
}

TEST_F(ZipReaderTest, MultiChapterEntries) {
  open_fixture("multi_chapter.epub");
  EXPECT_GE(reader.entry_count(), 6u);  // mimetype + container + opf + ncx + 3 chapters

  ASSERT_NE(reader.find("OEBPS/chapter1.xhtml"), nullptr);
  ASSERT_NE(reader.find("OEBPS/chapter2.xhtml"), nullptr);
  ASSERT_NE(reader.find("OEBPS/chapter3.xhtml"), nullptr);
  ASSERT_NE(reader.find("OEBPS/toc.ncx"), nullptr);
}

TEST_F(ZipReaderTest, NestedDirEntries) {
  open_fixture("nested_dirs.epub");
  ASSERT_NE(reader.find("OEBPS/sub/content.opf"), nullptr);
  ASSERT_NE(reader.find("OEBPS/sub/chapters/chapter1.xhtml"), nullptr);
}

TEST_F(ZipReaderTest, WithImagesEntries) {
  open_fixture("with_images.epub");
  ASSERT_NE(reader.find("OEBPS/images/test.jpg"), nullptr);
  ASSERT_NE(reader.find("OEBPS/images/test.png"), nullptr);

  // Image entries should be stored (uncompressed)
  auto* jpg = reader.find("OEBPS/images/test.jpg");
  EXPECT_EQ(jpg->compression, 0);
  auto* png = reader.find("OEBPS/images/test.png");
  EXPECT_EQ(png->compression, 0);
}

// ---------------------------------------------------------------------------
// ZipReader::extract — full extraction to buffer
// ---------------------------------------------------------------------------

TEST_F(ZipReaderTest, ExtractMimetype) {
  open_fixture("basic.epub");
  auto* entry = reader.find("mimetype");
  ASSERT_NE(entry, nullptr);

  std::vector<uint8_t> data;
  ASSERT_EQ(reader.extract(file, *entry, data), ZipError::Ok);

  std::string text(data.begin(), data.end());
  EXPECT_EQ(text, "application/epub+zip");
}

TEST_F(ZipReaderTest, ExtractStoredFile) {
  open_fixture("stored.epub");
  auto* entry = reader.find("META-INF/container.xml");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->compression, 0);

  std::vector<uint8_t> data;
  ASSERT_EQ(reader.extract(file, *entry, data), ZipError::Ok);

  std::string text(data.begin(), data.end());
  EXPECT_NE(text.find("rootfile"), std::string::npos) << "Should contain 'rootfile'";
  EXPECT_NE(text.find("content.opf"), std::string::npos);
}

TEST_F(ZipReaderTest, ExtractDeflatedFile) {
  open_fixture("basic.epub");
  auto* entry = reader.find("OEBPS/content.opf");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->compression, 8);  // deflate

  std::vector<uint8_t> data;
  ASSERT_EQ(reader.extract(file, *entry, data), ZipError::Ok);

  std::string text(data.begin(), data.end());
  EXPECT_NE(text.find("<package"), std::string::npos);
  EXPECT_NE(text.find("Basic Test"), std::string::npos);
}

TEST_F(ZipReaderTest, ExtractXhtmlChapter) {
  open_fixture("basic.epub");
  auto* entry = reader.find("OEBPS/chapter1.xhtml");
  ASSERT_NE(entry, nullptr);

  std::vector<uint8_t> data;
  ASSERT_EQ(reader.extract(file, *entry, data), ZipError::Ok);

  std::string text(data.begin(), data.end());
  EXPECT_NE(text.find("Hello, world!"), std::string::npos);
  EXPECT_NE(text.find("Chapter One"), std::string::npos);
}

TEST_F(ZipReaderTest, ExtractUnicodeContent) {
  open_fixture("special_chars.epub");
  auto* entry = reader.find("OEBPS/chapter1.xhtml");
  ASSERT_NE(entry, nullptr);

  std::vector<uint8_t> data;
  ASSERT_EQ(reader.extract(file, *entry, data), ZipError::Ok);

  std::string text(data.begin(), data.end());
  EXPECT_NE(text.find("Ünïcödë"), std::string::npos);
  EXPECT_NE(text.find("日本語テスト"), std::string::npos);
}

TEST_F(ZipReaderTest, ExtractLargeChapter) {
  open_fixture("large_chapter.epub");
  auto* entry = reader.find("OEBPS/chapter1.xhtml");
  ASSERT_NE(entry, nullptr);

  std::vector<uint8_t> data;
  ASSERT_EQ(reader.extract(file, *entry, data), ZipError::Ok);

  std::string text(data.begin(), data.end());
  // Should contain first and last paragraphs
  EXPECT_NE(text.find("Paragraph 1:"), std::string::npos);
  EXPECT_NE(text.find("Paragraph 200:"), std::string::npos);
}

TEST_F(ZipReaderTest, ExtractJpegImage) {
  open_fixture("with_images.epub");
  auto* entry = reader.find("OEBPS/images/test.jpg");
  ASSERT_NE(entry, nullptr);

  std::vector<uint8_t> data;
  ASSERT_EQ(reader.extract(file, *entry, data), ZipError::Ok);

  // Verify JPEG magic: FF D8 FF
  ASSERT_GE(data.size(), 3u);
  EXPECT_EQ(data[0], 0xFF);
  EXPECT_EQ(data[1], 0xD8);
  EXPECT_EQ(data[2], 0xFF);
}

TEST_F(ZipReaderTest, ExtractPngImage) {
  open_fixture("with_images.epub");
  auto* entry = reader.find("OEBPS/images/test.png");
  ASSERT_NE(entry, nullptr);

  std::vector<uint8_t> data;
  ASSERT_EQ(reader.extract(file, *entry, data), ZipError::Ok);

  // Verify PNG magic
  ASSERT_GE(data.size(), 8u);
  EXPECT_EQ(data[0], 0x89);
  EXPECT_EQ(data[1], 'P');
  EXPECT_EQ(data[2], 'N');
  EXPECT_EQ(data[3], 'G');
}

// ---------------------------------------------------------------------------
// ZipReader::extract_streaming — callback-based extraction
// ---------------------------------------------------------------------------

TEST_F(ZipReaderTest, StreamingExtract) {
  open_fixture("basic.epub");
  auto* entry = reader.find("OEBPS/chapter1.xhtml");
  ASSERT_NE(entry, nullptr);

  // Collect data via callback.
  std::vector<uint8_t> collected;
  auto cb = [](const uint8_t* data, size_t size, void* user_data) -> bool {
    auto* out = static_cast<std::vector<uint8_t>*>(user_data);
    out->insert(out->end(), data, data + size);
    return true;
  };

  std::vector<uint8_t> work_buf(45 * 1024);  // 45KB (decomp + dict + input)
  ASSERT_EQ(reader.extract_streaming(file, *entry, cb, &collected, work_buf.data(), work_buf.size()), ZipError::Ok);

  // Compare with full extract
  std::vector<uint8_t> full;
  ASSERT_EQ(reader.extract(file, *entry, full), ZipError::Ok);
  EXPECT_EQ(collected, full);
}

TEST_F(ZipReaderTest, StreamingAbort) {
  open_fixture("large_chapter.epub");
  auto* entry = reader.find("OEBPS/chapter1.xhtml");
  ASSERT_NE(entry, nullptr);

  size_t bytes_received = 0;
  auto cb = [](const uint8_t* data, size_t size, void* user_data) -> bool {
    auto* count = static_cast<size_t*>(user_data);
    *count += size;
    return *count < 100;  // abort after 100 bytes
  };

  std::vector<uint8_t> work_buf(45 * 1024);  // 45KB (decomp + dict + input)
  // Should not error — user abort is graceful
  ASSERT_EQ(reader.extract_streaming(file, *entry, cb, &bytes_received, work_buf.data(), work_buf.size()),
            ZipError::Ok);
  EXPECT_GE(bytes_received, 100u);
}

// ---------------------------------------------------------------------------
// Consistency: extract all entries from every test EPUB
// ---------------------------------------------------------------------------

class ZipReaderAllEpubsTest : public ::testing::TestWithParam<const char*> {};

TEST_P(ZipReaderAllEpubsTest, ExtractAllEntries) {
  StdioZipFile file;
  ASSERT_TRUE(file.open(fixture(GetParam()).c_str())) << GetParam();

  ZipReader reader;
  ASSERT_EQ(reader.open(file), ZipError::Ok) << GetParam();
  EXPECT_GT(reader.entry_count(), 0u);

  for (size_t i = 0; i < reader.entry_count(); ++i) {
    const auto& entry = reader.entry(i);

    // Skip directory entries
    if (!entry.name.empty() && entry.name.back() == '/')
      continue;

    std::vector<uint8_t> data;
    ZipError err = reader.extract(file, entry, data);
    EXPECT_EQ(err, ZipError::Ok) << "Failed to extract: " << entry.name << " from " << GetParam();
    EXPECT_EQ(data.size(), entry.uncompressed_size) << entry.name;
  }
}

INSTANTIATE_TEST_SUITE_P(AllEpubs, ZipReaderAllEpubsTest,
                         ::testing::Values("basic.epub", "multi_chapter.epub", "with_css.epub", "with_images.epub",
                                           "stored.epub", "nested_dirs.epub", "special_chars.epub",
                                           "large_chapter.epub"));
