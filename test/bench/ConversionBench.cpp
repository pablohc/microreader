#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "../support/TestBooks.h"
#include "microreader/content/Book.h"
#include "microreader/content/ContentModel.h"
#include "microreader/content/ImageDecoder.h"
#include "microreader/content/ZipReader.h"
#include "microreader/content/mrb/MrbConverter.h"

namespace fs = std::filesystem;
using namespace microreader;

// Cache the book list so discovery only happens once.
// Benchmarks use ALL books (not the curated 15).
static const std::vector<std::string>& all_books() {
  static auto books = test_books::get_all_books();
  return books;
}

// Find a book by partial filename match.
static const std::string* find_book(const char* needle) {
  for (const auto& path : all_books()) {
    if (fs::path(path).filename().string().find(needle) != std::string::npos)
      return &path;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Per-book benchmark: conversion only (excludes Book::open overhead)
// ---------------------------------------------------------------------------

static void BM_ConvertBook(benchmark::State& state, const std::string& epub_path) {
  auto filename = fs::path(epub_path).filename().string();
  auto mrb_path = (fs::temp_directory_path() / "bench.mrb").string();

  Book book;
  auto err = book.open(epub_path.c_str());
  if (err != EpubError::Ok) {
    state.SkipWithError("Cannot open book");
    return;
  }

  for (auto _ : state) {
    bool ok = convert_epub_to_mrb_streaming(book, mrb_path.c_str());
    benchmark::DoNotOptimize(ok);
  }

  auto mrb_size = fs::file_size(mrb_path);
  state.SetBytesProcessed(static_cast<int64_t>(mrb_size) * state.iterations());
  state.counters["chapters"] = static_cast<double>(book.chapter_count());
  state.counters["mrb_MB"] = mrb_size / 1048576.0;

  std::remove(mrb_path.c_str());
  std::remove((mrb_path + ".idx").c_str());
}

// ---------------------------------------------------------------------------
// Per-book benchmark: Book::open only
// ---------------------------------------------------------------------------

static void BM_OpenBook(benchmark::State& state, const std::string& epub_path) {
  for (auto _ : state) {
    Book book;
    auto err = book.open(epub_path.c_str());
    benchmark::DoNotOptimize(err);
  }
}

// ---------------------------------------------------------------------------
// Aggregate: convert ALL books end-to-end (open + convert)
// ---------------------------------------------------------------------------

static void BM_ConvertAllBooks(benchmark::State& state) {
  auto mrb_path = (fs::temp_directory_path() / "bench_all.mrb").string();
  const auto& books = all_books();

  for (auto _ : state) {
    int converted = 0;
    for (const auto& epub_path : books) {
      Book book;
      if (book.open(epub_path.c_str()) != EpubError::Ok)
        continue;
      if (convert_epub_to_mrb_streaming(book, mrb_path.c_str()))
        ++converted;
    }
    benchmark::DoNotOptimize(converted);
  }

  state.counters["books"] = static_cast<double>(books.size());

  std::remove(mrb_path.c_str());
  std::remove((mrb_path + ".idx").c_str());
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

// Key representative books at different sizes.
struct NamedBook {
  const char* label;
  const char* needle;
};

static const NamedBook kKeyBooks[] = {
    {"Shakespeare",     "complete-shakespeare"},
    {"Bible",           "bible_luther"        },
    {"WarAndPeace",     "war-and-peace"       },
    {"LesMiserables",   "les-miserables"      },
    {"AliceWonderland", "alice"               },
    {"Metamorphosis",   "metamorphosis"       },
};

static void RegisterBookBenchmarks() {
  for (const auto& [label, needle] : kKeyBooks) {
    const std::string* path = find_book(needle);
    if (!path)
      continue;
    std::string convert_name = std::string("Convert/") + label;
    std::string open_name = std::string("Open/") + label;
    benchmark::RegisterBenchmark(convert_name, BM_ConvertBook, *path)->Unit(benchmark::kMillisecond)->Iterations(3);
    benchmark::RegisterBenchmark(open_name, BM_OpenBook, *path)->Unit(benchmark::kMillisecond)->Iterations(5);
  }
}

// Register per-book benchmarks for every discovered epub.
static void RegisterAllIndividualBenchmarks() {
  for (const auto& path : all_books()) {
    auto stem = fs::path(path).stem().string();
    // Sanitize for benchmark name
    for (auto& c : stem)
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
        c = '_';
    std::string name = std::string("ConvertAll/") + stem;
    benchmark::RegisterBenchmark(name, BM_ConvertBook, path)->Unit(benchmark::kMillisecond)->Iterations(1);
  }
}

// ---------------------------------------------------------------------------
// Image size reading benchmark: extract raw image bytes + read_image_size()
// ---------------------------------------------------------------------------

static void BM_ImageSizeRead(benchmark::State& state, const std::string& epub_path) {
  Book book;
  if (book.open(epub_path.c_str()) != EpubError::Ok) {
    state.SkipWithError("Cannot open book");
    return;
  }

  // Collect unique image entry keys across all chapters (outside the timed loop).
  std::vector<uint16_t> image_keys;
  for (size_t i = 0; i < book.chapter_count(); ++i) {
    Chapter ch;
    if (book.load_chapter(i, ch) != EpubError::Ok)
      continue;
    for (auto& p : ch.paragraphs) {
      if (p.type == ParagraphType::Image)
        image_keys.push_back(p.image.key);
      if (p.type == ParagraphType::Text && p.text.inline_image.has_value())
        image_keys.push_back(p.text.inline_image->key);
    }
  }
  std::sort(image_keys.begin(), image_keys.end());
  image_keys.erase(std::unique(image_keys.begin(), image_keys.end()), image_keys.end());

  if (image_keys.empty()) {
    state.SkipWithError("No images in book");
    return;
  }

  for (auto _ : state) {
    for (uint16_t key : image_keys) {
      std::vector<uint8_t> raw;
      if (book.extract_entry(key, raw) == ZipError::Ok && !raw.empty()) {
        uint16_t w = 0, h = 0;
        auto err = read_image_size(raw.data(), raw.size(), w, h);
        benchmark::DoNotOptimize(err);
        benchmark::DoNotOptimize(w);
      }
    }
  }

  state.counters["images"] = static_cast<double>(image_keys.size());
  state.counters["images_per_sec"] =
      benchmark::Counter(static_cast<double>(image_keys.size()), benchmark::Counter::kIsRate);
}

int main(int argc, char** argv) {
  RegisterBookBenchmarks();
  RegisterAllIndividualBenchmarks();
  benchmark::RegisterBenchmark("ConvertAllBooks", BM_ConvertAllBooks)->Unit(benchmark::kMillisecond)->Iterations(1);

  // Image size reading benchmarks (books known to have images).
  static const NamedBook kImageBooks[] = {
      {"AliceIllustrated", "alice-illustrated"},
      {"PridePrejudice",   "pride-prejudice"  },
      {"MobyDick",         "moby-dick"        },
  };
  for (const auto& [label, needle] : kImageBooks) {
    const std::string* path = find_book(needle);
    if (path)
      benchmark::RegisterBenchmark(std::string("ImageSizeRead/") + label, BM_ImageSizeRead, *path)
          ->Unit(benchmark::kMillisecond)
          ->Iterations(5);
  }

  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
