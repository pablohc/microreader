#include "MainMenu.h"

#include <cstdio>
#include <cstring>

#include "../Application.h"
#include "../HeapLog.h"
#include "../content/BookIndex.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace microreader {

void MainMenu::on_start() {
  title_ = "Microreader";

  if (!app_->data_dir_) {
    needs_scan_ = false;
    return;
  }

  std::string index_path = std::string(app_->data_dir_) + "/book_index.dat";

  if (BookIndex::instance().load(index_path)) {
    populate_list_();
    needs_scan_ = false;
  } else {
    // We defer heavy scanning to update() so we don't trip hardware watchdog.
    needs_scan_ = true;
  }
}

void MainMenu::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) {
  if (needs_scan_) {
    needs_scan_ = false;
    scan_directory_(buf);
    populate_list_();

    // Force a redraw and full refresh since the list contents completely changed
    draw_all_(buf, runtime.battery_percentage());
    buf.full_refresh();
  }

  ListMenuScreen::update(buttons, buf, runtime);
}

void MainMenu::on_select(int index) {
  last_selected_path_ = entries_[index].path;
  app_->reader()->set_path(entries_[index].path.c_str());
  app_->push_screen(ScreenId::Reader);
}

void MainMenu::on_back() {
  app_->push_screen(ScreenId::Settings);
}

void MainMenu::scan_directory_(DrawBuffer& buf) {
  if (!books_dir_ || !app_->data_dir_)
    return;

  std::string root_dir = books_dir_;
  const std::string index_path = std::string(app_->data_dir_) + "/book_index.dat";

  buf.sync_bw_ram();

  BookIndex::instance().build_index(root_dir, buf);
  BookIndex::instance().save(index_path);

  // Refresh to clean up the loading bar
  buf.reset_after_scratch(true);
}

void MainMenu::populate_list_() {
  clear_items();
  entries_.clear();

  for (const auto& index_entry : BookIndex::instance().entries()) {
    BookEntry e;
    e.path = index_entry.path;

    if (list_format_ == BookListFormat::TitleOnly) {
      e.label = index_entry.title.empty() ? index_entry.label : index_entry.title;
    } else if (list_format_ == BookListFormat::Filename) {
      const char* name = index_entry.path.c_str();
      const char* sep = std::strrchr(name, '/');
#ifdef _WIN32
      const char* bsep = std::strrchr(name, '\\');
      if (bsep && (!sep || bsep > sep))
        sep = bsep;
#endif
      if (sep)
        name = sep + 1;

      const char* dot = std::strrchr(name, '.');
      if (dot) {
        e.label = std::string(name, dot - name);
      } else {
        e.label = name;
      }
    } else {
      e.label = index_entry.label;  // Title & Author
    }

    entries_.push_back(std::move(e));
    add_item(entries_.back().label);
  }

  // Restore previously selected book position — only on first visit.
  if (!initial_selection_.empty()) {
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
      if (entries_[i].path == initial_selection_) {
        set_selected(i);
        break;
      }
    }
    initial_selection_.clear();
  }
}

}  // namespace microreader
