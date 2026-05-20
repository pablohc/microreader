// StreamingPipelineTest.cpp — intermediate-level tests that verify each stage
// of the streaming pipeline matches the non-streaming (full-extract) path.
//
// Level 1: ZipEntryInput read-all vs ZipReader::extract (raw bytes)
// Level 2: parse_chapter_streaming paragraphs vs parse_chapter paragraphs
//
// These help isolate *where* in the pipeline data gets lost when the
// end-to-end StreamingMrbComparisonTest fails.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "TestBooks.h"
#include "microreader/content/Book.h"
#include "microreader/content/EpubParser.h"
#include "microreader/content/ZipReader.h"

namespace fs = std::filesystem;
using namespace microreader;

// ===========================================================================
// Level 1: ZipEntryInput read-all vs ZipReader::extract
// Verifies that streaming decompression produces byte-identical output
// to one-shot extraction for every XHTML entry in a book.
// ===========================================================================

class ZipStreamingTest : public ::testing::TestWithParam<std::string> {};

TEST_P(ZipStreamingTest, AllXhtmlEntriesMatch) {
  const std::string& epub_path = GetParam();
  auto filename = fs::path(epub_path).filename().string();

  StdioZipFile file;
  ASSERT_TRUE(file.open(epub_path.c_str())) << filename;

  ZipReader zip;
  ASSERT_EQ(zip.open(file), ZipError::Ok) << filename;

  static constexpr size_t kWorkBufSize = ZipEntryInput::kDecompSize + ZipEntryInput::kDictSize + 1024;
  std::vector<uint8_t> work_buf(kWorkBufSize);

  size_t tested = 0;
  for (size_t i = 0; i < zip.entry_count(); ++i) {
    auto& entry = zip.entry(i);
    auto name = std::string(entry.name);

    // Only test XHTML/HTML entries (the ones parsed by the streaming converter).
    bool is_xhtml =
        name.size() > 4 && (name.find(".xhtml") != std::string::npos || name.find(".html") != std::string::npos ||
                            name.find(".htm") != std::string::npos);
    if (!is_xhtml)
      continue;

    // One-shot extract
    std::vector<uint8_t> expected;
    ASSERT_EQ(zip.extract(file, entry, expected), ZipError::Ok) << filename << " extract failed: " << name;

    // Streaming read-all via ZipEntryInput
    ZipEntryInput input;
    ASSERT_EQ(input.open(file, entry, work_buf.data(), work_buf.size()), ZipError::Ok)
        << filename << " ZipEntryInput::open failed: " << name;

    std::vector<uint8_t> streamed;
    streamed.reserve(entry.uncompressed_size);
    uint8_t chunk[4096];
    for (;;) {
      size_t n = input.read(chunk, sizeof(chunk));
      if (n == 0)
        break;
      streamed.insert(streamed.end(), chunk, chunk + n);
    }

    EXPECT_FALSE(input.has_error()) << filename << " " << name << " ZipEntryInput error after read";
    ASSERT_EQ(streamed.size(), expected.size())
        << filename << " " << name << " size mismatch"
        << " (streamed=" << streamed.size() << " expected=" << expected.size() << ")";
    EXPECT_EQ(streamed, expected) << filename << " " << name << " content mismatch";

    ++tested;
  }

  printf("  [%s] %zu XHTML entries — ZIP STREAMING OK\n", filename.c_str(), tested);
}

INSTANTIATE_EPUB_TESTS(ZipStreamingTest);

// ===========================================================================
// Level 2: parse_chapter_streaming paragraphs vs parse_chapter
// Verifies that the streaming XHTML→Paragraph pipeline produces the
// same paragraph sequence as the non-streaming path (before MRB encoding).
// ===========================================================================

class ChapterStreamingTest : public ::testing::TestWithParam<std::string> {};

namespace {

// Collect paragraphs from the streaming sink into a vector.
struct CollectCtx {
  std::vector<Paragraph> paragraphs;
};

void collect_sink(void* raw, Paragraph&& para) {
  auto& ctx = *static_cast<CollectCtx*>(raw);
  ctx.paragraphs.push_back(std::move(para));
}

// Compare two paragraphs for equality (ignoring image key remapping).
// Adjacent non-breaking runs with the same style may be split at different
// points due to the 2KB current_run_ flush interacting with different XML
// buffer sizes (full-file for non-streaming vs 16KB for streaming).  Merge
// them before comparing so the test is insensitive to flush boundaries.
static std::vector<Run> merge_runs(const std::vector<Run>& runs) {
  std::vector<Run> merged;
  for (const auto& r : runs) {
    if (!merged.empty() && !merged.back().breaking && merged.back().style == r.style &&
        merged.back().size_pct == r.size_pct && merged.back().vertical_align == r.vertical_align &&
        merged.back().margin_left == r.margin_left && merged.back().margin_right == r.margin_right) {
      merged.back().text += r.text;
      merged.back().breaking = r.breaking;
    } else {
      merged.push_back(r);
    }
  }
  return merged;
}

void assert_paragraph_equal(const Paragraph& a, const Paragraph& b, const std::string& ctx) {
  ASSERT_EQ(a.type, b.type) << ctx << " type";

  if (a.type == ParagraphType::Text) {
    auto ar = merge_runs(a.text.runs);
    auto br = merge_runs(b.text.runs);
    ASSERT_EQ(ar.size(), br.size()) << ctx << " run count (after merge)";
    for (size_t ri = 0; ri < ar.size(); ++ri) {
      std::string rctx = ctx + " run[" + std::to_string(ri) + "]";
      EXPECT_EQ(ar[ri].text, br[ri].text) << rctx << " text";
      EXPECT_EQ(ar[ri].style, br[ri].style) << rctx << " style";
      EXPECT_EQ(ar[ri].size_pct, br[ri].size_pct) << rctx << " size";
      EXPECT_EQ(ar[ri].vertical_align, br[ri].vertical_align) << rctx << " valign";
      EXPECT_EQ(ar[ri].breaking, br[ri].breaking) << rctx << " breaking";
      EXPECT_EQ(ar[ri].margin_left, br[ri].margin_left) << rctx << " margin_left";
      EXPECT_EQ(ar[ri].margin_right, br[ri].margin_right) << rctx << " margin_right";
    }
    EXPECT_EQ(a.text.alignment.has_value(), b.text.alignment.has_value()) << ctx << " alignment presence";
    if (a.text.alignment.has_value() && b.text.alignment.has_value())
      EXPECT_EQ(*a.text.alignment, *b.text.alignment) << ctx << " alignment";
    EXPECT_EQ(a.text.indent.has_value(), b.text.indent.has_value()) << ctx << " indent presence";
    if (a.text.indent.has_value() && b.text.indent.has_value())
      EXPECT_EQ(*a.text.indent, *b.text.indent) << ctx << " indent";
    EXPECT_EQ(a.text.line_height_pct, b.text.line_height_pct) << ctx << " line_height_pct";

    // Inline image: compare dimensions (key may differ if image resolution differed)
    EXPECT_EQ(a.text.inline_image.has_value(), b.text.inline_image.has_value()) << ctx << " inline_image presence";
    if (a.text.inline_image.has_value() && b.text.inline_image.has_value()) {
      EXPECT_EQ(a.text.inline_image->key, b.text.inline_image->key) << ctx << " inline_image key";
      EXPECT_EQ(a.text.inline_image->attr_width, b.text.inline_image->attr_width) << ctx << " inline_image width";
      EXPECT_EQ(a.text.inline_image->attr_height, b.text.inline_image->attr_height) << ctx << " inline_image height";
    }
  } else if (a.type == ParagraphType::Image) {
    EXPECT_EQ(a.image.key, b.image.key) << ctx << " image key";
    EXPECT_EQ(a.image.attr_width, b.image.attr_width) << ctx << " image width";
    EXPECT_EQ(a.image.attr_height, b.image.attr_height) << ctx << " image height";
  }
  // Hr and PageBreak have no additional data.

  EXPECT_EQ(a.spacing_before.has_value(), b.spacing_before.has_value()) << ctx << " spacing_before presence";
  if (a.spacing_before.has_value() && b.spacing_before.has_value())
    EXPECT_EQ(*a.spacing_before, *b.spacing_before) << ctx << " spacing_before";
}

}  // namespace

TEST_P(ChapterStreamingTest, ParagraphsMatch) {
  const std::string& epub_path = GetParam();
  auto filename = fs::path(epub_path).filename().string();

  Book book;
  auto err = book.open(epub_path.c_str());
  if (err != EpubError::Ok)
    GTEST_SKIP() << "Cannot open " << filename;

  static constexpr size_t kWorkBufSize = ZipEntryInput::kDecompSize + ZipEntryInput::kDictSize + 2048;
  static constexpr size_t kXmlBufSize = 16384;
  std::vector<uint8_t> work_buf(kWorkBufSize);
  std::vector<uint8_t> xml_buf(kXmlBufSize);

  size_t total_paras = 0;
  for (size_t ci = 0; ci < book.chapter_count(); ++ci) {
    // Non-streaming path.
    Chapter ch;
    EpubError cerr = book.load_chapter(ci, ch);
    if (cerr != EpubError::Ok)
      continue;

    // Streaming (now includes image resolution + promotion, same as parse_chapter)
    CollectCtx streaming;
    EpubError serr = book.load_chapter_streaming(ci, collect_sink, &streaming, work_buf.data(), xml_buf.data());
    ASSERT_EQ(serr, EpubError::Ok) << filename << " ch" << ci << " streaming parse failed";

    std::string cctx = filename + " ch" + std::to_string(ci);
    ASSERT_EQ(ch.paragraphs.size(), streaming.paragraphs.size())
        << cctx << " paragraph count"
        << " (normal=" << ch.paragraphs.size() << " streaming=" << streaming.paragraphs.size() << ")";

    for (size_t pi = 0; pi < ch.paragraphs.size(); ++pi) {
      std::string pctx = cctx + " p" + std::to_string(pi);
      assert_paragraph_equal(ch.paragraphs[pi], streaming.paragraphs[pi], pctx);
    }

    total_paras += ch.paragraphs.size();
  }

  printf("  [%s] %zu chapters, %zu paragraphs — CHAPTER STREAMING OK\n", filename.c_str(), book.chapter_count(),
         total_paras);
}

INSTANTIATE_EPUB_TESTS(ChapterStreamingTest);
