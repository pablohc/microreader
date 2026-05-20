// NavigationTest.cpp — full-book page navigation without rendering.
// These tests convert EPUB→MRB, then paginate every chapter page-by-page
// using layout_page(), verifying the layout engine can traverse entire
// books without crashing or getting stuck in infinite loops.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "TestBooks.h"
#include "microreader/content/Book.h"
#include "microreader/content/TextLayout.h"
#include "microreader/content/mrb/MrbConverter.h"
#include "microreader/content/mrb/MrbReader.h"
#include "microreader/display/DrawBuffer.h"

using namespace microreader;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Match the real device layout parameters (ReaderScreen constants).
// ---------------------------------------------------------------------------
static constexpr int kScale = 2;
static constexpr int kGlyphW = 8;
static constexpr int kGlyphH = 8;
static constexpr int kPadding = 20;
static constexpr int kParaSpacing = 12;
static constexpr int kWidth = DrawBuffer::kWidth;
static constexpr int kHeight = DrawBuffer::kHeight;

static FixedFont device_font() {
  return FixedFont(kGlyphW * kScale, kGlyphH * kScale + 4);
}

static PageOptions device_opts() {
  PageOptions opts(kWidth, kHeight, kPadding, kParaSpacing, Alignment::Start);
  return opts;
}

// ---------------------------------------------------------------------------
// Navigate an entire book: EPUB → MRB → paginate all chapters.
// Returns total page count (0 on failure).
// ---------------------------------------------------------------------------
struct NavResult {
  bool ok = false;
  int chapters = 0;
  int total_pages = 0;
  int max_pages_in_chapter = 0;
};

static NavResult navigate_book(const std::string& epub_path) {
  NavResult result;

  // Convert EPUB → MRB in a temp file.
  auto mrb_path = (fs::temp_directory_path() / "nav_test.mrb").string();

  Book book;
  auto err = book.open(epub_path.c_str());
  if (err != EpubError::Ok)
    return result;

  if (!convert_epub_to_mrb_streaming(book, mrb_path.c_str()))
    return result;

  MrbReader mrb;
  if (!mrb.open(mrb_path.c_str())) {
    std::remove(mrb_path.c_str());
    return result;
  }

  auto font = device_font();
  auto opts = device_opts();

  result.chapters = mrb.chapter_count();

  for (uint16_t ci = 0; ci < mrb.chapter_count(); ++ci) {
    MrbChapterSource src(mrb, ci);
    int chapter_pages = 0;

    TextLayout tl(font, opts, src);
    while (true) {
      auto page = tl.layout();
      ++chapter_pages;

      if (page.at_chapter_end)
        break;

      // Safety: abort if stuck in an infinite loop.
      if (chapter_pages > 1000) {
        printf("    ABORT: ch%d exceeded 1000 pages at pos{%u,%u}\n", ci, page.end.paragraph, page.end.offset);
        mrb.close();
        std::remove(mrb_path.c_str());
        return result;
      }

      tl.set_position(page.end);
    }

    result.total_pages += chapter_pages;
    if (chapter_pages > result.max_pages_in_chapter)
      result.max_pages_in_chapter = chapter_pages;
  }

  mrb.close();
  std::remove(mrb_path.c_str());
  result.ok = true;
  return result;
}

// ---------------------------------------------------------------------------
// Parameterized test: navigates one book end-to-end.
// ---------------------------------------------------------------------------

class FullBookNavTest : public ::testing::TestWithParam<std::string> {};

TEST_P(FullBookNavTest, NavigateAllPages) {
  auto path = GetParam();
  if (!fs::exists(path))
    GTEST_SKIP() << "Book not found: " << path;

  auto name = fs::path(path).filename().string();
  auto result = navigate_book(path);

  ASSERT_TRUE(result.ok) << name << " navigation failed";
  EXPECT_GT(result.chapters, 0) << name;
  EXPECT_GT(result.total_pages, 0) << name;

  printf("  %s: %d chapters, %d pages (max %d/chapter)\n", name.c_str(), result.chapters, result.total_pages,
         result.max_pages_in_chapter);
}

// ---------------------------------------------------------------------------
// Test suites — smoke (unit_tests) vs full (microreader_tests).
// ---------------------------------------------------------------------------

INSTANTIATE_EPUB_TESTS(FullBookNavTest);

#ifndef SMOKE_TESTS_ONLY
INSTANTIATE_TEST_SUITE_P(AllNav, FullBookNavTest, ::testing::ValuesIn(test_books::get_all_books()),
                         EPUB_TEST_PARAM_NAME);
#endif

// ---------------------------------------------------------------------------
// Backward navigation test: page backward from end of each chapter.
// ---------------------------------------------------------------------------

class BackwardNavTest : public ::testing::TestWithParam<std::string> {};

TEST_P(BackwardNavTest, NavigateBackwardAllPages) {
  auto path = GetParam();
  if (!fs::exists(path))
    GTEST_SKIP() << "Book not found: " << path;

  auto name = fs::path(path).filename().string();
  auto mrb_path = (fs::temp_directory_path() / "nav_back_test.mrb").string();

  Book book;
  auto err = book.open(path.c_str());
  ASSERT_EQ(err, EpubError::Ok) << name << " open failed";
  ASSERT_TRUE(convert_epub_to_mrb_streaming(book, mrb_path.c_str())) << name << " convert failed";

  MrbReader mrb;
  ASSERT_TRUE(mrb.open(mrb_path.c_str())) << name << " MRB open failed";

  auto font = device_font();
  auto opts = device_opts();

  int total_fwd = 0;
  int total_bwd = 0;

  for (uint16_t ci = 0; ci < mrb.chapter_count(); ++ci) {
    MrbChapterSource src(mrb, ci);

    // Forward pass to count pages and find chapter end.
    int fwd_pages = 0;
    TextLayout tl(font, opts, src);
    while (true) {
      auto page = tl.layout();
      ++fwd_pages;
      if (page.at_chapter_end)
        break;
      ASSERT_LT(fwd_pages, 1000) << name << " ch" << ci << " forward stuck";
      tl.set_position(page.end);
    }

    // Backward pass from chapter end.
    PagePosition end_pos{static_cast<uint16_t>(src.paragraph_count()), 0};
    int bwd_pages = 0;
    while (true) {
      tl.set_position(end_pos);
      auto page = tl.layout_backward();
      ++bwd_pages;
      if (page.start.paragraph == 0 && page.start.offset == 0)
        break;
      ASSERT_LT(bwd_pages, 1000) << name << " ch" << ci << " backward stuck";
      end_pos = page.start;
    }

    EXPECT_EQ(fwd_pages, bwd_pages) << name << " ch" << ci << " page count mismatch (fwd=" << fwd_pages
                                    << " bwd=" << bwd_pages << ")";

    total_fwd += fwd_pages;
    total_bwd += bwd_pages;
  }

  printf("  %s: %d chapters, fwd=%d bwd=%d pages\n", name.c_str(), mrb.chapter_count(), total_fwd, total_bwd);

  mrb.close();
  std::remove(mrb_path.c_str());
}

INSTANTIATE_EPUB_TESTS(BackwardNavTest);
