// Bulk book test: auto-discovers ALL .epub files in test directories and
// verifies the full parsing pipeline for each. This catches regressions
// across a large corpus of real-world EPUBs.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "microreader/content/Book.h"
#include "microreader/content/ContentModel.h"
#include "microreader/content/ImageDecoder.h"

namespace fs = std::filesystem;
using namespace microreader;

// ---------------------------------------------------------------------------
// Directory discovery
// ---------------------------------------------------------------------------

static std::string workspace_root() {
  std::string fixtures = TEST_FIXTURES_DIR;
  // fixtures = .../microreader2/test/fixtures → workspace = 3 levels up
  auto pos = fixtures.rfind('/');
  if (pos == std::string::npos)
    pos = fixtures.rfind('\\');
  std::string up1 = fixtures.substr(0, pos);
  pos = up1.rfind('/');
  if (pos == std::string::npos)
    pos = up1.rfind('\\');
  std::string up2 = up1.substr(0, pos);
  pos = up2.rfind('/');
  if (pos == std::string::npos)
    pos = up2.rfind('\\');
  return up2.substr(0, pos);
}

static std::vector<std::string> discover_epubs(const std::string& dir) {
  std::vector<std::string> result;
  if (!fs::exists(dir))
    return result;
  for (auto& entry : fs::recursive_directory_iterator(dir)) {
    if (entry.is_regular_file()) {
      auto ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext == ".epub") {
        result.push_back(entry.path().string());
      }
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

// ---------------------------------------------------------------------------
// Stats tracking
// ---------------------------------------------------------------------------

struct BookResult {
  std::string path;
  bool opened = false;
  std::string error;
  std::string title;
  std::string author;
  std::string language;
  size_t chapters = 0;
  size_t text_paragraphs = 0;
  size_t images_found = 0;
  size_t images_decoded = 0;
  size_t total_chars = 0;
  size_t empty_chapters = 0;
  bool has_bold = false;
  bool has_italic = false;
  size_t toc_entries = 0;
};

static BookResult test_book(const std::string& path) {
  BookResult r;
  r.path = path;

  Book book;
  auto err = book.open(path.c_str());
  if (err != EpubError::Ok) {
    r.error = "open failed: error " + std::to_string((int)err);
    return r;
  }
  r.opened = true;
  r.title = book.metadata().title;
  r.author = book.metadata().author.value_or("");
  r.language = book.metadata().language.value_or("");
  r.chapters = book.chapter_count();
  r.toc_entries = book.toc().entries.size();

  for (size_t i = 0; i < book.chapter_count(); ++i) {
    Chapter ch;
    auto cerr = book.load_chapter(i, ch);
    if (cerr != EpubError::Ok) {
      r.error += "ch" + std::to_string(i) + " error=" + std::to_string((int)cerr) + "; ";
      continue;
    }

    if (ch.paragraphs.empty()) {
      r.empty_chapters++;
      continue;
    }

    for (auto& p : ch.paragraphs) {
      switch (p.type) {
        case ParagraphType::Text:
          r.text_paragraphs++;
          for (auto& run : p.text.runs) {
            r.total_chars += run.text.size();
            if (run.style == FontStyle::Bold || run.style == FontStyle::BoldItalic)
              r.has_bold = true;
            if (run.style == FontStyle::Italic || run.style == FontStyle::BoldItalic)
              r.has_italic = true;
          }
          break;
        case ParagraphType::Image: {
          r.images_found++;
          DecodedImage img;
          if (book.decode_image(p.image.key, img) == ImageError::Ok) {
            r.images_decoded++;
          }
          break;
        }
        case ParagraphType::Hr:
          break;
      }
    }
  }

  return r;
}

// ---------------------------------------------------------------------------
// Parameterized test for test/books/other/
// ---------------------------------------------------------------------------

class OtherBooksTest : public ::testing::TestWithParam<std::string> {};

TEST_P(OtherBooksTest, ParsesSuccessfully) {
  auto r = test_book(GetParam());

  // Must open
  ASSERT_TRUE(r.opened) << "Failed to open: " << r.path << " — " << r.error;

  // Must have chapters
  EXPECT_GT(r.chapters, 0u) << r.path << " has no chapters";

  // Must have some text content
  EXPECT_GT(r.text_paragraphs, 0u) << r.path << " has no text paragraphs";
  EXPECT_GT(r.total_chars, 50u) << r.path << " has very little text";

  // Report
  printf("  [%s]\n", fs::path(r.path).filename().string().c_str());
  printf("    Title: %s | Author: %s | Lang: %s\n", r.title.c_str(), r.author.c_str(), r.language.c_str());
  printf("    Chapters: %zu (empty: %zu) | Paragraphs: %zu | Chars: %zu\n", r.chapters, r.empty_chapters,
         r.text_paragraphs, r.total_chars);
  printf("    Images: %zu found, %zu decoded | TOC: %zu entries\n", r.images_found, r.images_decoded, r.toc_entries);
  printf("    Formatting: bold=%s italic=%s\n", r.has_bold ? "yes" : "no", r.has_italic ? "yes" : "no");
  if (!r.error.empty())
    printf("    Errors: %s\n", r.error.c_str());
}

static std::vector<std::string> get_other_books() {
  std::string dir = workspace_root() + "/microreader2/test/books/other";
  return discover_epubs(dir);
}

INSTANTIATE_TEST_SUITE_P(OtherBooks, OtherBooksTest, ::testing::ValuesIn(get_other_books()),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                           // Use filename (sanitized) as test name
                           auto name = fs::path(info.param).stem().string();
                           // Replace non-alphanumeric chars with underscore
                           for (auto& c : name) {
                             if (!std::isalnum(static_cast<unsigned char>(c)))
                               c = '_';
                           }
                           return name;
                         });

// ---------------------------------------------------------------------------
// Parameterized test for test/books/gutenberg/
// ---------------------------------------------------------------------------

class GutenbergBooksTest : public ::testing::TestWithParam<std::string> {};

TEST_P(GutenbergBooksTest, ParsesSuccessfully) {
  auto r = test_book(GetParam());

  ASSERT_TRUE(r.opened) << "Failed to open: " << r.path << " — " << r.error;
  EXPECT_GT(r.chapters, 0u) << r.path;
  EXPECT_GT(r.text_paragraphs, 0u) << r.path;

  printf("  [%s] %s — ch:%zu p:%zu img:%zu/%zu chars:%zu\n", fs::path(r.path).filename().string().c_str(),
         r.title.c_str(), r.chapters, r.text_paragraphs, r.images_decoded, r.images_found, r.total_chars);
}

static std::vector<std::string> get_gutenberg_books() {
  std::string dir = workspace_root() + "/microreader2/test/books/gutenberg";
  return discover_epubs(dir);
}

INSTANTIATE_TEST_SUITE_P(GutenbergBooks, GutenbergBooksTest, ::testing::ValuesIn(get_gutenberg_books()),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                           auto name = fs::path(info.param).stem().string();
                           for (auto& c : name) {
                             if (!std::isalnum(static_cast<unsigned char>(c)))
                               c = '_';
                           }
                           return name;
                         });

// ---------------------------------------------------------------------------
// Also test books from microreader/resources/books/ and TrustyReader/sd/
// ---------------------------------------------------------------------------

class LegacyBooksTest : public ::testing::TestWithParam<std::string> {};

TEST_P(LegacyBooksTest, ParsesSuccessfully) {
  auto r = test_book(GetParam());

  ASSERT_TRUE(r.opened) << "Failed to open: " << r.path << " — " << r.error;
  EXPECT_GT(r.chapters, 0u) << r.path;

  printf("  [%s] %s — ch:%zu p:%zu img:%zu/%zu chars:%zu\n", fs::path(r.path).filename().string().c_str(),
         r.title.c_str(), r.chapters, r.text_paragraphs, r.images_decoded, r.images_found, r.total_chars);
}

static std::vector<std::string> get_legacy_books() {
  std::string root = workspace_root();
  std::vector<std::string> all;
  for (auto& dir : {root + "/microreader/resources/books", root + "/TrustyReader/sd"}) {
    auto books = discover_epubs(dir);
    all.insert(all.end(), books.begin(), books.end());
  }
  return all;
}

INSTANTIATE_TEST_SUITE_P(LegacyBooks, LegacyBooksTest, ::testing::ValuesIn(get_legacy_books()),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                           // Use parent_dir + filename to avoid duplicates (e.g. mabuse.epub in multiple dirs)
                           auto p = fs::path(info.param);
                           auto name = p.parent_path().filename().string() + "_" + p.stem().string();
                           for (auto& c : name) {
                             if (!std::isalnum(static_cast<unsigned char>(c)))
                               c = '_';
                           }
                           return name;
                         });

// ---------------------------------------------------------------------------
// Summary test: runs all directories and prints aggregate stats
// ---------------------------------------------------------------------------

TEST(BulkBookSummary, AllDirectories) {
  std::string root = workspace_root();
  std::vector<std::pair<std::string, std::string>> dirs = {
      {"other",     root + "/microreader2/test/books/other"    },
      {"gutenberg", root + "/microreader2/test/books/gutenberg"},
      {"legacy",    root + "/microreader/resources/books"      },
      {"trusty",    root + "/TrustyReader/sd"                  },
  };

  size_t total_books = 0, total_passed = 0, total_failed = 0;
  size_t total_chapters = 0, total_paragraphs = 0, total_images = 0;
  size_t total_chars = 0;

  for (auto& [label, dir] : dirs) {
    auto books = discover_epubs(dir);
    if (books.empty())
      continue;

    printf("\n=== %s (%zu books) ===\n", label.c_str(), books.size());
    for (auto& path : books) {
      auto r = test_book(path);
      total_books++;
      if (r.opened && r.chapters > 0 && r.text_paragraphs > 0) {
        total_passed++;
        total_chapters += r.chapters;
        total_paragraphs += r.text_paragraphs;
        total_images += r.images_decoded;
        total_chars += r.total_chars;
        printf("  PASS [%s] ch:%zu p:%zu img:%zu chars:%zu\n", fs::path(path).filename().string().c_str(), r.chapters,
               r.text_paragraphs, r.images_decoded, r.total_chars);
      } else {
        total_failed++;
        printf("  FAIL [%s] %s\n", fs::path(path).filename().string().c_str(), r.error.c_str());
      }
    }
  }

  printf("\n=== GRAND TOTAL ===\n");
  printf("  Books: %zu tested, %zu passed, %zu failed\n", total_books, total_passed, total_failed);
  printf("  Chapters: %zu | Paragraphs: %zu | Images: %zu | Chars: %zu\n", total_chapters, total_paragraphs,
         total_images, total_chars);

  EXPECT_EQ(total_failed, 0u) << total_failed << " books failed parsing";
}
