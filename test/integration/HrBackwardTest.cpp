// Reproduces a bug where horizontal-rule (HR) items at or near the end of a
// chapter are not included on the last page produced by `layout_backward`
// when navigating backward into the chapter from the following chapter.
//
// Scenario on real hardware: ReaderScreen::prev_page_ at page_pos_ == {0,0}
// loads the previous chapter, calls
//
//     layout_engine_.set_position(PagePosition{paragraph_count(), 0});
//     pc = layout_engine_.layout_backward();
//     page_pos_ = pc.start;
//
// and then renders by calling forward layout from `page_pos_`. The HR present
// on the original forward last page must still show up on the page produced
// by this round-trip.

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>

#include "TestPaths.h"
#include "microreader/content/Book.h"
#include "microreader/content/TextLayout.h"
#include "microreader/content/mrb/MrbConverter.h"
#include "microreader/content/mrb/MrbReader.h"
#include "microreader/display/DrawBuffer.h"
#include "microreader/screens/ReaderScreen.h"

using namespace microreader;
namespace fs = std::filesystem;

namespace {

struct ItemSig {
  int kind;  // 0=text 1=image 2=hr
  uint16_t para;
  uint16_t line_or_key;
  bool operator==(const ItemSig& o) const {
    return kind == o.kind && para == o.para && line_or_key == o.line_or_key;
  }
};

std::vector<ItemSig> page_signature(const PageContent& pc) {
  std::vector<ItemSig> out;
  for (const auto& t : pc.text_items())
    out.push_back({0, t.paragraph_index, t.line_index});
  for (const auto& i : pc.image_items())
    out.push_back({1, i.paragraph_index, i.key});
  for (size_t i = 0; i < pc.hr_items().size(); ++i)
    out.push_back({2, 0, 0});
  return out;
}

size_t count_hrs(const PageContent& pc) {
  return pc.hr_items().size();
}

}  // namespace

// ---------------------------------------------------------------------------
// HR-at-chapter-end backward round trip.
//
// The contract being checked here is the one ReaderScreen relies on:
//   page_pos_ = layout_backward().start;
//   render uses layout() (forward) from page_pos_.
//
// So if layout_backward includes an HR, then layout() from the same start
// must also include the same HR.  Forward and backward pages do NOT need to
// match the chapter's forward "last page" — backward fills from the bottom
// up, forward fills from the top down, so the two halves of the chapter's
// final page can legitimately differ.
// ---------------------------------------------------------------------------
TEST(HrBackwardTest, AliceIllustrated_HrPreservedFromNextChapter) {
  fs::path epub_path = fs::path(small_books_dir()) / "alice-illustrated.epub";
  ASSERT_TRUE(fs::exists(epub_path)) << "alice-illustrated.epub missing";

  Book book;
  ASSERT_EQ(book.open(epub_path.string().c_str()), EpubError::Ok);

  auto mrb_path = (fs::temp_directory_path() / "alice_hrback.mrb").string();
  ASSERT_TRUE(convert_epub_to_mrb_streaming(book, mrb_path.c_str()));
  book.close();

  MrbReader mrb;
  ASSERT_TRUE(mrb.open(mrb_path.c_str()));

  auto font = ReaderScreen::make_fixed_font();
  auto opts = ReaderScreen::make_page_opts();
  auto size_fn = make_image_size_query(mrb, epub_path.string(), opts.width);

  int chapters_checked = 0;
  int failures = 0;

  for (uint16_t ci = 0; ci < mrb.chapter_count(); ++ci) {
    MrbChapterSource src(mrb, ci);
    TextLayout tl(font, opts, src, size_fn);

    // Backward from end-of-chapter, exactly like prev_page_ does.
    const uint16_t end_para = static_cast<uint16_t>(src.paragraph_count());
    if (end_para == 0)
      continue;
    tl.set_position(PagePosition{end_para, 0});
    auto back = tl.layout_backward();

    if (count_hrs(back) == 0)
      continue;
    ++chapters_checked;

    // ReaderScreen renders by forward layout from pc.start. The page that
    // gets shown must contain the same HR(s) that the backward layout said
    // would be on this page.
    tl.set_position(back.start);
    auto fwd_again = tl.layout();
    if (count_hrs(fwd_again) != count_hrs(back)) {
      ++failures;
      ADD_FAILURE() << "ch=" << ci << " back.start=" << back.start.paragraph << "/" << back.start.offset
                    << "  layout_backward HRs=" << count_hrs(back)
                    << "  forward-from-back.start HRs=" << count_hrs(fwd_again)
                    << "  (forward end=" << fwd_again.end.paragraph << "/" << fwd_again.end.offset << ")";
      if (ci == 3) {
        std::cout << "ch=3 paragraph_count=" << src.paragraph_count() << '\n';
        std::cout << "back items:\n";
        for (const auto& it : back.items) {
          if (auto* t = std::get_if<PageTextItem>(&it))
            std::cout << "  TEXT  para=" << t->paragraph_index << " line=" << t->line_index << " y=" << t->y_offset
                      << " h=" << t->height << '\n';
          else if (auto* im = std::get_if<PageImageItem>(&it))
            std::cout << "  IMG   para=" << im->paragraph_index << " y=" << im->y_offset << " h=" << im->height << '\n';
          else if (auto* h = std::get_if<PageHrItem>(&it))
            std::cout << "  HR    y=" << h->y_offset << " h=" << h->height << '\n';
        }
        std::cout << "back.start=" << back.start.paragraph << "/" << back.start.offset
                  << "  back.end=" << back.end.paragraph << "/" << back.end.offset << '\n';
        std::cout << "fwd_again items:\n";
        for (const auto& it : fwd_again.items) {
          if (auto* t = std::get_if<PageTextItem>(&it))
            std::cout << "  TEXT  para=" << t->paragraph_index << " line=" << t->line_index << " y=" << t->y_offset
                      << " h=" << t->height << '\n';
          else if (auto* im = std::get_if<PageImageItem>(&it))
            std::cout << "  IMG   para=" << im->paragraph_index << " y=" << im->y_offset << " h=" << im->height << '\n';
          else if (auto* h = std::get_if<PageHrItem>(&it))
            std::cout << "  HR    y=" << h->y_offset << " h=" << h->height << '\n';
        }
        std::cout << "fwd_again.start=" << fwd_again.start.paragraph << "/" << fwd_again.start.offset
                  << "  fwd_again.end=" << fwd_again.end.paragraph << "/" << fwd_again.end.offset << '\n';
        std::cout << "page height = " << opts.height << " padding top=" << opts.padding_top
                  << " bottom=" << opts.padding_bottom << '\n';
        for (size_t pi = back.start.paragraph; pi < src.paragraph_count(); ++pi) {
          const auto& p = src.paragraph(pi);
          const char* kind = "?";
          switch (p.type) {
            case ParagraphType::Text:
              kind = "Text";
              break;
            case ParagraphType::Image:
              kind = "Image";
              break;
            case ParagraphType::Hr:
              kind = "Hr";
              break;
            case ParagraphType::PageBreak:
              kind = "PageBreak";
              break;
          }
          std::cout << "  para " << pi << " type=" << kind
                    << " sp_before=" << (p.spacing_before.has_value() ? (int)*p.spacing_before : -1) << '\n';
        }
      }
    }
  }

  mrb.close();
  std::remove(mrb_path.c_str());

  std::cout << "Chapters checked: " << chapters_checked << ", failures: " << failures << '\n';
  ASSERT_GT(chapters_checked, 0) << "expected some chapters to have HR on the backward last page";
}

// ---------------------------------------------------------------------------
// Synthetic minimal repro: chapter that ends in an HR.  Multi-page so that
// the HR ends up on the chapter's last page along with some preceding text.
// ---------------------------------------------------------------------------
#include "TestChapterSource.h"

namespace {
microreader::FixedFont synth_font(8, 16);
}

TEST(HrBackwardTest, Synthetic_HrAtChapterEnd_RoundTrip) {
  Chapter ch;
  for (int i = 0; i < 6; ++i) {
    TextParagraph tp;
    tp.runs.push_back(microreader::Run(std::string("paragraph") + std::to_string(i), FontStyle::Regular, false));
    ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  }
  ch.paragraphs.push_back(Paragraph::make_hr());
  TestChapterSource src(ch);

  PageOptions opts(200, 80, 0, 0);
  TextLayout tl(synth_font, opts, src);

  // Simulate cross-chapter backward navigation.
  const uint16_t pcnt = static_cast<uint16_t>(src.paragraph_count());
  tl.set_position(PagePosition{pcnt, 0});
  auto back = tl.layout_backward();
  ASSERT_EQ(count_hrs(back), 1u) << "layout_backward from end of chapter must include HR";

  // ReaderScreen renders by forward-from-start: that must include the same HR.
  tl.set_position(back.start);
  auto fwd_again = tl.layout();
  EXPECT_EQ(count_hrs(fwd_again), count_hrs(back)) << "forward from back.start must preserve HR count";
  EXPECT_EQ(page_signature(fwd_again), page_signature(back))
      << "forward-from-back.start must reproduce the backward page exactly";
}

// HR is the only paragraph remaining at the chapter end position
// (worst case: HR followed by nothing).
TEST(HrBackwardTest, Synthetic_HrOnlyAtEnd) {
  Chapter ch;
  TextParagraph tp;
  tp.runs.push_back(microreader::Run("hello world", FontStyle::Regular, false));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp)));
  ch.paragraphs.push_back(Paragraph::make_hr());
  TestChapterSource src(ch);

  PageOptions opts(200, 200, 0, 0);
  TextLayout tl(synth_font, opts, src);
  auto fwd = tl.layout();
  ASSERT_TRUE(fwd.at_chapter_end);
  ASSERT_EQ(count_hrs(fwd), 1u);

  tl.set_position(PagePosition{static_cast<uint16_t>(src.paragraph_count()), 0});
  auto back = tl.layout_backward();
  EXPECT_EQ(count_hrs(back), 1u);
  EXPECT_EQ(page_signature(back), page_signature(fwd));
}
