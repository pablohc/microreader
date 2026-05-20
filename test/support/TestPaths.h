#pragma once

#include <string>

// Resolve the microreader2 repository root from TEST_FIXTURES_DIR.
static inline std::string repo_root() {
  std::string fixtures = TEST_FIXTURES_DIR;
  auto pos = fixtures.rfind('/');
  if (pos == std::string::npos)
    pos = fixtures.rfind('\\');
  std::string up1 = fixtures.substr(0, pos);  // .../microreader2/test
  pos = up1.rfind('/');
  if (pos == std::string::npos)
    pos = up1.rfind('\\');
  return up1.substr(0, pos);  // .../microreader2
}

static inline std::string small_books_dir() {
  return repo_root() + "/test/books/small";
}

static inline std::string books_dir() {
  return repo_root() + "/test/books";
}
