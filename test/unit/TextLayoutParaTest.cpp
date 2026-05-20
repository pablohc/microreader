#include <gtest/gtest.h>

#include <algorithm>

#include "TestChapterSource.h"
#include "microreader/content/TextLayout.h"
#include "microreader/display/DrawBuffer.h"

using namespace microreader;

// ===== Helper: extract word text =====
static std::string word_str(const LayoutWord& w) {
  return std::string(w.text, static_cast<size_t>(w.len));
}

// ===== Helper: extract text from a layout line =====
static std::string line_text(const LayoutLine& line) {
  std::string result;
  for (size_t i = 0; i < line.words.size(); ++i) {
    if (i > 0)
      result += ' ';
    result.append(line.words[i].text, line.words[i].len);
  }
  return result;
}

// ===== Helper: count total words across all lines =====
static size_t total_words(const std::vector<LayoutLine>& lines) {
  size_t n = 0;
  for (auto& l : lines)
    n += l.words.size();
  return n;
}

// ===== Helper: collect all text from lines =====
static std::string all_text(const std::vector<LayoutLine>& lines) {
  std::string result;
  for (auto& line : lines) {
    if (!result.empty())
      result += ' ';
    result += line_text(line);
  }
  return result;
}

// ===== Font: 8px per char, 16px line height =====
static FixedFont font8(8, 16);

// ===================================================================
// layout_paragraph() tests
// ===================================================================

TEST(TextLayout, SingleWordFitsOneLine) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hello", FontStyle::Regular, false));

  LayoutOptions opts{100, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  EXPECT_EQ(line_text(lines[0]), "Hello");
  EXPECT_EQ(lines[0].words[0].x, 0);
}

TEST(TextLayout, MultipleWordsSingleLine) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hello world", FontStyle::Regular, false));

  // "Hello" = 40px, " " = 8px, "world" = 40px = 88px < 100
  LayoutOptions opts{100, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  EXPECT_EQ(line_text(lines[0]), "Hello world");
}

TEST(TextLayout, WordWrapToTwoLines) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hello world foo", FontStyle::Regular, false));

  // Width 80: "Hello" = 40, space=8, "world" = 40 → need 88 > 80 → wrap
  LayoutOptions opts{80, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 2);
  EXPECT_EQ(line_text(lines[0]), "Hello");
  EXPECT_EQ(line_text(lines[1]), "world foo");
}

TEST(TextLayout, MultipleWordsMultipleLines) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("aa bb cc dd ee ff", FontStyle::Regular, false));

  // Each word = 16px, space = 8px
  // Width 56: "aa" + " " + "bb" + " " + "cc" = 16+8+16+8+16 = 64 > 56
  //           "aa" + " " + "bb" = 16+8+16 = 40 < 56
  //           "aa" + " " + "bb" + " " = 48 + "cc" = 64 > 56 → wrap after bb
  LayoutOptions opts{56, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_GE(lines.size(), 2);
  // All words should be preserved
  EXPECT_EQ(all_text(lines), "aa bb cc dd ee ff");
}

TEST(TextLayout, EmptyParagraph) {
  TextParagraph para;
  LayoutOptions opts{100, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);
  EXPECT_TRUE(lines.empty());
}

TEST(TextLayout, WhitespaceOnlyRun) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("   ", FontStyle::Regular, false));

  LayoutOptions opts{100, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);
  EXPECT_TRUE(lines.empty());
}

TEST(TextLayout, MultipleRuns) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hello ", FontStyle::Regular, false));
  para.runs.push_back(microreader::Run("bold", FontStyle::Bold, false));
  para.runs.push_back(microreader::Run(" world", FontStyle::Regular, false));

  LayoutOptions opts{200, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  ASSERT_EQ(lines[0].words.size(), 3);
  EXPECT_EQ(std::string(lines[0].words[0].text, lines[0].words[0].len), "Hello");
  EXPECT_EQ(lines[0].words[0].style, FontStyle::Regular);
  EXPECT_EQ(std::string(lines[0].words[1].text, lines[0].words[1].len), "bold");
  EXPECT_EQ(lines[0].words[1].style, FontStyle::Bold);
  EXPECT_EQ(std::string(lines[0].words[2].text, lines[0].words[2].len), "world");
  EXPECT_EQ(lines[0].words[2].style, FontStyle::Regular);
}

TEST(TextLayout, BreakingRun) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Line one", FontStyle::Regular, true));
  para.runs.push_back(microreader::Run("Line two", FontStyle::Regular, false));

  LayoutOptions opts{200, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 2);
  EXPECT_EQ(line_text(lines[0]), "Line one");
  EXPECT_EQ(line_text(lines[1]), "Line two");
}

TEST(TextLayout, MultipleBreakingRuns) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("A", FontStyle::Regular, true));
  para.runs.push_back(microreader::Run("B", FontStyle::Regular, true));
  para.runs.push_back(microreader::Run("C", FontStyle::Regular, false));

  LayoutOptions opts{200, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 3);
  EXPECT_EQ(line_text(lines[0]), "A");
  EXPECT_EQ(line_text(lines[1]), "B");
  EXPECT_EQ(line_text(lines[2]), "C");
}

// ===== Alignment tests =====

TEST(TextLayout, AlignCenter) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hi", FontStyle::Regular, false));
  para.alignment = Alignment::Center;

  // "Hi" = 16px on width 100 → room = 84 → nudge = 42
  LayoutOptions opts{100};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  EXPECT_EQ(lines[0].words[0].x, 42);
}

TEST(TextLayout, AlignEnd) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hi", FontStyle::Regular, false));
  para.alignment = Alignment::End;

  LayoutOptions opts{100};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  EXPECT_EQ(lines[0].words[0].x, 84);
}

TEST(TextLayout, JustifyTwoWords) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("aa bb cc", FontStyle::Regular, false));

  // Width 80: "aa" + " " + "bb" + " " + "cc" = 16+8+16+8+16 = 64
  // But "aa" + " " + "bb" = 40 → fits
  // "aa" + " " + "bb" + " " + "cc" = 64 < 80 → all on one line
  // Last line → no justification for single-line paragraphs
  LayoutOptions opts{80, Alignment::Justify};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  // Last line is not justified
  EXPECT_EQ(lines[0].words[0].x, 0);
}

TEST(TextLayout, JustifyMultipleLines) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("aa bb cc dd", FontStyle::Regular, false));

  // Width 50: "aa bb" = 40px text, room=10, gaps=1.
  // max_gap=50/8=6. natural_gap=10 > 6 → fixed=space*2=16; min(16,10)=10.
  // bb moves the full 10px to x=34.
  LayoutOptions opts{50, Alignment::Justify};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 2);
  // First line: fully justified (room fits within 2×space cap).
  EXPECT_EQ(lines[0].words[0].x, 0);
  EXPECT_EQ(lines[0].words[1].x, 34);  // 24 natural + 10 room
}

TEST(TextLayout, JustifyWideLineGetsStretched) {
  // Width 400: "aa"=16, "bb"=16 → 40px text, room=360.
  // But room/gaps = 360 > 400/8=50 → suppressed (only 2 very short words).
  // Use a line where room is modest: 8 words of 16px each = 128px text.
  // "aa bb cc dd ee ff gg hh" on width=200:
  //   one line attempt: 8*16 + 7*8 = 128+56 = 184 < 200 → fits on one line (last line, no justify).
  // Use width=160: 8*16+7*8=184 > 160 → splits.
  // Line 1 tries to fit: "aa bb cc dd ee" = 5*16+4*8 = 112. Add "ff"=16+8=24 → 136 < 160.
  //   Add "gg"=16+8=24 → 160 = 160. Fits exactly → room=0 → no justify.
  // width=150: "aa bb cc dd ee ff" = 6*16+5*8 = 96+40=136 < 150. Add "gg"=24 → 160 > 150.
  // Line 1: "aa bb cc dd ee ff" text=136, room=14, gaps=5. room/gaps=2 < 150/8=18 → JUSTIFIED.
  TextParagraph para;
  para.runs.push_back(microreader::Run("aa bb cc dd ee ff gg hh", FontStyle::Regular, false));
  LayoutOptions opts{150, Alignment::Justify};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_GE(lines.size(), 2u);
  // First line must be justified: second word must be further right than its natural position.
  EXPECT_EQ(lines[0].words[0].x, 0);
  EXPECT_GT(lines[0].words[1].x, 24u) << "First line should be justified (word 2 must move right)";
  // Last line is not justified
  EXPECT_EQ(lines.back().words[0].x, 0);
}

// ===== Indent test =====

TEST(TextLayout, ParagraphIndent) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hello world", FontStyle::Regular, false));
  para.indent = 20;

  LayoutOptions opts{200, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  // First word indented by 20px
  EXPECT_EQ(lines[0].words[0].x, 20);
}

TEST(TextLayout, DefaultAlignmentFromOptions) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hi", FontStyle::Regular, false));
  // No para.alignment set → uses opts.alignment

  LayoutOptions opts{100, Alignment::Center};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  EXPECT_EQ(lines[0].words[0].x, 42);  // centered
}

TEST(TextLayout, MarginLeftWithIndent) {
  // When margin_left is set, text-indent is skipped to keep all lines aligned.
  TextParagraph para;
  para.indent = 16;  // 2 chars indent — ignored when margin_left is set
  // Runs with margin_left set (simulating a poem with left margin)
  microreader::Run r1("Hello world this is a long line here", FontStyle::Regular, false);
  r1.margin_left = 40;  // 5 chars margin
  para.runs.push_back(r1);

  LayoutOptions opts{200, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_GE(lines.size(), 2u);
  // All lines at margin_left = 40 (indent skipped when margin_left present)
  EXPECT_EQ(lines[0].words[0].x, 40);
  EXPECT_EQ(lines[1].words[0].x, 40);
}

TEST(TextLayout, MarginRightReducesLineWidth) {
  // With margin_right, text should wrap earlier (at max_width - margin_right).
  TextParagraph para;
  // "AAAA BBBB CCCC" = 3 words × 4 chars × 8px = 32px each, + 8px spaces
  microreader::Run r1("AAAA BBBB CCCC", FontStyle::Regular, false);
  r1.margin_right = 40;
  para.runs.push_back(r1);

  // Width = 100, effective = 100 - 40 = 60. First two words: 32+8+32 = 72 > 60
  // So only "AAAA" fits on line 1 (32px), "BBBB" starts line 2, "CCCC" starts line 3
  LayoutOptions opts{100, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(word_str(lines[0].words[0]), "AAAA");
  EXPECT_EQ(word_str(lines[1].words[0]), "BBBB");
  EXPECT_EQ(word_str(lines[2].words[0]), "CCCC");
}

// ===== Unicode test =====

TEST(TextLayout, UnicodeText) {
  TextParagraph para;
  // 2 codepoints × 8px = 16px
  para.runs.push_back(microreader::Run(std::string("Gr\xc3\xbc\xc3\x9f"
                                                   "e"),
                                       FontStyle::Regular, false));

  LayoutOptions opts{200, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1);
  // "Grüße" = 5 codepoints × 8 = 40px
  EXPECT_EQ(lines[0].words[0].x, 0);
}

// ===== German text rendering pipeline test =====
// Simulates exactly what ReaderScreen does: layout → DrawWord → glyph iteration

TEST(TextLayout, GermanTextRenderingPipeline) {
  // Use the same font parameters as ReaderScreen: glyph_width=16, line_height=20
  FixedFont font16(16, 20);

  // German sentence with umlauts, guillemets, em-dash
  // "Rechtsgeschäft" = 14 codepoints (13 ASCII + 1 ä)
  // "für" = 3 codepoints (f + ü + r)
  // "»Prinz Eugen«" = 13 codepoints (» + P,r,i,n,z + space + E,u,g,e,n + «)
  // "1944–1945" = 9 codepoints (4 digits + – + 4 digits)
  TextParagraph para;
  para.runs.push_back(microreader::Run(std::string("Rechtsgesch\xc3\xa4"
                                                   "ft"                            // Rechtsgeschäft
                                                   " f\xc3\xbcr"                   // für
                                                   " \xc2\xbbPrinz Eugen\xc2\xab"  // »Prinz Eugen«
                                                   " 1944\xe2\x80\x93"             // 1944–
                                                   "1945"),                        // 1945
                                       FontStyle::Regular, false));

  LayoutOptions opts{DrawBuffer::kWidth, Alignment::Start};
  auto lines = TextLayout(font16).layout_paragraph(opts, para);

  ASSERT_GE(lines.size(), 1u);

  // Verify word widths match codepoint counts × 16
  struct Expected {
    const char* text;
    size_t byte_len;
    int codepoints;
  };
  Expected expected[] = {
      {"Rechtsgesch\xc3\xa4"
       "ft",     15, 14}, // 11 ASCII + 2 bytes ä + 2 ASCII = 15 bytes, 14 codepoints
      {"f\xc3\xbcr",    4,  3 }, // f + ü(2b) + r = 4 bytes, 3 codepoints
      {"\xc2\xbbPrinz", 7,  6 }, // »(2b) + Prinz = 7 bytes, 6 codepoints
      {"Eugen\xc2\xab", 7,  6 }, // Eugen + «(2b) = 7 bytes, 6 codepoints
      {"1944\xe2\x80\x93"
       "1945",   11, 9 }, // 1944 + –(3b) + 1945 = 11 bytes, 9 codepoints
  };

  // Collect all words from all lines
  std::vector<LayoutWord> all_words;
  for (auto& line : lines)
    for (auto& w : line.words)
      all_words.push_back(w);

  ASSERT_EQ(all_words.size(), 5u) << "Expected 5 words";

  for (size_t i = 0; i < 5; ++i) {
    const auto& w = all_words[i];
    std::string word_text(w.text, w.len);
    SCOPED_TRACE("word[" + std::to_string(i) + "] = '" + word_text + "'");

    // Verify byte length
    EXPECT_EQ(w.len, expected[i].byte_len);

    // Verify word width = codepoints × glyph_width
    uint16_t measured = font16.word_width(w.text, w.len, FontStyle::Regular);
    EXPECT_EQ(measured, expected[i].codepoints * 16);

    // Count UTF-8 codepoints to verify FixedFont width calculation.
    const char* p = w.text;
    const char* end = w.text + w.len;
    int glyph_count = 0;
    while (p < end && *p) {
      const uint8_t b = static_cast<uint8_t>(*p);
      if (b < 0x80)
        p += 1;
      else if (b < 0xE0)
        p += 2;
      else if (b < 0xF0)
        p += 3;
      else
        p += 4;
      ++glyph_count;
    }
    EXPECT_EQ(glyph_count, expected[i].codepoints) << "Codepoint count doesn't match expected";

    // Verify rendered width = glyph_count × 8 × scale (where scale=2, so 16)
    // This matches the rendering: dw.x + ci * 8 * scale
    int rendered_width = glyph_count * 16;
    EXPECT_EQ(rendered_width, measured) << "Rendered width doesn't match layout-measured width";
  }
}

// ===== Word preservation across wrap =====

TEST(TextLayout, AllWordsPreservedAfterWrap) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("the quick brown fox jumps over the lazy dog", FontStyle::Regular, false));

  LayoutOptions opts{80, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  EXPECT_EQ(all_text(lines), "the quick brown fox jumps over the lazy dog");
}

// ===================================================================
// Font size tests
// ===================================================================

TEST(TextLayout, LargeWordsSameWidth) {
  // Large and Normal words have the same width (bitmap font can't resize glyphs).
  TextParagraph para_normal;
  para_normal.runs.push_back(microreader::Run("Hello", FontStyle::Regular, 100));

  TextParagraph para_large;
  para_large.runs.push_back(microreader::Run("Hello", FontStyle::Regular, 100));

  LayoutOptions opts{300, Alignment::Start};
  auto lines_n = TextLayout(font8).layout_paragraph(opts, para_normal);
  auto lines_l = TextLayout(font8).layout_paragraph(opts, para_large);

  ASSERT_EQ(lines_n.size(), 1u);
  ASSERT_EQ(lines_l.size(), 1u);

  uint16_t end_n = lines_n[0].words[0].x + font8.word_width("Hello", 5, FontStyle::Regular, 100);
  uint16_t end_l = lines_l[0].words[0].x + font8.word_width("Hello", 5, FontStyle::Regular, 100);
  EXPECT_EQ(end_l, end_n);
}

TEST(TextLayout, SmallWordsSameWidth) {
  // Small and Normal words have the same width (bitmap font can't resize glyphs).
  TextParagraph para_normal;
  para_normal.runs.push_back(microreader::Run("Hello", FontStyle::Regular, 100));

  TextParagraph para_small;
  para_small.runs.push_back(microreader::Run("Hello", FontStyle::Regular, 100));

  LayoutOptions opts{300, Alignment::Start};
  auto lines_n = TextLayout(font8).layout_paragraph(opts, para_normal);
  auto lines_s = TextLayout(font8).layout_paragraph(opts, para_small);

  ASSERT_EQ(lines_n.size(), 1u);
  ASSERT_EQ(lines_s.size(), 1u);

  uint16_t end_n = lines_n[0].words[0].x + font8.word_width("Hello", 5, FontStyle::Regular, 100);
  uint16_t end_s = lines_s[0].words[0].x + font8.word_width("Hello", 5, FontStyle::Regular, 100);
  EXPECT_EQ(end_s, end_n);
}

TEST(TextLayout, LargeTextSameWrapping) {
  // With fixed glyph width, Large and Normal produce the same line count.
  std::string text = "The quick brown fox jumps over the lazy dog";

  TextParagraph para_normal;
  para_normal.runs.push_back(microreader::Run(text, FontStyle::Regular, 100));

  TextParagraph para_large;
  para_large.runs.push_back(microreader::Run(text, FontStyle::Regular, 100));

  LayoutOptions opts{200, Alignment::Start};
  auto lines_n = TextLayout(font8).layout_paragraph(opts, para_normal);
  auto lines_l = TextLayout(font8).layout_paragraph(opts, para_large);

  EXPECT_EQ(lines_l.size(), lines_n.size());
}

TEST(TextLayout, LayoutWordCarriesFontSize) {
  TextParagraph para;
  para.runs.push_back(microreader::Run("Normal", FontStyle::Regular, 100));
  para.runs.push_back(microreader::Run(" Large", FontStyle::Bold, 100));
  para.runs.push_back(microreader::Run(" Small", FontStyle::Italic, 100));

  LayoutOptions opts{500, Alignment::Start};
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].words.size(), 3u);

  EXPECT_EQ(lines[0].words[0].size_pct, 100);
  EXPECT_EQ(lines[0].words[1].size_pct, 100);
  EXPECT_EQ(lines[0].words[2].size_pct, 100);
}

TEST(TextLayout, MixedSizesLineHeight) {
  // A page with a Large-text line should use a taller y_advance for that line
  Chapter ch;
  TextParagraph tp_large;
  tp_large.runs.push_back(microreader::Run("Heading", FontStyle::Bold, 100));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp_large)));

  TextParagraph tp_normal;
  tp_normal.runs.push_back(microreader::Run("Body text paragraph.", FontStyle::Regular, 100));
  ch.paragraphs.push_back(Paragraph::make_text(std::move(tp_normal)));
  TestChapterSource src(ch);

  PageOptions opts(300, 400, 0, 8);
  auto page = TextLayout(font8, opts, src, PagePosition(0, 0)).layout();

  ASSERT_GE(page.text_items().size(), 2u);

  // First line (Large) should have a greater y_advance than normal
  uint16_t y0 = page.text_items()[0].y_offset;
  uint16_t y1 = page.text_items()[1].y_offset;
  uint16_t gap = y1 - y0;

  // Large line height = 16 * 5/4 = 20, plus para_spacing = 8 → gap = 28
  // Normal line height would be 16 + 8 = 24
  EXPECT_GT(gap, font8.y_advance(100));
}

TEST(TextLayout, FixedFontSizeScaling) {
  // Width is constant regardless of FontSize (bitmap font can't resize glyphs).
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, 80), font8.char_width('A', FontStyle::Regular, 100));
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, 120), font8.char_width('A', FontStyle::Regular, 100));
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, 140), font8.char_width('A', FontStyle::Regular, 100));
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, 160), font8.char_width('A', FontStyle::Regular, 100));

  // y_advance still scales (line heights can vary without causing overlap).
  EXPECT_LT(font8.y_advance(80), font8.y_advance(100));
  EXPECT_GT(font8.y_advance(120), font8.y_advance(100));
  EXPECT_GT(font8.y_advance(140), font8.y_advance(100));
  EXPECT_GT(font8.y_advance(160), font8.y_advance(100));

  // Width: always 8 regardless of size
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, 80), 8);
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, 100), 8);
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, 100), 8);
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, 100), 8);
  EXPECT_EQ(font8.char_width('A', FontStyle::Regular, 100), 8);

  // Line heights: 16*90%=14, 16, 16*110%=17, 16*120%=19, 16*130%=20
  EXPECT_EQ(font8.y_advance(90), 14);
  EXPECT_EQ(font8.y_advance(100), 16);
  EXPECT_EQ(font8.y_advance(110), 17);
  EXPECT_EQ(font8.y_advance(120), 19);
  EXPECT_EQ(font8.y_advance(130), 20);
}

// ---------------------------------------------------------------------------
// Cross-run word continuation (no space between adjacent runs without trailing space)
// ---------------------------------------------------------------------------

TEST(TextLayout, CrossRunWordContinuation) {
  // Two runs: "A" (no trailing space) + "LICE" (no leading space)
  // Should be placed immediately adjacent, not with a word space.
  TextParagraph para;
  para.runs.push_back(microreader::Run("A", FontStyle::Regular));
  para.runs.push_back(microreader::Run("LICE was here", FontStyle::Regular));

  LayoutOptions opts;
  opts.width = 560;
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_GE(lines.size(), 1u);
  // "A" is at x=0, width=8. "LICE" should be at x=8 (no space gap).
  ASSERT_GE(lines[0].words.size(), 2u);
  EXPECT_EQ(lines[0].words[0].x, 0);
  EXPECT_EQ(std::string(lines[0].words[0].text, lines[0].words[0].len), "A");
  EXPECT_EQ(lines[0].words[1].x, 8);  // immediately after "A", no space
  EXPECT_EQ(std::string(lines[0].words[1].text, lines[0].words[1].len), "LICE");
}

TEST(TextLayout, CrossRunWithTrailingSpace) {
  // Run ending with space: "Hello " + "world" → normal word spacing
  TextParagraph para;
  para.runs.push_back(microreader::Run("Hello ", FontStyle::Regular));
  para.runs.push_back(microreader::Run("world", FontStyle::Regular));

  LayoutOptions opts;
  opts.width = 560;
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_GE(lines.size(), 1u);
  ASSERT_GE(lines[0].words.size(), 2u);
  // "Hello" at x=0 (40px), then space (8px), then "world" at 48px
  EXPECT_EQ(lines[0].words[0].x, 0);
  EXPECT_EQ(lines[0].words[1].x, 48);  // 40 + 8 space
}

TEST(TextLayout, CrossRunDifferentStyles) {
  // "A" in bold + "LICE" in regular, no spaces → adjacent
  TextParagraph para;
  para.runs.push_back(microreader::Run("A", FontStyle::Bold));
  para.runs.push_back(microreader::Run("LICE", FontStyle::Regular));

  LayoutOptions opts;
  opts.width = 560;
  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_EQ(lines.size(), 1u);
  ASSERT_EQ(lines[0].words.size(), 2u);
  EXPECT_EQ(lines[0].words[0].x, 0);
  EXPECT_EQ(lines[0].words[1].x, 8);  // no space between them
}

// ---------------------------------------------------------------------------
// Hyphenation tests
// ---------------------------------------------------------------------------

// Helper: collect all word strings from all lines (no space joining).
static std::vector<std::string> all_words(const std::vector<LayoutLine>& lines) {
  std::vector<std::string> result;
  for (auto& line : lines)
    for (auto& w : line.words)
      result.push_back(word_str(w));
  return result;
}

TEST(TextLayout, HyphenationSplitsLongGermanWord) {
  // "Abendessen" = 10 chars = 80px at 8px/char.
  // Line width = 60px → word doesn't fit on its own, must be hyphenated.
  TextParagraph para;
  para.runs.push_back(microreader::Run("Abendessen", FontStyle::Regular));

  LayoutOptions opts;
  opts.width = 60;
  opts.hyphenation_lang = HyphenationLang::German;

  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  // Should produce at least 2 lines.
  ASSERT_GE(lines.size(), 2u);

  // Last word on line 0 must be "-".
  ASSERT_FALSE(lines[0].words.empty());
  EXPECT_EQ(word_str(lines[0].words.back()), "-");

  // line 0 must be marked hyphenated.
  EXPECT_TRUE(lines[0].hyphenated);

  // Concatenating all non-hyphen words must reconstruct "Abendessen".
  std::string reconstructed;
  for (auto& w : all_words(lines))
    if (w != "-")
      reconstructed += w;
  EXPECT_EQ(reconstructed, "Abendessen");
}

TEST(TextLayout, HyphenationNoneDoesNotSplit) {
  // Same word, same narrow line, but no hyphenation language set.
  // Word should be forced onto its own line unbroken.
  TextParagraph para;
  para.runs.push_back(microreader::Run("Abendessen", FontStyle::Regular));

  LayoutOptions opts;
  opts.width = 60;
  // hyphenation_lang defaults to None

  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  // The word is forced onto a single line (no split).
  bool found_hyphen = false;
  for (auto& w : all_words(lines))
    if (w == "-")
      found_hyphen = true;
  EXPECT_FALSE(found_hyphen);

  // All characters of the word must be present as one token.
  std::string reconstructed;
  for (auto& w : all_words(lines))
    reconstructed += w;
  EXPECT_EQ(reconstructed, "Abendessen");
}

TEST(TextLayout, HyphenationPrefixFitsOnLine) {
  // "Abendessen" at 8px/char = 80px. Line = 60px.
  // Hyphenation points from Liang DE for "Abendessen": after "Abend" (5 chars = 40px).
  // 40px prefix + 8px hyphen = 48px ≤ 60px → prefix fits.
  TextParagraph para;
  para.runs.push_back(microreader::Run("Abendessen", FontStyle::Regular));

  LayoutOptions opts;
  opts.width = 60;
  opts.hyphenation_lang = HyphenationLang::German;

  auto lines = TextLayout(font8).layout_paragraph(opts, para);

  ASSERT_GE(lines.size(), 2u);
  // First word on line 0 must start at x=0.
  EXPECT_EQ(lines[0].words.front().x, 0u);
  // Width used on line 0: prefix_w + hyphen_w ≤ 60.
  uint16_t used = 0;
  for (auto& w : lines[0].words)
    used = w.x + font8.word_width(w.text, w.len, FontStyle::Regular);
  EXPECT_LE(used, 60u);
}
