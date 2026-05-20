// LinksScreenTest.cpp — tests for LinksScreen::populate() chapter/fragment resolution.
//
// Tests verify that:
//   1. A PageLink href is resolved to the correct chapter index via spine_files.
//   2. An anchor fragment is resolved to the correct paragraph index via find_anchor().
//   3. Unresolvable hrefs produce chapter_idx == 0xFFFF (shown as "?").
//   4. Fragment-only hrefs (empty path part) produce no chapter match.
//
// These tests require microreader_core (LinksScreen) and microreader_content (MrbWriter/Reader).

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "microreader/Application.h"
#include "microreader/content/mrb/MrbReader.h"
#include "microreader/content/mrb/MrbWriter.h"
#include "microreader/screens/LinksScreen.h"
#include "microreader/screens/ReaderOptionsScreen.h"  // for PageLink

namespace fs = std::filesystem;
using namespace microreader;

// ---------------------------------------------------------------------------
// Fixture: creates a temp MRB file with configurable spine/anchors.
// ---------------------------------------------------------------------------
class LinksScreenTest : public ::testing::Test {
 protected:
  std::string tmp_path_;
  MrbReader mrb_;

  void SetUp() override {
    tmp_path_ = (fs::temp_directory_path() / "links_screen_test.mrb").string();
  }

  void TearDown() override {
    mrb_.close();
    std::remove(tmp_path_.c_str());
  }

  // Build a minimal MRB with given spine files and anchors, then open it.
  struct AnchorDef {
    uint16_t chapter_idx;
    uint16_t para_idx;
    std::string id;
  };

  bool build_mrb(const std::vector<std::string>& spine_files, const std::vector<AnchorDef>& anchors = {}) {
    MrbWriter writer;
    if (!writer.open(tmp_path_.c_str()))
      return false;

    // One chapter per spine file, each with enough paragraphs to hold anchors.
    uint16_t max_para = 0;
    for (const auto& a : anchors)
      if (a.para_idx > max_para)
        max_para = a.para_idx;

    for (size_t i = 0; i < spine_files.size(); ++i) {
      writer.begin_chapter();
      // Write enough paragraphs so para_idx references are in range.
      uint16_t n = max_para + 1;
      if (n < 1)
        n = 1;
      for (uint16_t p = 0; p < n; ++p) {
        TextParagraph tp;
        tp.runs.push_back(microreader::Run("para", FontStyle::Regular));
        if (!writer.write_paragraph(Paragraph::make_text(std::move(tp))))
          return false;
      }
      writer.end_chapter();
    }

    for (const auto& a : anchors)
      writer.add_anchor(a.chapter_idx, a.para_idx, a.id.c_str(), a.id.size());

    EpubMetadata meta;
    meta.title = "Test";
    TableOfContents toc;
    if (!writer.finish(meta, toc, spine_files))
      return false;

    return mrb_.open(tmp_path_.c_str());
  }

  // Run populate() + on_select(index) and return pending chapter/para.
  // Returns false if the link was unresolvable (chapter_idx == 0xFFFF).
  struct SelectResult {
    bool resolved = false;
    uint16_t chapter_idx = 0xFFFF;
    uint16_t para_idx = 0;
  };

  SelectResult select_link(const std::vector<PageLink>& links, int select_index) {
    Application app;  // pop_screen() is deferred, safe to call with empty stack.
    LinksScreen screen;
    screen.set_app(&app);
    screen.populate(links, mrb_.spine_files(), mrb_);

    // Call on_select — uses app.pop_screen() which is a no-op (empty stack).
    screen.test_select(select_index);

    SelectResult r;
    r.resolved = screen.has_pending();
    r.chapter_idx = screen.pending_chapter();
    r.para_idx = screen.pending_para();
    return r;
  }
};

// ---------------------------------------------------------------------------
// Chapter index resolution (spine_files basename matching)
// ---------------------------------------------------------------------------

TEST_F(LinksScreenTest, ChapterResolution_BasicMatch) {
  // spine[0]=cover, spine[1]=ch1, spine[2]=ch2
  ASSERT_TRUE(build_mrb({"cover.xhtml", "ch1.xhtml", "ch2.xhtml"}));

  PageLink link;
  link.label = "Chapter 1";
  link.href = "OEBPS/Text/ch1.xhtml|";  // no fragment

  auto r = select_link({link}, 0);
  ASSERT_TRUE(r.resolved);
  EXPECT_EQ(r.chapter_idx, 1u) << "ch1.xhtml is spine[1]";
  EXPECT_EQ(r.para_idx, 0u);
}

TEST_F(LinksScreenTest, ChapterResolution_AliceStructure) {
  // Mirrors the real alice-illustrated.epub spine (first 5 items).
  std::vector<std::string> spine = {
      "wrap0000.xhtml",
      "502862557236502936_28885-h-0.htm.xhtml",  // spine[1]: TOC/frontmatter
      "502862557236502936_28885-h-1.htm.xhtml",  // spine[2]: Chapter I
      "502862557236502936_28885-h-2.htm.xhtml",  // spine[3]: Chapter II
      "502862557236502936_28885-h-3.htm.xhtml",  // spine[4]: Chapter III
  };
  ASSERT_TRUE(build_mrb(spine));

  // The TOC chapter (spine[1]) links to chapters 2, 3, 4 with fragment #Page_N.
  // These are the actual href values stored by EpubParser after resolve_path().
  std::vector<PageLink> links = {
      {"CONTENTS (self)", "OEBPS/502862557236502936_28885-h-0.htm.xhtml|Page_1" },
      {"Chapter I",       "OEBPS/502862557236502936_28885-h-1.htm.xhtml|Page_13"},
      {"Chapter II",      "OEBPS/502862557236502936_28885-h-2.htm.xhtml|Page_24"},
      {"Chapter III",     "OEBPS/502862557236502936_28885-h-3.htm.xhtml|Page_35"},
  };

  {
    auto r = select_link(links, 0);
    ASSERT_TRUE(r.resolved);
    EXPECT_EQ(r.chapter_idx, 1u) << "Self-link to h-0 should be spine[1]";
  }
  {
    auto r = select_link(links, 1);
    ASSERT_TRUE(r.resolved);
    EXPECT_EQ(r.chapter_idx, 2u) << "Chapter I link (h-1) should be spine[2], not spine[1]";
  }
  {
    auto r = select_link(links, 2);
    ASSERT_TRUE(r.resolved);
    EXPECT_EQ(r.chapter_idx, 3u) << "Chapter II link (h-2) should be spine[3]";
  }
  {
    auto r = select_link(links, 3);
    ASSERT_TRUE(r.resolved);
    EXPECT_EQ(r.chapter_idx, 4u) << "Chapter III link (h-3) should be spine[4]";
  }
}

TEST_F(LinksScreenTest, ChapterResolution_UnresolvableHref) {
  ASSERT_TRUE(build_mrb({"cover.xhtml", "ch1.xhtml"}));

  PageLink link;
  link.label = "External";
  link.href = "https://example.com|";  // external URL, no basename match

  auto r = select_link({link}, 0);
  EXPECT_FALSE(r.resolved) << "External URL should not resolve to any chapter";
}

TEST_F(LinksScreenTest, ChapterResolution_FragmentOnlyHref) {
  // Fragment-only href "OEBPS/Text/|anchor" (empty basename after last slash).
  ASSERT_TRUE(build_mrb({"cover.xhtml", "ch1.xhtml"}));

  PageLink link;
  link.label = "Same-page anchor";
  link.href = "OEBPS/Text/|some_anchor";  // empty basename

  auto r = select_link({link}, 0);
  EXPECT_FALSE(r.resolved) << "Fragment-only hrefs have empty basename and should not match";
}

TEST_F(LinksScreenTest, ChapterResolution_NoPath_JustBasename) {
  ASSERT_TRUE(build_mrb({"ch1.xhtml", "ch2.xhtml"}));

  PageLink link;
  link.label = "Ch2";
  link.href = "ch2.xhtml|";  // no directory prefix

  auto r = select_link({link}, 0);
  ASSERT_TRUE(r.resolved);
  EXPECT_EQ(r.chapter_idx, 1u);
}

// ---------------------------------------------------------------------------
// Fragment (anchor) → paragraph index resolution
// ---------------------------------------------------------------------------

TEST_F(LinksScreenTest, FragmentResolution_BasicAnchor) {
  std::vector<std::string> spine = {"cover.xhtml", "ch1.xhtml"};
  std::vector<LinksScreenTest::AnchorDef> anchors = {
      {1, 5, "section_a"}, // chapter 1, paragraph 5, id "section_a"
  };
  ASSERT_TRUE(build_mrb(spine, anchors));

  PageLink link;
  link.label = "Section A";
  link.href = "OEBPS/ch1.xhtml|section_a";

  auto r = select_link({link}, 0);
  ASSERT_TRUE(r.resolved);
  EXPECT_EQ(r.chapter_idx, 1u);
  EXPECT_EQ(r.para_idx, 5u) << "Fragment 'section_a' should resolve to para 5";
}

TEST_F(LinksScreenTest, FragmentResolution_AnchorNotFound_DefaultsToZero) {
  std::vector<std::string> spine = {"cover.xhtml", "ch1.xhtml"};
  ASSERT_TRUE(build_mrb(spine, {}));  // no anchors stored

  PageLink link;
  link.label = "Section";
  link.href = "OEBPS/ch1.xhtml|missing_id";

  auto r = select_link({link}, 0);
  ASSERT_TRUE(r.resolved);
  EXPECT_EQ(r.chapter_idx, 1u);
  EXPECT_EQ(r.para_idx, 0u) << "Unresolved fragment should default to para 0";
}

TEST_F(LinksScreenTest, FragmentResolution_EmptyFragment_DefaultsToZero) {
  std::vector<std::string> spine = {"cover.xhtml", "ch1.xhtml"};
  ASSERT_TRUE(build_mrb(spine, {}));

  PageLink link;
  link.label = "Start";
  link.href = "OEBPS/ch1.xhtml|";  // empty fragment

  auto r = select_link({link}, 0);
  ASSERT_TRUE(r.resolved);
  EXPECT_EQ(r.chapter_idx, 1u);
  EXPECT_EQ(r.para_idx, 0u);
}

TEST_F(LinksScreenTest, FragmentResolution_MultipleAnchors) {
  std::vector<std::string> spine = {"cover.xhtml", "ch1.xhtml", "ch2.xhtml"};
  std::vector<LinksScreenTest::AnchorDef> anchors = {
      {1, 3,  "intro" },
      {1, 10, "part2" },
      {2, 0,  "begin" },
      {2, 7,  "midway"},
  };
  ASSERT_TRUE(build_mrb(spine, anchors));

  struct Case {
    std::string href;
    uint16_t expected_ch;
    uint16_t expected_para;
  };
  std::vector<Case> cases = {
      {"OEBPS/ch1.xhtml|intro",  1, 3 },
      {"OEBPS/ch1.xhtml|part2",  1, 10},
      {"OEBPS/ch2.xhtml|begin",  2, 0 },
      {"OEBPS/ch2.xhtml|midway", 2, 7 },
  };

  for (const auto& c : cases) {
    PageLink link;
    link.label = "test";
    link.href = c.href;

    auto r = select_link({link}, 0);
    ASSERT_TRUE(r.resolved) << "href=" << c.href;
    EXPECT_EQ(r.chapter_idx, c.expected_ch) << "href=" << c.href;
    EXPECT_EQ(r.para_idx, c.expected_para) << "href=" << c.href;
  }
}

TEST_F(LinksScreenTest, FragmentResolution_WrongChapter_NotFound) {
  // Anchor exists in ch1 but href points to ch2 — should NOT resolve fragment.
  std::vector<std::string> spine = {"cover.xhtml", "ch1.xhtml", "ch2.xhtml"};
  std::vector<LinksScreenTest::AnchorDef> anchors = {
      {1, 5, "anchor_id"}, // only in chapter 1
  };
  ASSERT_TRUE(build_mrb(spine, anchors));

  PageLink link;
  link.label = "wrong chapter";
  link.href = "OEBPS/ch2.xhtml|anchor_id";  // ch2 has no such anchor

  auto r = select_link({link}, 0);
  ASSERT_TRUE(r.resolved);
  EXPECT_EQ(r.chapter_idx, 2u);
  EXPECT_EQ(r.para_idx, 0u) << "Anchor from ch1 should not match when href points to ch2";
}

// ---------------------------------------------------------------------------
// Multiple links on one page — only the selected one is returned
// ---------------------------------------------------------------------------

TEST_F(LinksScreenTest, MultipleLinks_SelectCorrectOne) {
  std::vector<std::string> spine = {"cover.xhtml", "ch1.xhtml", "ch2.xhtml", "ch3.xhtml"};
  std::vector<LinksScreenTest::AnchorDef> anchors = {
      {1, 2, "a1"},
      {2, 7, "a2"},
      {3, 1, "a3"},
  };
  ASSERT_TRUE(build_mrb(spine, anchors));

  std::vector<PageLink> links = {
      {"Ch1", "OEBPS/ch1.xhtml|a1"},
      {"Ch2", "OEBPS/ch2.xhtml|a2"},
      {"Ch3", "OEBPS/ch3.xhtml|a3"},
  };

  Application app;
  LinksScreen screen;
  screen.set_app(&app);
  screen.populate(links, mrb_.spine_files(), mrb_);

  // Select second link (index 1)
  screen.test_select(1);
  ASSERT_TRUE(screen.has_pending());
  EXPECT_EQ(screen.pending_chapter(), 2u);
  EXPECT_EQ(screen.pending_para(), 7u);

  screen.clear_pending();

  // Select third link (index 2)
  screen.test_select(2);
  ASSERT_TRUE(screen.has_pending());
  EXPECT_EQ(screen.pending_chapter(), 3u);
  EXPECT_EQ(screen.pending_para(), 1u);
}
