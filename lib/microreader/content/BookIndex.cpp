#include "BookIndex.h"

#include <cstdio>
#include <cstring>
#include <queue>
#include <vector>

#include "../display/DrawBuffer.h"
#include "Book.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace microreader {

BookIndex& BookIndex::instance() {
  static BookIndex instance;
  return instance;
}

bool BookIndex::load(const std::string& index_file) {
  FILE* f = std::fopen(index_file.c_str(), "rb");
  if (!f)
    return false;

  entries_.clear();
  char line[1024];
  while (std::fgets(line, sizeof(line), f)) {
    // Remove newline
    size_t len = std::strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
      len--;
    }
    if (len > 0 && line[len - 1] == '\r') {
      line[len - 1] = '\0';
      len--;
    }

    // Format: path|title|author
    char* sep1 = std::strchr(line, '|');
    if (!sep1)
      continue;
    *sep1 = '\0';
    char* sep2 = std::strchr(sep1 + 1, '|');
    if (!sep2)
      continue;
    *sep2 = '\0';

    BookIndexEntry entry;
    entry.path = line;
    entry.title = sep1 + 1;
    entry.author = sep2 + 1;

    if (!entry.title.empty()) {
      entry.label = entry.title;
      if (!entry.author.empty()) {
        entry.label += " - ";
        entry.label += entry.author;
      }
    } else {
      // Fallback label to filename
      const char* name = entry.path.c_str();
      const char* sep = std::strrchr(name, '/');
#ifdef _WIN32
      const char* bsep = std::strrchr(name, '\\');
      if (bsep && (!sep || bsep > sep))
        sep = bsep;
#endif
      if (sep)
        name = sep + 1;
      const char* dot = std::strrchr(name, '.');
      size_t name_len = dot ? static_cast<size_t>(dot - name) : std::strlen(name);
      entry.label = std::string(name, name_len);
    }
    entries_.push_back(std::move(entry));
  }
  std::fclose(f);
  return true;
}

bool BookIndex::save(const std::string& index_file) const {
  FILE* f = std::fopen(index_file.c_str(), "wb");
  if (!f)
    return false;

  for (const auto& entry : entries_) {
    std::fprintf(f, "%s|%s|%s\n", entry.path.c_str(), entry.title.c_str(), entry.author.c_str());
  }
  std::fclose(f);
  return true;
}

void BookIndex::build_index(const std::string& root_dir, DrawBuffer& buf) {
  entries_.clear();
  buf.show_loading("Scanning...", 0);
  std::vector<std::string> epub_files;

#ifdef ESP_PLATFORM
  std::queue<std::string> q;
  q.push(root_dir);

  while (!q.empty()) {
    std::string current_dir = q.front();
    q.pop();

    DIR* dir = opendir(current_dir.c_str());
    if (!dir)
      continue;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
      if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0)
        continue;

      std::string fullpath = current_dir + "/" + ent->d_name;

      if (ent->d_type == DT_DIR) {
        q.push(fullpath);
      } else {
        size_t len = std::strlen(ent->d_name);
        if (len > 5 && std::strcmp(ent->d_name + len - 5, ".epub") == 0) {
          epub_files.push_back(fullpath);
        }
      }
    }
    closedir(dir);
  }
#else
  try {
    for (const auto& entry : fs::recursive_directory_iterator(root_dir)) {
      if (!entry.is_regular_file())
        continue;
      auto ext = entry.path().extension().string();
      // Handle uppercase as well if needed, but standard is .epub
      if (ext == ".epub" || ext == ".EPUB") {
        std::string path_str = entry.path().string();
        // normalize slashes for index stability across platforms
        for (char& c : path_str) {
          if (c == '\\')
            c = '/';
        }
        epub_files.push_back(path_str);
      }
    }
  } catch (...) {}
#endif

  int total = static_cast<int>(epub_files.size());
  int done = 0;

  for (const auto& path : epub_files) {
    buf.show_loading("Indexing...", total > 0 ? 10 + (done * 90 / total) : 10);

    Book book;
    if (book.open(path.c_str(), buf.scratch_buf1(), buf.scratch_buf2(), false) == EpubError::Ok) {
      BookIndexEntry entry;
      entry.path = path;
      auto meta = book.metadata();
      entry.title = meta.title;
      if (meta.author) {
        entry.author = *meta.author;
      }

      if (!entry.title.empty()) {
        entry.label = entry.title;
        if (!entry.author.empty()) {
          entry.label += " - ";
          entry.label += entry.author;
        }
      } else {
        const char* name = path.c_str();
        const char* sep = std::strrchr(path.c_str(), '/');
        if (sep)
          name = sep + 1;
        const char* dot = std::strrchr(name, '.');
        size_t name_len = dot ? static_cast<size_t>(dot - name) : std::strlen(name);
        entry.label = std::string(name, name_len);
      }
      entries_.push_back(std::move(entry));
    }
    book.close();
    done++;
  }

  // Refresh loading to 100% just before exit
  if (total > 0) {
    buf.show_loading("Indexing...", 100);
  }
}

}  // namespace microreader
