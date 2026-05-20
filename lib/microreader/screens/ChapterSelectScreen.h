#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "../content/ContentModel.h"
#include "ListMenuScreen.h"

namespace microreader {

// Chapter selection screen — lists TOC entries from an MRB file.
// Built on top of ListMenuScreen for consistent UI and scrolling.
// Button3/Button2 = navigate up/down, Button1 = jump to chapter, Button0 = cancel.
class ChapterSelectScreen final : public ListMenuScreen {
 public:
  ChapterSelectScreen() = default;

  // Populate the list from a TableOfContents. Call before pushing this screen.
  // current_chapter/current_para select the closest TOC entry to the reading position.
  void populate(const TableOfContents& toc, uint16_t current_chapter = 0, uint16_t current_para = 0);

  const char* name() const override {
    return "Chapters";
  }

  // Returns true if the user selected a chapter (vs. pressing back).
  bool has_pending() const {
    return has_pending_;
  }
  // Returns true if this screen was populated with a non-empty TOC.
  bool has_toc() const {
    return !entries_.empty();
  }
  // The chapter index to jump to (valid only when has_pending() == true).
  uint16_t pending_chapter() const {
    return pending_chapter_;
  }
  // The paragraph index within that chapter (0 if no specific anchor).
  uint16_t pending_para_index() const {
    return pending_para_index_;
  }
  void clear_pending() {
    has_pending_ = false;
  }

 protected:
  void on_start() override;
  void on_select(int index) override;

 private:
  struct Entry {
    std::string label;
    uint16_t chapter_idx;
    uint16_t para_index;
    uint8_t depth;
  };
  std::vector<Entry> entries_;
  int initial_selected_ = 0;

  uint16_t pending_chapter_ = 0;
  uint16_t pending_para_index_ = 0;
  bool has_pending_ = false;
};

}  // namespace microreader
