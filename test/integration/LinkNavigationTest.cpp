// LinkNavigationTest.cpp — end-to-end link navigation using the REAL ReaderScreen.
//
// These tests use the actual application code path:
//   1. ReaderScreen::start()   — converts EPUB→MRB, renders first page.
//   2. next_page_and_render()  — navigate until we reach the target chapter/page.
//   3. test_page_links()       — the exact page_links_ produced by collect_page_links_().
//   4. LinksScreen::populate() — same call ReaderOptionsScreen makes on Button1.
//   5. test_select()           — fire on_select(), read pending chapter+para.
//
// No manual reimplementation of ReaderScreen internals.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "ScreenshotDisplay.h"
#include "TestBooks.h"
#include "microreader/Application.h"
#include "microreader/Runtime.h"
#include "microreader/display/DrawBuffer.h"
#include "microreader/screens/LinksScreen.h"
#include "microreader/screens/ReaderScreen.h"

namespace fs = std::filesystem;
using namespace microreader;

// ---------------------------------------------------------------------------
// Minimal IRuntime stub — no sleeping, no battery.
// ---------------------------------------------------------------------------
class MockRuntime : public IRuntime {
 public:
  std::optional<uint8_t> battery_percentage() const override {
    return std::nullopt;
  }
  bool should_continue() const override {
    return true;
  }
  uint32_t frame_time_ms() const override {
    return 30;
  }
  void wait_next_frame() override {}
};

// ---------------------------------------------------------------------------
// Fixture: opens a book via the REAL ReaderScreen and navigates it.
// ---------------------------------------------------------------------------
class ReaderScreenLinkNavTest : public ::testing::Test {
 protected:
  ScreenshotDisplay display_;
  DrawBuffer buf_{display_};
  MockRuntime runtime_;
  Application app_;
  ReaderScreen screen_;
  fs::path data_dir_;

  void SetUp() override {
    screen_.set_app(&app_);
    data_dir_ = fs::temp_directory_path() / "reader_link_nav_test";
    fs::create_directories(data_dir_);
    screen_.set_data_dir(data_dir_.string());
  }

  void TearDown() override {
    screen_.stop();
    fs::remove_all(data_dir_);
  }

  bool open_book(const std::string& path) {
    screen_.set_path(path);
    screen_.start(buf_, runtime_);
    return screen_.is_open_ok();
  }

  // Page forward until current_chapter_index() == target, or give up.
  bool navigate_to_chapter(size_t target) {
    for (int i = 0; i < 50000; ++i) {
      if (screen_.current_chapter_index() == target)
        return true;
      if (screen_.current_chapter_index() > target)
        return false;
      if (!screen_.next_page_and_render(buf_))
        return false;
    }
    return false;
  }

  // Page forward within the current chapter until test_page_links() contains
  // a link whose href includes href_sub. Returns true when found.
  bool find_page_with_link(const std::string& href_sub) {
    for (int i = 0; i < 50000; ++i) {
      for (const auto& lk : screen_.test_page_links())
        if (lk.href.find(href_sub) != std::string::npos)
          return true;
      if (!screen_.next_page_and_render(buf_))
        return false;
    }
    return false;
  }

  struct NavResult {
    bool resolved = false;
    uint16_t chapter = 0xFFFF;
    uint16_t para = 0xFFFF;
  };

  // Populate LinksScreen exactly as ReaderOptionsScreen does, then select the
  // link matching href_sub and return the pending navigation target.
  NavResult select_link(const std::string& href_sub) {
    LinksScreen ls;
    ls.set_app(&app_);
    ls.populate(screen_.test_page_links(), screen_.test_mrb().spine_files(), screen_.test_mrb());

    const auto& links = screen_.test_page_links();
    for (int i = 0; i < static_cast<int>(links.size()); ++i) {
      if (links[i].href.find(href_sub) != std::string::npos) {
        ls.test_select(i);
        return {ls.has_pending(), ls.pending_chapter(), ls.pending_para()};
      }
    }
    return {};
  }
};

// ---------------------------------------------------------------------------
// Alice — "Chapter I" link on the TOC page
// ---------------------------------------------------------------------------
TEST_F(ReaderScreenLinkNavTest, Alice_ChapterI_Link) {
  const std::string root = test_books::workspace_root();
  const std::string alice = root + "/microreader2/sd/books/alice-illustrated.epub";
  if (!fs::exists(alice))
    GTEST_SKIP() << "alice-illustrated.epub not found";

  ASSERT_TRUE(open_book(alice)) << "ReaderScreen failed to open alice-illustrated.epub";
  ASSERT_TRUE(navigate_to_chapter(1)) << "Could not reach chapter 1 (TOC)";
  ASSERT_TRUE(find_page_with_link("h-1.htm.xhtml")) << "No page in chapter 1 contains a link to h-1.htm.xhtml";

  auto nav = select_link("h-1.htm.xhtml");
  ASSERT_TRUE(nav.resolved) << "Chapter I link did not resolve";
  EXPECT_EQ(nav.chapter, 2u) << "h-1.htm.xhtml is spine[2]";
  EXPECT_EQ(nav.para, 27u) << "Page_13 anchor is paragraph 27";
}

// ---------------------------------------------------------------------------
// Alice — "Chapter II" link on the TOC page
// ---------------------------------------------------------------------------
TEST_F(ReaderScreenLinkNavTest, Alice_ChapterII_Link) {
  const std::string root = test_books::workspace_root();
  const std::string alice = root + "/microreader2/sd/books/alice-illustrated.epub";
  if (!fs::exists(alice))
    GTEST_SKIP() << "alice-illustrated.epub not found";

  ASSERT_TRUE(open_book(alice));
  ASSERT_TRUE(navigate_to_chapter(1));
  ASSERT_TRUE(find_page_with_link("h-2.htm.xhtml")) << "No page in chapter 1 contains a link to h-2.htm.xhtml";

  auto nav = select_link("h-2.htm.xhtml");
  ASSERT_TRUE(nav.resolved) << "Chapter II link did not resolve";
  EXPECT_EQ(nav.chapter, 3u) << "h-2.htm.xhtml is spine[3]";
  EXPECT_EQ(nav.para, 32u) << "Page_24 anchor is paragraph 32";
}

// ---------------------------------------------------------------------------
// Ohler — footnote back-link from chapter 13
// "zurück zum Inhalt" → content-5.xhtml#note_1 → chapter 4, para 6
// ---------------------------------------------------------------------------
TEST_F(ReaderScreenLinkNavTest, Ohler_FootnoteBacklink) {
  const std::string root = test_books::workspace_root();
  std::string ohler;
  for (const auto& p : {root + "/microreader2/sd/books/ohler.epub", root + "/microreader2/test/books/ohler.epub"})
    if (fs::exists(p)) {
      ohler = p;
      break;
    }
  if (ohler.empty())
    GTEST_SKIP() << "ohler.epub not found";

  ASSERT_TRUE(open_book(ohler));
  ASSERT_TRUE(navigate_to_chapter(13)) << "Could not reach chapter 13";
  ASSERT_TRUE(find_page_with_link("content-5.xhtml|note_1"))
      << "Chapter 13 must contain a link to content-5.xhtml|note_1";

  auto nav = select_link("content-5.xhtml|note_1");
  ASSERT_TRUE(nav.resolved) << "Footnote back-link did not resolve";
  EXPECT_EQ(nav.chapter, 4u) << "content-5.xhtml is spine[4]";
  EXPECT_EQ(nav.para, 6u) << "note_1 anchor is paragraph 6";
}
