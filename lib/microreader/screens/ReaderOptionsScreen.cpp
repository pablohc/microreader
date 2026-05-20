#include "ReaderOptionsScreen.h"

#include <cstring>

#include "../Application.h"

namespace microreader {
constexpr uint16_t ReaderSettings::kHPaddingPresets[];
constexpr uint16_t ReaderSettings::kVPaddingPresets[];
constexpr uint16_t ReaderSettings::kSpacingPercents[];
constexpr uint8_t ReaderSettings::kNumAlignPresets;
constexpr uint8_t ReaderSettings::kNumSpacingPresets;
constexpr uint8_t ReaderSettings::kNumFontSizePresets;
constexpr const char* ReaderSettings::kHPaddingNames[];
constexpr const char* ReaderSettings::kVPaddingNames[];
constexpr const char* ReaderSettings::kAlignNames[];
constexpr const char* ReaderSettings::kSpacingNames[];
constexpr const char* ReaderSettings::kFontSizeNames[];

// ---------------------------------------------------------------------------

void ReaderOptionsScreen::populate(const TableOfContents& toc, uint16_t current_chapter, uint16_t current_para,
                                   const std::string& fallback_title, int progress_pct) {
  chapter_title_ = fallback_title;
  int best_match = -1;
  for (size_t i = 0; i < toc.entries.size(); ++i) {
    if (toc.entries[i].file_idx < current_chapter ||
        (toc.entries[i].file_idx == current_chapter && toc.entries[i].para_index <= current_para)) {
      best_match = static_cast<int>(i);
    }
  }
  if (best_match >= 0) {
    chapter_title_ = toc.entries[best_match].label;
  }

  progress_pct_ = progress_pct;
  if (!toc.entries.empty() && app_) {
    app_->chapter_select()->populate(toc, current_chapter, current_para);
    app_->chapter_select()->clear_pending();
  }
}

// Build a "Label: Value" string into a fixed buffer.
static const char* fmt_setting(char* buf, size_t bufsz, const char* label, const char* value) {
  snprintf(buf, bufsz, "%s: %s", label, value);
  return buf;
}

void ReaderOptionsScreen::start(DrawBuffer& buf, IRuntime& runtime) {
  // Capture current selection and Links position before the base class
  // calls on_start(), which rebuilds the list.
  prev_selected_ = selected_index();
  prev_idx_links_ = idx_links_;
  ListMenuScreen::start(buf, runtime);
}

void ReaderOptionsScreen::set_page_links(const std::vector<PageLink>& links,
                                         const std::vector<std::string>& spine_files, const MrbReader& mrb) {
  page_links_ = links;
  // Populate the links screen now while the MrbReader is still open.
  // (ReaderScreen::stop() will close mrb before we get to on_select.)
  if (app_)
    app_->links_screen()->populate(links, spine_files, mrb);
}

void ReaderOptionsScreen::on_start() {
  title_ = "Options";

  subtitle_ = chapter_title_;
  if (subtitle_.length() > 42) {
    subtitle_ = subtitle_.substr(0, 39) + "...";
  }

  char pct_str[32];
  snprintf(pct_str, sizeof(pct_str), "%d%%", progress_pct_);
  subtitle2_ = pct_str;

  clear_items();
  idx_justify_ = idx_padding_h_ = idx_padding_v_ = idx_line_spacing_ = idx_progress_ = idx_chapters_ = idx_pub_fonts_ =
      idx_rotate_display_ = idx_links_ = -1;

  char tmp[40];

  // "Chapters" goes at the top so it's easy to reach.
  if (app_ && app_->chapter_select()->has_toc()) {
    idx_chapters_ = count();
    add_item("Chapters");
  }

  // "Links" on this page — shown only when there are hyperlinks.
  if (!page_links_.empty()) {
    idx_links_ = count();
    char link_label[40];
    snprintf(link_label, sizeof(link_label), "Links (%d)", static_cast<int>(page_links_.size()));
    add_item(link_label);
  }

  // Separator between chapter navigation and text layout settings.
  if (settings_ && app_ && app_->chapter_select()->has_toc())
    add_separator();

  if (settings_) {
    idx_font_size_ = count();
    if (app_ && app_->font_manager() && app_->font_manager()->valid()) {
      auto* fonts = app_->font_manager()->font_set();
      int sz = 0;
      if (fonts && settings_->font_size_idx < fonts->num_fonts()) {
        auto* f = fonts->get_font(settings_->font_size_idx);
        if (f) {
          sz = f->nominal_size();
          if (sz == 0)
            sz = f->y_advance();
        }
      }
      char val[16];
      if (sz > 0)
        snprintf(val, sizeof(val), "%d", sz);
      else
        snprintf(val, sizeof(val), "Unknown");
      add_item(fmt_setting(tmp, sizeof(tmp), "Font Size", val));
    } else {
      add_item(fmt_setting(tmp, sizeof(tmp), "Font Size", ReaderSettings::kFontSizeNames[settings_->font_size_idx]));
    }

    idx_pub_fonts_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "Publisher Sizes", settings_->override_publisher_fonts ? "Off" : "On"));

    idx_line_spacing_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "Line spacing",
                         ReaderSettings::kSpacingNames[static_cast<uint8_t>(settings_->spacing_override)]));

    idx_justify_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "Alignment",
                         ReaderSettings::kAlignNames[static_cast<uint8_t>(settings_->align_override)]));

    add_separator();

    idx_padding_h_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "H-Margin", ReaderSettings::kHPaddingNames[settings_->padding_h_idx]));

    idx_padding_v_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "V-Margin", ReaderSettings::kVPaddingNames[settings_->padding_v_idx]));

    add_separator();

    idx_progress_ = count();
    const char* prog_name = settings_->progress_style == ProgressStyle::None         ? "None"
                            : settings_->progress_style == ProgressStyle::Percentage ? "Percent"
                                                                                     : "Bar";
    add_item(fmt_setting(tmp, sizeof(tmp), "Progress", prog_name));

    idx_rotate_display_ = count();
    add_item(fmt_setting(tmp, sizeof(tmp), "Display", app_ && app_->rotate_display() ? "Landscape" : "Portrait"));
  }

  // Restore selection, adjusting for Links appearing or disappearing.
  int sel = prev_selected_;
  if (prev_idx_links_ == -1 && idx_links_ != -1) {
    // Links appeared: everything at idx_links_ and below shifted down by 1.
    if (sel >= idx_links_)
      sel++;
  } else if (prev_idx_links_ != -1 && idx_links_ == -1) {
    // Links disappeared: cursor was on Links or below it — move up by 1.
    if (sel >= prev_idx_links_)
      sel--;
  }
  int max_sel = count() - 1;
  if (max_sel >= 0) {
    if (sel < 0)
      sel = 0;
    if (sel > max_sel)
      sel = max_sel;
  }
  set_selected(sel);
}

void ReaderOptionsScreen::refresh_items_(int restore_selection) {
  prev_selected_ = restore_selection;
  prev_idx_links_ = idx_links_;
  on_start();  // on_start() applies shift correction and calls set_selected().
}

void ReaderOptionsScreen::on_select(int index) {
  if (!settings_) {
    if (index == idx_chapters_) {
      app_->push_screen(ScreenId::ChapterSelect);
      return;
    }
    return;
  }

  if (index == idx_justify_) {
    settings_->align_override = static_cast<AlignOverride>((static_cast<uint8_t>(settings_->align_override) + 1) %
                                                           ReaderSettings::kNumAlignPresets);
    refresh_items_(index);
    return;
  }
  if (index == idx_pub_fonts_) {
    settings_->override_publisher_fonts = !settings_->override_publisher_fonts;
    refresh_items_(index);
    return;
  }
  if (index == idx_font_size_) {
    uint8_t max_idx = ReaderSettings::kNumFontSizePresets;
    if (app_ && app_->font_manager() && app_->font_manager()->valid()) {
      auto* fonts = app_->font_manager()->font_set();
      if (fonts && fonts->num_fonts() > 0)
        max_idx = static_cast<uint8_t>(fonts->num_fonts());
    }
    settings_->font_size_idx = static_cast<uint8_t>((settings_->font_size_idx + 1) % max_idx);
    refresh_items_(index);
    return;
  }
  if (index == idx_padding_h_) {
    settings_->padding_h_idx = static_cast<uint8_t>((settings_->padding_h_idx + 1) % ReaderSettings::kNumPresets);
    refresh_items_(index);
    return;
  }
  if (index == idx_padding_v_) {
    settings_->padding_v_idx = static_cast<uint8_t>((settings_->padding_v_idx + 1) % ReaderSettings::kNumPresets);
    refresh_items_(index);
    return;
  }
  if (index == idx_line_spacing_) {
    settings_->spacing_override = static_cast<SpacingOverride>((static_cast<uint8_t>(settings_->spacing_override) + 1) %
                                                               ReaderSettings::kNumSpacingPresets);
    refresh_items_(index);
    return;
  }
  if (index == idx_progress_) {
    settings_->progress_style = static_cast<ProgressStyle>((static_cast<uint8_t>(settings_->progress_style) + 1) % 3);
    refresh_items_(index);
    return;
  }
  if (index == idx_rotate_display_) {
    if (app_ && buf_) {
      bool v = !app_->rotate_display();
      app_->set_rotate_display(v);
      buf_->set_rotation(v ? Rotation::Deg0 : Rotation::Deg90);
      refresh_items_(index);
    }
    return;
  }
  if (index == idx_chapters_) {
    app_->push_screen(ScreenId::ChapterSelect);
    return;
  }
  if (index == idx_links_) {
    app_->push_screen(ScreenId::Links);
    return;
  }
  return;
}

}  // namespace microreader
