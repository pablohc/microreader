#include <gtest/gtest.h>

#include "microreader/content/TextLayout.h"

using namespace microreader;

// ===================================================================
// LaidOutParagraph::collect() / collect_backward() unit tests
// ===================================================================

// Build a text LaidOutParagraph with `num_lines` lines of height `line_h`.
static TextLayout::LaidOutParagraph make_text_lp(uint16_t para_idx, int num_lines, uint16_t line_h = 16) {
  TextLayout::LaidOutParagraph lp;
  lp.para_idx = para_idx;
  lp.type = ParagraphType::Text;
  lp.text_runs_empty = false;
  for (int i = 0; i < num_lines; ++i) {
    lp.lines.push_back(LayoutLine{});
    lp.line_heights.push_back(line_h);
    lp.line_baselines.push_back(12);
  }
  return lp;
}

// Build an image LaidOutParagraph with the given block height.
static TextLayout::LaidOutParagraph make_image_lp(uint16_t para_idx, uint16_t block_h) {
  TextLayout::LaidOutParagraph lp;
  lp.para_idx = para_idx;
  lp.type = ParagraphType::Image;
  lp.block_height = block_h;
  lp.img_key = 1;
  lp.img_w = 100;
  lp.img_h = block_h;
  lp.img_x = 0;
  return lp;
}

// Build a text LaidOutParagraph with a promoted inline image followed by text lines.
// idx space: [0..promoted_h) = image pixels, [promoted_h..promoted_h+num_lines) = text lines.
static TextLayout::LaidOutParagraph make_promoted_lp(uint16_t para_idx, uint16_t promoted_h, int num_lines,
                                                     uint16_t line_h = 16) {
  TextLayout::LaidOutParagraph lp;
  lp.para_idx = para_idx;
  lp.type = ParagraphType::Text;
  lp.text_runs_empty = false;
  lp.inline_img.has_image = true;
  lp.inline_img.promoted = true;
  lp.inline_img.key = 42;
  lp.inline_img.width = 100;
  lp.inline_img.height = promoted_h;
  lp.promoted_w = 100;
  lp.promoted_h = promoted_h;
  lp.promoted_x = 0;
  for (int i = 0; i < num_lines; ++i) {
    lp.lines.push_back(LayoutLine{});
    lp.line_heights.push_back(line_h);
    lp.line_baselines.push_back(12);
  }
  return lp;
}

// Build an empty-text LaidOutParagraph (runs were empty â€” emits one Empty item).
static TextLayout::LaidOutParagraph make_empty_text_lp(uint16_t para_idx, uint16_t block_h = 16) {
  TextLayout::LaidOutParagraph lp;
  lp.para_idx = para_idx;
  lp.type = ParagraphType::Text;
  lp.text_runs_empty = true;
  lp.block_height = block_h;
  return lp;
}

// Build an Hr LaidOutParagraph.
static TextLayout::LaidOutParagraph make_hr_lp(uint16_t para_idx, uint16_t block_h = 16) {
  TextLayout::LaidOutParagraph lp;
  lp.para_idx = para_idx;
  lp.type = ParagraphType::Hr;
  lp.block_height = block_h;
  return lp;
}

// Build a PageBreak LaidOutParagraph.
static TextLayout::LaidOutParagraph make_page_break_lp(uint16_t para_idx) {
  TextLayout::LaidOutParagraph lp;
  lp.para_idx = para_idx;
  lp.type = ParagraphType::PageBreak;
  return lp;
}

// ===================================================================
// collect_backward() â€” text paragraphs
// ===================================================================

// collect_backward from end returns the last line.
TEST(CollectBackward, TextLastLine) {
  auto lp = make_text_lp(0, 5, 16);
  // end_idx=5 means "after line 4" -> should return line 4
  auto r = lp.collect_backward(5, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::TextLine);
  EXPECT_EQ(r->item.line_idx, 4);
  EXPECT_EQ(r->item.height, 16);
  EXPECT_EQ(r->next_idx, 4u);
}

// collect_backward from middle returns the correct line.
TEST(CollectBackward, TextMiddleLine) {
  auto lp = make_text_lp(0, 5, 16);
  // end_idx=3 -> should return line 2
  auto r = lp.collect_backward(3, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.line_idx, 2);
  EXPECT_EQ(r->next_idx, 2u);
}

// collect_backward(0) returns nullopt (nothing before line 0).
TEST(CollectBackward, TextAtStart) {
  auto lp = make_text_lp(0, 5, 16);
  auto r = lp.collect_backward(0, 200);
  EXPECT_FALSE(r.has_value());
}

// collect_backward returns nullopt when line doesn't fit.
TEST(CollectBackward, TextLineDoesntFit) {
  auto lp = make_text_lp(0, 5, 16);
  // available=10, line height=16 -> doesn't fit
  auto r = lp.collect_backward(5, 10);
  EXPECT_FALSE(r.has_value());
}

// ===================================================================
// collect_backward() â€” image paragraphs
// ===================================================================

// collect_backward returns a slice ending at end_idx.
TEST(CollectBackward, ImageFullSlice) {
  auto lp = make_image_lp(0, 100);
  // end_idx=100, available=40 -> slice [60..100), height=40
  auto r = lp.collect_backward(100, 40);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::Image);
  EXPECT_EQ(r->item.height, 40);
  EXPECT_EQ(r->item.img_y_crop, 60);
  EXPECT_EQ(r->next_idx, 60u);
}

// collect_backward when available exceeds what remains before end_idx.
TEST(CollectBackward, ImageSliceClampedToStart) {
  auto lp = make_image_lp(0, 100);
  // end_idx=30, available=40 -> only 30px available -> slice [0..30), height=30
  auto r = lp.collect_backward(30, 40);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.height, 30);
  EXPECT_EQ(r->item.img_y_crop, 0);
  EXPECT_EQ(r->next_idx, 0u);
}

// collect_backward(0) returns nullopt.
TEST(CollectBackward, ImageAtStart) {
  auto lp = make_image_lp(0, 100);
  auto r = lp.collect_backward(0, 40);
  EXPECT_FALSE(r.has_value());
}

// ===================================================================
// collect() / collect_backward() â€” Hr paragraphs
// ===================================================================

TEST(CollectBackward, Hr_Forward) {
  auto lp = make_hr_lp(0, 16);
  auto r = lp.collect(0, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::Hr);
  EXPECT_EQ(r->item.height, 16);
  EXPECT_EQ(r->next_idx, 1u);
}

TEST(CollectBackward, Hr_Forward_DoesntFit) {
  auto lp = make_hr_lp(0, 16);
  EXPECT_FALSE(lp.collect(0, 10).has_value());
}

TEST(CollectBackward, Hr_Backward) {
  auto lp = make_hr_lp(0, 16);
  auto r = lp.collect_backward(1, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::Hr);
  EXPECT_EQ(r->item.height, 16);
  EXPECT_EQ(r->next_idx, 0u);
}

TEST(CollectBackward, Hr_Backward_WrongIdx) {
  auto lp = make_hr_lp(0, 16);
  EXPECT_FALSE(lp.collect_backward(0, 200).has_value());
  EXPECT_FALSE(lp.collect_backward(2, 200).has_value());
}

TEST(CollectBackward, Hr_Backward_DoesntFit) {
  auto lp = make_hr_lp(0, 16);
  EXPECT_FALSE(lp.collect_backward(1, 10).has_value());
}

TEST(CollectBackward, Hr_RoundTrip) {
  auto lp = make_hr_lp(0, 16);
  auto fwd = lp.collect(0, 200);
  ASSERT_TRUE(fwd.has_value());
  auto bwd = lp.collect_backward(fwd->next_idx, 200);
  ASSERT_TRUE(bwd.has_value());
  EXPECT_EQ(bwd->item.kind, fwd->item.kind);
  EXPECT_EQ(bwd->item.height, fwd->item.height);
  EXPECT_EQ(bwd->next_idx, 0u);
}

// ===================================================================
// collect() / collect_backward() â€” PageBreak paragraphs
// ===================================================================

TEST(CollectBackward, PageBreak_Forward) {
  auto lp = make_page_break_lp(0);
  auto r = lp.collect(0, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::PageBreak);
  EXPECT_EQ(r->next_idx, 1u);
}

TEST(CollectBackward, PageBreak_Forward_WrongIdx) {
  auto lp = make_page_break_lp(0);
  EXPECT_FALSE(lp.collect(1, 200).has_value());
}

TEST(CollectBackward, PageBreak_Backward) {
  auto lp = make_page_break_lp(0);
  auto r = lp.collect_backward(1, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::PageBreak);
  EXPECT_EQ(r->next_idx, 0u);
}

TEST(CollectBackward, PageBreak_Backward_WrongIdx) {
  auto lp = make_page_break_lp(0);
  EXPECT_FALSE(lp.collect_backward(0, 200).has_value());
  EXPECT_FALSE(lp.collect_backward(2, 200).has_value());
}

TEST(CollectBackward, PageBreak_RoundTrip) {
  auto lp = make_page_break_lp(0);
  auto fwd = lp.collect(0, 200);
  ASSERT_TRUE(fwd.has_value());
  auto bwd = lp.collect_backward(fwd->next_idx, 200);
  ASSERT_TRUE(bwd.has_value());
  EXPECT_EQ(bwd->next_idx, 0u);
}

// ===================================================================
// collect() / collect_backward() round-trip
// ===================================================================

// collect() forward then collect_backward() from next_idx gives the same line.
TEST(CollectBackward, ForwardThenBackwardRoundTrip) {
  auto lp = make_text_lp(0, 5, 16);

  // Forward: collect line 0
  auto fwd = lp.collect(0, 200);
  ASSERT_TRUE(fwd.has_value());
  EXPECT_EQ(fwd->item.kind, TextLayout::PageItem::TextLine);
  EXPECT_EQ(fwd->item.line_idx, 0);
  EXPECT_EQ(fwd->item.height, 16);
  EXPECT_EQ(fwd->next_idx, 1u);

  // Backward from next_idx=1 should give the same line back
  auto bwd = lp.collect_backward(fwd->next_idx, 200);
  ASSERT_TRUE(bwd.has_value());
  EXPECT_EQ(bwd->item.kind, TextLayout::PageItem::TextLine);
  EXPECT_EQ(bwd->item.line_idx, fwd->item.line_idx);
  EXPECT_EQ(bwd->item.height, fwd->item.height);
  EXPECT_EQ(bwd->next_idx, 0u);
}

// ===================================================================
// collect_text â€” empty-text paragraphs (forward and backward)
// ===================================================================

TEST(CollectText, EmptyText_Forward) {
  auto lp = make_empty_text_lp(0, 16);
  auto r = lp.collect(0, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::Empty);
  EXPECT_EQ(r->item.height, 16);
  EXPECT_EQ(r->next_idx, 1u);
}

TEST(CollectText, EmptyText_Forward_WrongIdx) {
  auto lp = make_empty_text_lp(0, 16);
  EXPECT_FALSE(lp.collect(1, 200).has_value());
}

TEST(CollectText, EmptyText_Backward) {
  auto lp = make_empty_text_lp(0, 16);
  auto r = lp.collect_backward(1, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::Empty);
  EXPECT_EQ(r->item.height, 16);
  EXPECT_EQ(r->next_idx, 0u);
}

TEST(CollectText, EmptyText_Backward_InvalidEnd) {
  auto lp = make_empty_text_lp(0, 16);
  EXPECT_FALSE(lp.collect_backward(2, 200).has_value());
}

TEST(CollectText, EmptyText_RoundTrip) {
  auto lp = make_empty_text_lp(0, 16);
  auto fwd = lp.collect(0, 200);
  ASSERT_TRUE(fwd.has_value());
  auto bwd = lp.collect_backward(fwd->next_idx, 200);
  ASSERT_TRUE(bwd.has_value());
  EXPECT_EQ(bwd->item.kind, fwd->item.kind);
  EXPECT_EQ(bwd->next_idx, 0u);
}

// ===================================================================
// collect_text â€” promoted inline image (forward and backward)
// ===================================================================
// make_promoted_lp(0, 60, 3, 16): idx [0..60) = image, [60..63) = 3 text lines

TEST(CollectText, Promoted_Forward_FullImage) {
  auto lp = make_promoted_lp(0, 60, 3);
  // Available > promoted_h â†’ single full-image slice
  auto r = lp.collect(0, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::Image);
  EXPECT_EQ(r->item.height, 60);
  EXPECT_EQ(r->item.img_y_crop, 0);
  EXPECT_EQ(r->next_idx, 60u);
}

TEST(CollectText, Promoted_Forward_PartialSlice) {
  auto lp = make_promoted_lp(0, 60, 3);
  // Available=40 â†’ slice [0..40)
  auto r = lp.collect(0, 40);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.height, 40);
  EXPECT_EQ(r->item.img_y_crop, 0);
  EXPECT_EQ(r->next_idx, 40u);
}

TEST(CollectText, Promoted_Forward_ContinueSlice) {
  auto lp = make_promoted_lp(0, 60, 3);
  // Available=200 from mid-image â†’ remainder [40..60)
  auto r = lp.collect(40, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.height, 20);
  EXPECT_EQ(r->item.img_y_crop, 40);
  EXPECT_EQ(r->next_idx, 60u);
}

TEST(CollectText, Promoted_Forward_TextLineAfterImage) {
  auto lp = make_promoted_lp(0, 60, 3);
  // idx=60 â†’ first text line (line_idx=0)
  auto r = lp.collect(60, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::TextLine);
  EXPECT_EQ(r->item.line_idx, 0);
  EXPECT_EQ(r->item.height, 16);
  EXPECT_EQ(r->next_idx, 61u);
}

TEST(CollectText, Promoted_Backward_FullImage) {
  auto lp = make_promoted_lp(0, 60, 3);
  // Backward from end of image region, large available â†’ whole image
  auto r = lp.collect_backward(60, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::Image);
  EXPECT_EQ(r->item.height, 60);
  EXPECT_EQ(r->item.img_y_crop, 0);
  EXPECT_EQ(r->next_idx, 0u);
}

TEST(CollectText, Promoted_Backward_PartialSlice) {
  auto lp = make_promoted_lp(0, 60, 3);
  // Backward from 60 with available=40 â†’ slice [20..60)
  auto r = lp.collect_backward(60, 40);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.height, 40);
  EXPECT_EQ(r->item.img_y_crop, 20);
  EXPECT_EQ(r->next_idx, 20u);
}

TEST(CollectText, Promoted_Backward_ClampedToStart) {
  auto lp = make_promoted_lp(0, 60, 3);
  // Backward from 30 with available=200 â†’ slice [0..30) (clamped to start)
  auto r = lp.collect_backward(30, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.height, 30);
  EXPECT_EQ(r->item.img_y_crop, 0);
  EXPECT_EQ(r->next_idx, 0u);
}

TEST(CollectText, Promoted_Backward_TextLineAtBoundary) {
  auto lp = make_promoted_lp(0, 60, 3);
  // Backward from 61 (end of text line 0) â†’ text line 0
  auto r = lp.collect_backward(61, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::TextLine);
  EXPECT_EQ(r->item.line_idx, 0);
  EXPECT_EQ(r->next_idx, 60u);
}

TEST(CollectText, Promoted_RoundTrip_ImageSlice) {
  auto lp = make_promoted_lp(0, 60, 3);
  // Forward from 10 with available=30 â†’ slice [10..40). Then backward from 40 with same available.
  auto fwd = lp.collect(10, 30);
  ASSERT_TRUE(fwd.has_value());
  EXPECT_EQ(fwd->item.img_y_crop, 10);
  EXPECT_EQ(fwd->item.height, 30);
  EXPECT_EQ(fwd->next_idx, 40u);

  auto bwd = lp.collect_backward(fwd->next_idx, 30);
  ASSERT_TRUE(bwd.has_value());
  EXPECT_EQ(bwd->item.img_y_crop, fwd->item.img_y_crop);
  EXPECT_EQ(bwd->item.height, fwd->item.height);
  EXPECT_EQ(bwd->next_idx, 10u);
}

TEST(CollectText, Promoted_RoundTrip_ImageTextBoundary) {
  auto lp = make_promoted_lp(0, 60, 3);
  // Forward from 60 (text line 0), then backward from next_idx.
  auto fwd = lp.collect(60, 200);
  ASSERT_TRUE(fwd.has_value());
  EXPECT_EQ(fwd->item.kind, TextLayout::PageItem::TextLine);

  auto bwd = lp.collect_backward(fwd->next_idx, 200);
  ASSERT_TRUE(bwd.has_value());
  EXPECT_EQ(bwd->item.kind, TextLayout::PageItem::TextLine);
  EXPECT_EQ(bwd->item.line_idx, fwd->item.line_idx);
  EXPECT_EQ(bwd->next_idx, 60u);
}

// ===================================================================
// collect_text â€” inline_extra on line 0 (non-promoted inline image)
// ===================================================================

TEST(CollectText, InlineExtra_AffectsLine0Height) {
  // inline_extra bumps the height and baseline of line 0 only.
  auto lp = make_text_lp(0, 3, 16);
  lp.inline_extra = 8;  // line 0 becomes height=24, baseline=20

  auto r0 = lp.collect(0, 200);
  ASSERT_TRUE(r0.has_value());
  EXPECT_EQ(r0->item.height, 24);
  EXPECT_EQ(r0->item.baseline, 20);
  EXPECT_EQ(r0->next_idx, 1u);

  // Line 1 is unaffected.
  auto r1 = lp.collect(1, 200);
  ASSERT_TRUE(r1.has_value());
  EXPECT_EQ(r1->item.height, 16);
}

TEST(CollectText, InlineExtra_Backward_Line0) {
  auto lp = make_text_lp(0, 3, 16);
  lp.inline_extra = 8;

  // Backward to line 0: available must accommodate the taller line.
  auto r = lp.collect_backward(1, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.height, 24);
  EXPECT_EQ(r->next_idx, 0u);

  // Backward with available=19 < baseline=20 â†’ line 0 doesn't fit.
  EXPECT_FALSE(lp.collect_backward(1, 19).has_value());
  // available=20 == baseline=20 â†’ now accepted (baseline-fit rule).
  EXPECT_TRUE(lp.collect_backward(1, 20).has_value());
}

// ===================================================================
// Multi-item round-trip: collect all forward, then all backward,
// verify same items in reverse order.
// ===================================================================

// Helper: collect all items from a paragraph forward starting at idx=0.
static std::vector<TextLayout::PageItem> collect_all_forward(const TextLayout::LaidOutParagraph& lp,
                                                             uint16_t available = 9999) {
  std::vector<TextLayout::PageItem> items;
  size_t idx = 0;
  while (true) {
    auto r = lp.collect(idx, available);
    if (!r)
      break;
    idx = r->next_idx;
    items.push_back(std::move(r->item));
  }
  return items;
}

// Helper: collect all items from a paragraph backward starting from end_idx.
static std::vector<TextLayout::PageItem> collect_all_backward(const TextLayout::LaidOutParagraph& lp, size_t end_idx,
                                                              uint16_t available = 9999) {
  std::vector<TextLayout::PageItem> items;
  while (end_idx > 0) {
    auto r = lp.collect_backward(end_idx, available);
    if (!r)
      break;
    end_idx = r->next_idx;
    items.push_back(std::move(r->item));
  }
  return items;  // in reverse order relative to forward
}

TEST(MultiItemRoundTrip, TextParagraph) {
  auto lp = make_text_lp(0, 5, 16);
  auto fwd = collect_all_forward(lp);
  ASSERT_EQ(fwd.size(), 5u);

  // End cursor = next_idx after the last forward item = 5
  auto bwd = collect_all_backward(lp, 5);
  ASSERT_EQ(bwd.size(), 5u);

  // Forward and backward should yield the same items in opposite order.
  for (size_t i = 0; i < fwd.size(); ++i) {
    const auto& f = fwd[i];
    const auto& b = bwd[fwd.size() - 1 - i];
    EXPECT_EQ(f.kind, b.kind);
    EXPECT_EQ(f.line_idx, b.line_idx);
    EXPECT_EQ(f.height, b.height);
  }
}

TEST(MultiItemRoundTrip, ImageParagraph) {
  auto lp = make_image_lp(0, 100);
  // No slicing: available > block_height â†’ single item.
  auto fwd = collect_all_forward(lp);
  ASSERT_EQ(fwd.size(), 1u);
  EXPECT_EQ(fwd[0].img_y_crop, 0);
  EXPECT_EQ(fwd[0].height, 100);

  auto bwd = collect_all_backward(lp, 100);
  ASSERT_EQ(bwd.size(), 1u);
  EXPECT_EQ(bwd[0].img_y_crop, fwd[0].img_y_crop);
  EXPECT_EQ(bwd[0].height, fwd[0].height);
}

TEST(MultiItemRoundTrip, ImageParagraph_Sliced_CursorConsistency) {
  // Sliced image: for each forward item, collect_backward(next_cursor, item.height)
  // must reproduce the exact same item and return the prev cursor.
  auto lp = make_image_lp(0, 100);
  size_t idx = 0;
  size_t prev_cursor = 0;
  while (true) {
    auto f = lp.collect(idx, 40);
    if (!f)
      break;
    size_t next_cursor = f->next_idx;

    // Backward with exactly item.height as budget â†’ same item.
    auto b = lp.collect_backward(next_cursor, f->item.height);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->item.img_y_crop, f->item.img_y_crop);
    EXPECT_EQ(b->item.height, f->item.height);
    EXPECT_EQ(b->next_idx, prev_cursor);

    prev_cursor = next_cursor;
    idx = next_cursor;
  }
}

TEST(MultiItemRoundTrip, PromotedParagraph) {
  // promoted_h=60, 3 text lines of height 16 â†’ 4 items: image + 3 lines
  auto lp = make_promoted_lp(0, 60, 3);
  auto fwd = collect_all_forward(lp);
  ASSERT_EQ(fwd.size(), 4u);
  EXPECT_EQ(fwd[0].kind, TextLayout::PageItem::Image);
  EXPECT_EQ(fwd[0].height, 60);
  EXPECT_EQ(fwd[1].kind, TextLayout::PageItem::TextLine);
  EXPECT_EQ(fwd[1].line_idx, 0);
  EXPECT_EQ(fwd[2].kind, TextLayout::PageItem::TextLine);
  EXPECT_EQ(fwd[2].line_idx, 1);
  EXPECT_EQ(fwd[3].kind, TextLayout::PageItem::TextLine);
  EXPECT_EQ(fwd[3].line_idx, 2);

  auto bwd = collect_all_backward(lp, 63);  // 60 image + 3 lines
  ASSERT_EQ(bwd.size(), 4u);

  for (size_t i = 0; i < fwd.size(); ++i) {
    const auto& f = fwd[i];
    const auto& b = bwd[fwd.size() - 1 - i];
    EXPECT_EQ(f.kind, b.kind);
    EXPECT_EQ(f.height, b.height);
    if (f.kind == TextLayout::PageItem::TextLine)
      EXPECT_EQ(f.line_idx, b.line_idx);
    if (f.kind == TextLayout::PageItem::Image)
      EXPECT_EQ(f.img_y_crop, b.img_y_crop);
  }
}

TEST(MultiItemRoundTrip, PromotedParagraph_SlicedImage) {
  // For each forward item (text line or image slice), collect_backward(next_cursor,
  // item.height) must reproduce the exact same item and return the prev cursor.
  auto lp = make_promoted_lp(0, 60, 3);  // image [0..60), 3 lines of h=16
  size_t idx = 0;
  size_t prev_cursor = 0;
  while (true) {
    auto f = lp.collect(idx, 25);
    if (!f)
      break;
    size_t next_cursor = f->next_idx;

    auto b = lp.collect_backward(next_cursor, f->item.height);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->item.kind, f->item.kind);
    EXPECT_EQ(b->item.height, f->item.height);
    if (f->item.kind == TextLayout::PageItem::TextLine)
      EXPECT_EQ(b->item.line_idx, f->item.line_idx);
    if (f->item.kind == TextLayout::PageItem::Image)
      EXPECT_EQ(b->item.img_y_crop, f->item.img_y_crop);
    EXPECT_EQ(b->next_idx, prev_cursor);

    prev_cursor = next_cursor;
    idx = next_cursor;
  }
}

// ===================================================================
// leading_spacer â€” collect() and collect_backward()
// ===================================================================

// idx 0 returns the Spacer item; real lines start at idx 1.
TEST(LeadingSpacer, Forward_SpacerAtIdx0) {
  auto lp = make_text_lp(0, 3, 16);
  lp.leading_spacer = 20;

  auto r = lp.collect(0, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::Spacer);
  EXPECT_EQ(r->item.height, 20);
  EXPECT_EQ(r->next_idx, 1u);
}

// idx 1 returns the first real line (line_idx 0).
TEST(LeadingSpacer, Forward_FirstRealLine) {
  auto lp = make_text_lp(0, 3, 16);
  lp.leading_spacer = 20;

  auto r = lp.collect(1, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::TextLine);
  EXPECT_EQ(r->item.line_idx, 0);
  EXPECT_EQ(r->next_idx, 2u);
}

// idx 3 returns line_idx 2 (last line of 3-line para), next_idx == 4.
TEST(LeadingSpacer, Forward_LastLine) {
  auto lp = make_text_lp(0, 3, 16);
  lp.leading_spacer = 20;

  auto r = lp.collect(3, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::TextLine);
  EXPECT_EQ(r->item.line_idx, 2);
  EXPECT_EQ(r->next_idx, 4u);
}

// idx 4 (past end) returns nullopt.
TEST(LeadingSpacer, Forward_PastEnd) {
  auto lp = make_text_lp(0, 3, 16);
  lp.leading_spacer = 20;

  EXPECT_FALSE(lp.collect(4, 200).has_value());
}

// Spacer doesn't fit â†’ nullopt.
TEST(LeadingSpacer, Forward_SpacerDoesntFit) {
  auto lp = make_text_lp(0, 3, 16);
  lp.leading_spacer = 50;

  EXPECT_FALSE(lp.collect(0, 30).has_value());
}

// collect_backward(end_idx=1) returns the Spacer (item ending at 1, starting at 0).
TEST(LeadingSpacer, Backward_SpacerAtEnd1) {
  auto lp = make_text_lp(0, 3, 16);
  lp.leading_spacer = 20;

  auto r = lp.collect_backward(1, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::Spacer);
  EXPECT_EQ(r->item.height, 20);
  EXPECT_EQ(r->next_idx, 0u);
}

// collect_backward(2) returns line_idx 0 (real line ending at external idx 2).
TEST(LeadingSpacer, Backward_FirstRealLine) {
  auto lp = make_text_lp(0, 3, 16);
  lp.leading_spacer = 20;

  auto r = lp.collect_backward(2, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::TextLine);
  EXPECT_EQ(r->item.line_idx, 0);
  EXPECT_EQ(r->next_idx, 1u);
}

// collect_backward(4) returns line_idx 2 (last line).
TEST(LeadingSpacer, Backward_LastLine) {
  auto lp = make_text_lp(0, 3, 16);
  lp.leading_spacer = 20;

  auto r = lp.collect_backward(4, 200);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::TextLine);
  EXPECT_EQ(r->item.line_idx, 2);
  EXPECT_EQ(r->next_idx, 3u);
}

// Spacer backward doesn't fit â†’ nullopt.
TEST(LeadingSpacer, Backward_SpacerDoesntFit) {
  auto lp = make_text_lp(0, 3, 16);
  lp.leading_spacer = 50;

  EXPECT_FALSE(lp.collect_backward(1, 30).has_value());
}

// Full round-trip: iterate forward through all items, then verify collect_backward from each next_idx.
TEST(LeadingSpacer, RoundTrip_AllItems) {
  auto lp = make_text_lp(0, 3, 16);
  lp.leading_spacer = 20;

  size_t idx = 0;
  size_t prev_cursor = 0;
  int count = 0;
  while (true) {
    auto f = lp.collect(idx, 200);
    if (!f)
      break;
    ++count;
    size_t next_cursor = f->next_idx;

    auto b = lp.collect_backward(next_cursor, 200);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->item.kind, f->item.kind);
    EXPECT_EQ(b->item.height, f->item.height);
    if (f->item.kind == TextLayout::PageItem::TextLine)
      EXPECT_EQ(b->item.line_idx, f->item.line_idx);
    EXPECT_EQ(b->next_idx, prev_cursor);

    prev_cursor = next_cursor;
    idx = next_cursor;
  }
  // Spacer + 3 lines = 4 items, final idx = 4.
  EXPECT_EQ(count, 4);
  EXPECT_EQ(idx, 4u);
}

// ===================================================================
// collect_text â€” baseline-fit acceptance (lh > available but bl <= available)
// ===================================================================

// Helper: build a text LP with custom baseline (different from default 12).
static TextLayout::LaidOutParagraph make_text_lp_bl(uint16_t para_idx, int num_lines, uint16_t line_h,
                                                    uint16_t baseline) {
  TextLayout::LaidOutParagraph lp;
  lp.para_idx = para_idx;
  lp.type = ParagraphType::Text;
  lp.text_runs_empty = false;
  for (int i = 0; i < num_lines; ++i) {
    lp.lines.push_back(LayoutLine{});
    lp.line_heights.push_back(line_h);
    lp.line_baselines.push_back(baseline);
  }
  return lp;
}

// Forward: line accepted when baseline fits but full height does not.
TEST(CollectText, BaselineFit_Forward_Accepts) {
  // lh=20, bl=16. available=18: bl(16) <= 18 < lh(20) â†’ accept.
  auto lp = make_text_lp_bl(0, 3, 20, 16);
  auto r = lp.collect(0, 18);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->item.kind, TextLayout::PageItem::TextLine);
  // Height is always lh; the extended backward budget (ph+desc) ensures symmetry.
  EXPECT_EQ(r->item.height, 20);  // lh, not bl
  EXPECT_EQ(r->item.baseline, 16);
  EXPECT_EQ(r->next_idx, 1u);
}

// Forward: line rejected when even baseline doesn't fit.
TEST(CollectText, BaselineFit_Forward_Rejects) {
  // lh=20, bl=16. available=15 < bl(16) â†’ reject.
  auto lp = make_text_lp_bl(0, 3, 20, 16);
  EXPECT_FALSE(lp.collect(0, 15).has_value());
}

// Forward: line accepted when baseline exactly equals available.
TEST(CollectText, BaselineFit_Forward_ExactBaseline) {
  auto lp = make_text_lp_bl(0, 1, 20, 16);
  EXPECT_TRUE(lp.collect(0, 16).has_value());
}

// Backward: same baseline-fit rule applies.
TEST(CollectText, BaselineFit_Backward_Accepts) {
  auto lp = make_text_lp_bl(0, 3, 20, 16);
  // end_idx=1 â†’ line 0. available=18: bl(16) <= 18 â†’ accept.
  auto r = lp.collect_backward(1, 18);
  ASSERT_TRUE(r.has_value());
  // Height is always lh; the extended backward budget (ph+desc) ensures symmetry.
  EXPECT_EQ(r->item.height, 20);  // lh, not bl
  EXPECT_EQ(r->item.baseline, 16);
  EXPECT_EQ(r->next_idx, 0u);
}

// Backward: line rejected when baseline doesn't fit.
TEST(CollectText, BaselineFit_Backward_Rejects) {
  auto lp = make_text_lp_bl(0, 3, 20, 16);
  EXPECT_FALSE(lp.collect_backward(1, 15).has_value());
}

// Round-trip: collect forward with baseline-fit available, then backward from next_idx.
TEST(CollectText, BaselineFit_RoundTrip) {
  // 3-line paragraph, lh=20, bl=16.
  // available=18 accepts each line individually.
  auto lp = make_text_lp_bl(0, 3, 20, 16);

  // Forward line 0 with tight available=18.
  auto fwd = lp.collect(0, 18);
  ASSERT_TRUE(fwd.has_value());
  EXPECT_EQ(fwd->next_idx, 1u);

  // Backward from next_idx=1 with same available â†’ same line.
  auto bwd = lp.collect_backward(fwd->next_idx, 18);
  ASSERT_TRUE(bwd.has_value());
  EXPECT_EQ(bwd->item.line_idx, fwd->item.line_idx);
  EXPECT_EQ(bwd->item.height, fwd->item.height);
  EXPECT_EQ(bwd->next_idx, 0u);
}

// leading_spacer on an Hr paragraph.
TEST(LeadingSpacer, Hr_Forward_SpacerThenHr) {
  auto lp = make_hr_lp(0, 16);
  lp.leading_spacer = 10;

  auto r0 = lp.collect(0, 200);
  ASSERT_TRUE(r0.has_value());
  EXPECT_EQ(r0->item.kind, TextLayout::PageItem::Spacer);
  EXPECT_EQ(r0->next_idx, 1u);

  auto r1 = lp.collect(1, 200);
  ASSERT_TRUE(r1.has_value());
  EXPECT_EQ(r1->item.kind, TextLayout::PageItem::Hr);
  EXPECT_EQ(r1->next_idx, 2u);
}

TEST(LeadingSpacer, Hr_Backward_RoundTrip) {
  auto lp = make_hr_lp(0, 16);
  lp.leading_spacer = 10;

  auto bwd2 = lp.collect_backward(2, 200);
  ASSERT_TRUE(bwd2.has_value());
  EXPECT_EQ(bwd2->item.kind, TextLayout::PageItem::Hr);
  EXPECT_EQ(bwd2->next_idx, 1u);

  auto bwd1 = lp.collect_backward(1, 200);
  ASSERT_TRUE(bwd1.has_value());
  EXPECT_EQ(bwd1->item.kind, TextLayout::PageItem::Spacer);
  EXPECT_EQ(bwd1->next_idx, 0u);
}
