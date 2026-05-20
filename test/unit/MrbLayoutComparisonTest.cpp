#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "TestBooks.h"
#include "TestChapterSource.h"
#include "microreader/content/Book.h"
#include "microreader/content/TextLayout.h"
#include "microreader/content/ZipReader.h"
#include "microreader/content/mrb/MrbConverter.h"
#include "microreader/content/mrb/MrbReader.h"
#include "microreader/display/DrawBuffer.h"

namespace fs = std::filesystem;
using namespace microreader;

// MrbChapterSource is now in MrbReader.h — used directly from library.

// ---------------------------------------------------------------------------
// Comparison helpers
// ---------------------------------------------------------------------------

static void assert_words_equal(const LayoutWord& a, const LayoutWord& b, const std::string& ctx) {
  std::string a_text(a.text, a.len);
  std::string b_text(b.text, b.len);
  EXPECT_EQ(a_text, b_text) << ctx << " text";
  EXPECT_EQ(a.x, b.x) << ctx << " x (text='" << a_text << "')";
  EXPECT_EQ(a.style, b.style) << ctx << " style";
  EXPECT_EQ(a.size_pct, b.size_pct) << ctx << " size";
  EXPECT_EQ(a.vertical_align, b.vertical_align) << ctx << " vertical_align";
  EXPECT_EQ(a.continues_prev, b.continues_prev) << ctx << " continues_prev";
}

static void assert_pages_equal(const PageContent& epub_page, const PageContent& mrb_page, const std::string& ctx) {
  EXPECT_EQ(epub_page.start, mrb_page.start) << ctx << " start";
  EXPECT_EQ(epub_page.end, mrb_page.end) << ctx << " end";
  EXPECT_EQ(epub_page.at_chapter_end, mrb_page.at_chapter_end) << ctx << " at_chapter_end";

  // Text items
  auto epub_texts = epub_page.text_items();
  auto mrb_texts = mrb_page.text_items();
  ASSERT_EQ(epub_texts.size(), mrb_texts.size()) << ctx << " text_items count";
  for (size_t ti = 0; ti < epub_texts.size(); ++ti) {
    const auto& ea = epub_texts[ti];
    const auto& ma = mrb_texts[ti];
    std::string tctx = ctx + " text[" + std::to_string(ti) + "]";

    EXPECT_EQ(ea.paragraph_index, ma.paragraph_index) << tctx << " para_idx";
    EXPECT_EQ(ea.line_index, ma.line_index) << tctx << " line_idx";
    EXPECT_EQ(ea.y_offset, ma.y_offset) << tctx << " y_offset";
    EXPECT_EQ(ea.line.hyphenated, ma.line.hyphenated) << tctx << " hyphenated";

    ASSERT_EQ(ea.line.words.size(), ma.line.words.size()) << tctx << " word count";
    for (size_t wi = 0; wi < ea.line.words.size(); ++wi) {
      assert_words_equal(ea.line.words[wi], ma.line.words[wi], tctx + " word[" + std::to_string(wi) + "]");
    }
  }

  // Image items
  auto epub_images = epub_page.image_items();
  auto mrb_images = mrb_page.image_items();
  ASSERT_EQ(epub_images.size(), mrb_images.size()) << ctx << " image_items count";
  for (size_t ii = 0; ii < epub_images.size(); ++ii) {
    const auto& ea = epub_images[ii];
    const auto& ma = mrb_images[ii];
    std::string ictx = ctx + " img[" + std::to_string(ii) + "]";

    EXPECT_EQ(ea.paragraph_index, ma.paragraph_index) << ictx << " para_idx";
    // Note: image key differs — EPUB uses zip entry index, MRB uses remapped
    // image ref table index.  Dimensions and positions are what matter.
    EXPECT_EQ(ea.width, ma.width) << ictx << " width";
    EXPECT_EQ(ea.height, ma.height) << ictx << " height";
    EXPECT_EQ(ea.x_offset, ma.x_offset) << ictx << " x_offset";
    EXPECT_EQ(ea.y_offset, ma.y_offset) << ictx << " y_offset";
  }

  // HR items
  auto epub_hrs = epub_page.hr_items();
  auto mrb_hrs = mrb_page.hr_items();
  ASSERT_EQ(epub_hrs.size(), mrb_hrs.size()) << ctx << " hr_items count";
  for (size_t hi = 0; hi < epub_hrs.size(); ++hi) {
    const auto& ea = epub_hrs[hi];
    const auto& ma = mrb_hrs[hi];
    std::string hctx = ctx + " hr[" + std::to_string(hi) + "]";

    EXPECT_EQ(ea.x_offset, ma.x_offset) << hctx << " x_offset";
    EXPECT_EQ(ea.y_offset, ma.y_offset) << hctx << " y_offset";
    EXPECT_EQ(ea.width, ma.width) << hctx << " width";
  }
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

#ifndef TEST_FIXTURES_DIR
#define TEST_FIXTURES_DIR "."
#endif

class MrbLayoutComparisonTest : public ::testing::Test {
 protected:
  std::string tmp_path_;

  void SetUp() override {
    tmp_path_ = (fs::temp_directory_path() / "mrb_layout_cmp.mrb").string();
  }

  void TearDown() override {
    std::remove(tmp_path_.c_str());
  }

  static std::string fixture(const char* name) {
    return std::string(TEST_FIXTURES_DIR) + "/" + name;
  }

  // Compare all chapters' full pagination between EPUB and MRB paths.
  void compare_book(const char* epub_name) {
    Book book;
    auto err = book.open(fixture(epub_name).c_str());
    if (err != EpubError::Ok)
      GTEST_SKIP() << epub_name << " not found";

    ASSERT_TRUE(convert_epub_to_mrb_streaming(book, tmp_path_.c_str())) << "Failed to convert " << epub_name;

    MrbReader mrb;
    ASSERT_TRUE(mrb.open(tmp_path_.c_str())) << "Failed to open MRB for " << epub_name;

    ASSERT_EQ(mrb.chapter_count(), static_cast<uint16_t>(book.chapter_count()))
        << epub_name << " chapter count mismatch";

    FixedFont font(16, 20);
    PageOptions opts(DrawBuffer::kWidth, DrawBuffer::kHeight, 20, 16, Alignment::Start);

    for (uint16_t ci = 0; ci < mrb.chapter_count(); ++ci) {
      // EPUB path: load chapter into memory.
      Chapter ch;
      book.load_chapter(ci, ch);

      // MRB path: on-demand paragraph source.
      MrbChapterSource mrb_src(mrb, ci);

      // Verify paragraph counts match.
      ASSERT_EQ(ch.paragraphs.size(), mrb_src.paragraph_count()) << epub_name << " ch" << ci << " paragraph count";

      // Paginate both and compare.
      TestChapterSource epub_src(ch);
      TextLayout epub_tl(font, opts, epub_src);
      TextLayout mrb_tl(font, opts, mrb_src);
      int page_num = 0;

      while (true) {
        auto epub_page = epub_tl.layout();
        auto mrb_page = mrb_tl.layout();

        std::string ctx = std::string(epub_name) + " ch" + std::to_string(ci) + " page" + std::to_string(page_num);
        assert_pages_equal(epub_page, mrb_page, ctx);

        if (epub_page.at_chapter_end) {
          EXPECT_TRUE(mrb_page.at_chapter_end) << ctx << " MRB should also be at chapter end";
          break;
        }
        ASSERT_FALSE(mrb_page.at_chapter_end) << ctx << " MRB at chapter end too early";

        epub_tl.set_position(epub_page.end);
        mrb_tl.set_position(mrb_page.end);
        ++page_num;

        ASSERT_LT(page_num, 5000) << ctx << " too many pages (safety limit)";
      }
    }
  }
};

// ---------------------------------------------------------------------------
// Per-fixture EPUB comparison tests
// ---------------------------------------------------------------------------

TEST_F(MrbLayoutComparisonTest, Basic) {
  compare_book("basic.epub");
}

TEST_F(MrbLayoutComparisonTest, MultiChapter) {
  compare_book("multi_chapter.epub");
}

TEST_F(MrbLayoutComparisonTest, LargeChapter) {
  compare_book("large_chapter.epub");
}

TEST_F(MrbLayoutComparisonTest, WithCss) {
  compare_book("with_css.epub");
}

TEST_F(MrbLayoutComparisonTest, WithImages) {
  compare_book("with_images.epub");
}

TEST_F(MrbLayoutComparisonTest, SpecialChars) {
  compare_book("special_chars.epub");
}

TEST_F(MrbLayoutComparisonTest, NestedDirs) {
  compare_book("nested_dirs.epub");
}

TEST_F(MrbLayoutComparisonTest, RegressionTest) {
  compare_book("regression_test.epub");
}

TEST_F(MrbLayoutComparisonTest, Stored) {
  compare_book("stored.epub");
}

// ---------------------------------------------------------------------------
// Bulk comparison: curated EPUB list from TestBooks.h
// ---------------------------------------------------------------------------

class BulkMrbComparisonTest : public ::testing::TestWithParam<std::string> {
 protected:
  std::string tmp_path_;

  void SetUp() override {
    tmp_path_ = (fs::temp_directory_path() / "mrb_bulk_cmp.mrb").string();
  }

  void TearDown() override {
    std::remove(tmp_path_.c_str());
  }
};

TEST_P(BulkMrbComparisonTest, LayoutMatchesEpub) {
  const std::string& epub_path = GetParam();
  auto filename = fs::path(epub_path).filename().string();

  Book book;
  auto err = book.open(epub_path.c_str());
  if (err != EpubError::Ok)
    GTEST_SKIP() << "Cannot open " << filename << ": error " << (int)err;

  ASSERT_TRUE(convert_epub_to_mrb_streaming(book, tmp_path_.c_str())) << "Failed to convert " << filename;

  MrbReader mrb;
  ASSERT_TRUE(mrb.open(tmp_path_.c_str())) << "Failed to open MRB for " << filename;

  ASSERT_EQ(mrb.chapter_count(), static_cast<uint16_t>(book.chapter_count())) << filename << " chapter count mismatch";

  FixedFont font(16, 20);
  PageOptions opts(DrawBuffer::kWidth, DrawBuffer::kHeight, 20, 16, Alignment::Start);

  size_t total_pages = 0;
  for (uint16_t ci = 0; ci < mrb.chapter_count(); ++ci) {
    Chapter ch;
    book.load_chapter(ci, ch);

    MrbChapterSource mrb_src(mrb, ci);
    ASSERT_EQ(ch.paragraphs.size(), mrb_src.paragraph_count()) << filename << " ch" << ci << " paragraph count";

    TestChapterSource epub_src(ch);
    TextLayout epub_tl(font, opts, epub_src);
    TextLayout mrb_tl(font, opts, mrb_src);
    int page_num = 0;

    while (true) {
      auto epub_page = epub_tl.layout();
      auto mrb_page = mrb_tl.layout();

      std::string ctx = filename + " ch" + std::to_string(ci) + " page" + std::to_string(page_num);
      assert_pages_equal(epub_page, mrb_page, ctx);

      if (epub_page.at_chapter_end) {
        EXPECT_TRUE(mrb_page.at_chapter_end) << ctx;
        break;
      }
      ASSERT_FALSE(mrb_page.at_chapter_end) << ctx;

      epub_tl.set_position(epub_page.end);
      mrb_tl.set_position(mrb_page.end);
      ++page_num;
      ++total_pages;

      ASSERT_LT(page_num, 50000) << ctx << " too many pages (safety limit)";
    }
    total_pages++;
  }
  printf("  [%s] %d chapters, %zu pages — MATCH\n", filename.c_str(), mrb.chapter_count(), total_pages);
}

INSTANTIATE_EPUB_TESTS(BulkMrbComparisonTest);

// ---------------------------------------------------------------------------
// Benchmark: measure streaming conversion time per book
// ---------------------------------------------------------------------------

class ConversionBenchmark : public ::testing::TestWithParam<std::string> {
 protected:
  std::string mrb_path_;

  void SetUp() override {
    mrb_path_ = (fs::temp_directory_path() / "mrb_bench.mrb").string();
  }

  void TearDown() override {
    std::remove(mrb_path_.c_str());
    std::remove((mrb_path_ + ".idx").c_str());
  }
};

TEST_P(ConversionBenchmark, StreamingConversion) {
  const std::string& epub_path = GetParam();
  auto filename = fs::path(epub_path).filename().string();

  // --- Phase 1: Book open ---
  auto t0 = std::chrono::high_resolution_clock::now();
  Book book;
  auto err = book.open(epub_path.c_str());
  auto t1 = std::chrono::high_resolution_clock::now();
  if (err != EpubError::Ok)
    GTEST_SKIP() << "Cannot open " << filename;

  double open_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

  // --- Phase 2: Streaming conversion ---
  auto t2 = std::chrono::high_resolution_clock::now();
  bool ok = convert_epub_to_mrb_streaming(book, mrb_path_.c_str());
  auto t3 = std::chrono::high_resolution_clock::now();
  ASSERT_TRUE(ok) << "Conversion failed for " << filename;

  double convert_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
  double total_ms = std::chrono::duration<double, std::milli>(t3 - t0).count();

  // Get file sizes
  auto epub_size = fs::file_size(epub_path);
  auto mrb_size = fs::file_size(mrb_path_);

  printf("  %-45s  open=%6.1fms  convert=%7.1fms  total=%7.1fms  epub=%5.1fMB  mrb=%5.1fMB  chapters=%zu\n",
         filename.c_str(), open_ms, convert_ms, total_ms, epub_size / 1048576.0, mrb_size / 1048576.0,
         book.chapter_count());
}

INSTANTIATE_EPUB_TESTS(ConversionBenchmark);
