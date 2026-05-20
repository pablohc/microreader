#pragma once

#include <string>
#include <vector>

namespace microreader {

class DrawBuffer;

struct BookIndexEntry {
  std::string path;
  std::string title;
  std::string author;
  std::string label;
};

class BookIndex {
 public:
  static BookIndex& instance();

  bool load(const std::string& index_file);
  bool save(const std::string& index_file) const;

  // Scan a directory recursively, updating the index
  // Uses DrawBuffer for scratch buffers and for showing loading progress.
  void build_index(const std::string& root_dir, DrawBuffer& buf);

  const std::vector<BookIndexEntry>& entries() const {
    return entries_;
  }

  void clear_entries() {
    entries_.clear();
  }

 private:
  std::vector<BookIndexEntry> entries_;
  BookIndex() = default;
};

}  // namespace microreader
