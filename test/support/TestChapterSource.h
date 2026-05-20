#pragma once

#include "microreader/content/ContentModel.h"
#include "microreader/content/IParagraphSource.h"

namespace microreader {

// IParagraphSource adapter wrapping an in-memory Chapter.
// Used in tests to feed a Chapter directly into TextLayout.
class TestChapterSource : public IParagraphSource {
 public:
  explicit TestChapterSource(const Chapter& ch) : ch_(ch) {}
  size_t paragraph_count() const override {
    return ch_.paragraphs.size();
  }
  const Paragraph& paragraph(size_t index) const override {
    return ch_.paragraphs[index];
  }

 private:
  const Chapter& ch_;
};

}  // namespace microreader
