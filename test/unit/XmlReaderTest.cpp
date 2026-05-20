#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "microreader/content/XmlReader.h"
#include "microreader/content/ZipReader.h"

using namespace microreader;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string to_string(XmlStringView sv) {
  if (!sv.data || sv.length == 0)
    return {};
  return std::string(sv.data, sv.length);
}

// Parse XML from memory with a given buffer size
class XmlTest : public ::testing::Test {
 protected:
  std::vector<uint8_t> work_buf;
  MemoryXmlInput* input = nullptr;
  XmlReader reader;

  void open(const char* xml, size_t buf_size = 4096) {
    work_buf.resize(buf_size);
    input = new MemoryXmlInput(xml, std::strlen(xml));
    ASSERT_EQ(reader.open(*input, work_buf.data(), work_buf.size()), XmlError::Ok);
  }

  XmlEvent next() {
    XmlEvent ev;
    XmlError err = reader.next_event(ev);
    EXPECT_EQ(err, XmlError::Ok);
    return ev;
  }

  void TearDown() override {
    delete input;
    input = nullptr;
  }
};

// ---------------------------------------------------------------------------
// Basic parsing
// ---------------------------------------------------------------------------

TEST_F(XmlTest, EmptyInput) {
  open("");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndOfFile);
}

TEST_F(XmlTest, ProcessingInstruction) {
  open("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::ProcessingInstruction);
  EXPECT_EQ(ev.name, "xml");
  EXPECT_EQ(to_string(ev.attrs.get("version")), "1.0");
  EXPECT_EQ(to_string(ev.attrs.get("encoding")), "utf-8");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndOfFile);
}

TEST_F(XmlTest, SimpleElement) {
  open("<root></root>");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "root");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "root");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndOfFile);
}

TEST_F(XmlTest, SelfClosingElement) {
  open("<br/>");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "br");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "br");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndOfFile);
}

TEST_F(XmlTest, SelfClosingWithSpace) {
  open("<br />");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "br");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "br");
}

TEST_F(XmlTest, SelfClosingWithAttrs) {
  open("<img src=\"test.jpg\" alt=\"A test\"/>");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "img");
  EXPECT_EQ(to_string(ev.attrs.get("src")), "test.jpg");
  EXPECT_EQ(to_string(ev.attrs.get("alt")), "A test");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "img");
}

TEST_F(XmlTest, TextContent) {
  open("<p>Hello, world!</p>");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "p");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);
  EXPECT_EQ(to_string(ev.content), "Hello, world!");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "p");
}

TEST_F(XmlTest, WhitespaceOnlyTextEmitted) {
  open("<root>  \n  <child/> \n </root>");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "root");

  // Whitespace-only text nodes are now emitted (significant between inline elements)
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "child");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "child");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "root");
}

TEST_F(XmlTest, ElementWithAttributes) {
  open("<div id=\"main\" class=\"container wide\">");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "div");
  EXPECT_EQ(to_string(ev.attrs.get("id")), "main");
  EXPECT_EQ(to_string(ev.attrs.get("class")), "container wide");

  // attribute that doesn't exist
  EXPECT_TRUE(ev.attrs.get("nonexistent").empty());
}

TEST_F(XmlTest, SingleQuotedAttributes) {
  open("<item key='value'/>");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "item");
  EXPECT_EQ(to_string(ev.attrs.get("key")), "value");
}

TEST_F(XmlTest, NamespacedElements) {
  open("<dc:title>My Book</dc:title>");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "dc:title");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);
  EXPECT_EQ(to_string(ev.content), "My Book");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "dc:title");
}

TEST_F(XmlTest, NamespacedAttributes) {
  open("<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "rootfile");
  EXPECT_EQ(to_string(ev.attrs.get("full-path")), "OEBPS/content.opf");
  EXPECT_EQ(to_string(ev.attrs.get("media-type")), "application/oebps-package+xml");
}

TEST_F(XmlTest, Comment) {
  open("<!-- This is a comment --><root/>");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Comment);
  // Content should contain the comment text
  std::string text = to_string(ev.content);
  EXPECT_NE(text.find("This is a comment"), std::string::npos);

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "root");
}

TEST_F(XmlTest, CData) {
  open("<![CDATA[Some <raw> & data]]>");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::CData);
  EXPECT_EQ(to_string(ev.content), "Some <raw> & data");
}

TEST_F(XmlTest, Dtd) {
  open("<!DOCTYPE html><html/>");
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Dtd);

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "html");
}

// ---------------------------------------------------------------------------
// Nested structures (EPUB-like)
// ---------------------------------------------------------------------------

TEST_F(XmlTest, NestedElements) {
  open("<root><a><b>text</b></a></root>");
  EXPECT_EQ(next().type, XmlEventType::StartElement);  // root
  EXPECT_EQ(next().type, XmlEventType::StartElement);  // a
  EXPECT_EQ(next().type, XmlEventType::StartElement);  // b

  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);
  EXPECT_EQ(to_string(ev.content), "text");

  EXPECT_EQ(next().type, XmlEventType::EndElement);  // b
  EXPECT_EQ(next().type, XmlEventType::EndElement);  // a
  EXPECT_EQ(next().type, XmlEventType::EndElement);  // root
  EXPECT_EQ(next().type, XmlEventType::EndOfFile);
}

TEST_F(XmlTest, EpubContainerXml) {
  const char* xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<container xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\" version=\"1.0\">\n"
      "  <rootfiles>\n"
      "    <rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>\n"
      "  </rootfiles>\n"
      "</container>";

  open(xml);

  // PI
  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::ProcessingInstruction);

  // whitespace between PI and <container>
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);

  // container
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "container");

  // whitespace before <rootfiles>
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);

  // rootfiles
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "rootfiles");

  // whitespace before <rootfile>
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);

  // rootfile (self-closing)
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "rootfile");
  EXPECT_EQ(to_string(ev.attrs.get("full-path")), "OEBPS/content.opf");

  // self-closing end
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "rootfile");

  // whitespace before </rootfiles>
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);

  // </rootfiles>
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "rootfiles");

  // whitespace before </container>
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);

  // </container>
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "container");

  EXPECT_EQ(next().type, XmlEventType::EndOfFile);
}

TEST_F(XmlTest, EpubMetadata) {
  const char* xml =
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
      "<dc:title>Test Book</dc:title>"
      "<dc:creator>Test Author</dc:creator>"
      "<dc:language>en</dc:language>"
      "<meta name=\"cover\" content=\"cover-image\"/>"
      "</metadata>";

  open(xml);

  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "metadata");

  // dc:title
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "dc:title");
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);
  EXPECT_EQ(to_string(ev.content), "Test Book");
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);

  // dc:creator
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "dc:creator");
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);
  EXPECT_EQ(to_string(ev.content), "Test Author");
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);

  // dc:language
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "dc:language");
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);
  EXPECT_EQ(to_string(ev.content), "en");
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);

  // meta
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "meta");
  EXPECT_EQ(to_string(ev.attrs.get("name")), "cover");
  EXPECT_EQ(to_string(ev.attrs.get("content")), "cover-image");
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);  // self-closing

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);  // metadata
}

TEST_F(XmlTest, InlineElements) {
  // Test inline elements like <i>, <b> within a paragraph
  open("<p>Text with <i>Inline</i> styles <b>bold</b></p>");

  EXPECT_EQ(next().name, "p");  // start p

  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);
  EXPECT_EQ(to_string(ev.content), "Text with ");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "i");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);
  EXPECT_EQ(to_string(ev.content), "Inline");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "i");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);
  EXPECT_EQ(to_string(ev.content), " styles ");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "b");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::Text);
  EXPECT_EQ(to_string(ev.content), "bold");

  EXPECT_EQ(next().type, XmlEventType::EndElement);  // /b
  EXPECT_EQ(next().type, XmlEventType::EndElement);  // /p
}

// ---------------------------------------------------------------------------
// Streaming with small buffer
// ---------------------------------------------------------------------------

TEST_F(XmlTest, SmallBuffer) {
  // Use a small buffer to test the advance/refill logic.
  // Buffer must be large enough to hold the longest single element.
  // "<child attr=\"value\">" is 20 chars, so 32 is the minimum viable size.
  // Note: text content may arrive in multiple Text events when the buffer
  // is smaller than the text.
  const char* xml = "<root><child attr=\"value\">content</child></root>";
  open(xml, 32);

  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "root");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "child");
  EXPECT_EQ(to_string(ev.attrs.get("attr")), "value");

  // Collect all text events (may be split across buffer boundaries)
  std::string collected;
  while (true) {
    ev = next();
    if (ev.type == XmlEventType::Text) {
      collected += to_string(ev.content);
    } else {
      break;
    }
  }
  EXPECT_EQ(collected, "content");

  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "child");

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "root");

  EXPECT_EQ(next().type, XmlEventType::EndOfFile);
}

TEST_F(XmlTest, MediumBufferWithLongText) {
  // 64 byte buffer with text that spans multiple fills
  std::string xml = "<p>";
  for (int i = 0; i < 20; ++i)
    xml += "word ";
  xml += "</p>";

  open(xml.c_str(), 64);

  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "p");

  // Collect all text events
  std::string collected;
  while (true) {
    ev = next();
    if (ev.type == XmlEventType::Text) {
      collected += to_string(ev.content);
    } else {
      break;
    }
  }
  EXPECT_EQ(ev.type, XmlEventType::EndElement);
  EXPECT_EQ(ev.name, "p");

  // Build expected text
  std::string expected;
  for (int i = 0; i < 20; ++i)
    expected += "word ";
  EXPECT_EQ(collected, expected);
}

// ---------------------------------------------------------------------------
// Buffer-boundary edge cases: XML tokens split across buffer refill points.
// These tests exercise the advance/refill logic in XmlReader::next_event()
// by choosing a buffer size that forces a refill inside the token being parsed.
// ---------------------------------------------------------------------------

// Helper macro: collect all Text events from the fixture into `collected_`
// until a non-Text event lands in `terminator_`.  Uses the protected next()
// accessor that every TEST_F in this suite can call directly.
#define COLLECT_TEXT(collected_, terminator_)   \
  do {                                          \
    while (true) {                              \
      auto _ev = next();                        \
      if (_ev.type == XmlEventType::Text)       \
        (collected_) += to_string(_ev.content); \
      else {                                    \
        (terminator_) = _ev;                    \
        break;                                  \
      }                                         \
    }                                           \
  } while (0)

// Two adjacent elements: the second forces a buffer refill after the first.
// "<img src=\"long-attr-value\"/>" is 28 bytes; buf=28 fits it exactly,
// so parsing "<br/>" requires a refill.
TEST_F(XmlTest, AttributeValueSplitAcrossBuffer) {
  const char* xml = "<img src=\"long-attr-value\"/><br/>";
  open(xml, 28);

  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "img");
  EXPECT_EQ(to_string(ev.attrs.get("src")), "long-attr-value");
  EXPECT_EQ(next().type, XmlEventType::EndElement);

  // Buffer refill happens here.
  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "br");
  EXPECT_EQ(next().type, XmlEventType::EndElement);
}

// Text content spans two buffer-fills; element names fit in their own fill.
// "<longelement>" is 13 bytes, "</longelement>" is 14 bytes — buf=16 fits
// each tag individually but forces refills between tag, text, and close tag.
TEST_F(XmlTest, ElementNameSplitAcrossBuffer) {
  const char* xml = "<longelement>text</longelement>";
  open(xml, 16);

  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "longelement");

  XmlEvent terminator;
  std::string text;
  COLLECT_TEXT(text, terminator);
  EXPECT_EQ(text, "text");

  EXPECT_EQ(terminator.type, XmlEventType::EndElement);
  EXPECT_EQ(terminator.name, "longelement");
}

// End-tag name split across a buffer boundary.
TEST_F(XmlTest, EndTagNameSplitAcrossBuffer) {
  const char* xml = "<ab>hello</abcde>";
  open(xml, 8);

  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "ab");

  XmlEvent terminator;
  std::string text;
  COLLECT_TEXT(text, terminator);

  EXPECT_EQ(terminator.type, XmlEventType::EndElement);
  EXPECT_EQ(terminator.name, "abcde");
}

// CDATA token: "<![CDATA[hello world]]>" is 23 bytes → buf=24 just fits it.
TEST_F(XmlTest, CDataContentSplitAcrossBuffer) {
  const char* xml = "<![CDATA[hello world]]>";
  open(xml, 24);

  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::CData);
  EXPECT_EQ(to_string(ev.content), "hello world");
}

// Multiple adjacent elements where the boundary falls between them.
TEST_F(XmlTest, MultipleElementsSmallBuffer) {
  const char* xml = "<a/><b/><c/>";
  open(xml, 8);

  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "a");
  EXPECT_EQ(next().type, XmlEventType::EndElement);

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "b");
  EXPECT_EQ(next().type, XmlEventType::EndElement);

  ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);
  EXPECT_EQ(ev.name, "c");
  EXPECT_EQ(next().type, XmlEventType::EndElement);

  EXPECT_EQ(next().type, XmlEventType::EndOfFile);
}

// UTF-8 multi-byte sequence split across a buffer boundary.
// The test simply verifies the full sequence is reassembled correctly.
TEST_F(XmlTest, Utf8MultiByteContentSplitAcrossBuffer) {
  // U+00E9 = é = 0xC3 0xA9 (2 bytes). Use buf=8 to force split.
  const char* xml = "<t>\xC3\xA9\xC3\xA9\xC3\xA9</t>";
  open(xml, 8);

  auto ev = next();
  EXPECT_EQ(ev.type, XmlEventType::StartElement);

  XmlEvent term;
  std::string text;
  COLLECT_TEXT(text, term);
  // All three é characters must survive the buffer boundary.
  EXPECT_EQ(text.size(), 6u);  // 3 × 2 bytes
  EXPECT_EQ(static_cast<uint8_t>(text[0]), 0xC3u);
  EXPECT_EQ(static_cast<uint8_t>(text[1]), 0xA9u);
  EXPECT_EQ(text, "\xC3\xA9\xC3\xA9\xC3\xA9");
}

// ---------------------------------------------------------------------------
// EPUB fixture extraction + XML parsing
// ---------------------------------------------------------------------------

static std::string fixture(const char* name) {
  return std::string(TEST_FIXTURES_DIR) + "/" + name;
}

TEST(XmlReaderEpub, ParseContainerXml) {
  // Extract container.xml from basic.epub using ZipReader, then parse with XmlReader
  // This integration test validates the full pipeline
  StdioZipFile file;
  ASSERT_TRUE(file.open(fixture("basic.epub").c_str()));

  ZipReader zip;
  ASSERT_EQ(zip.open(file), ZipError::Ok);

  auto* entry = zip.find("META-INF/container.xml");
  ASSERT_NE(entry, nullptr);

  std::vector<uint8_t> data;
  ASSERT_EQ(zip.extract(file, *entry, data), ZipError::Ok);

  // Parse the XML
  MemoryXmlInput input(data.data(), data.size());
  uint8_t buf[512];
  XmlReader reader;
  ASSERT_EQ(reader.open(input, buf, sizeof(buf)), XmlError::Ok);

  std::string rootfile_path;
  XmlEvent ev;
  while (reader.next_event(ev) == XmlError::Ok) {
    if (ev.type == XmlEventType::EndOfFile)
      break;
    if (ev.type == XmlEventType::StartElement && ev.name == "rootfile") {
      rootfile_path = to_string(ev.attrs.get("full-path"));
    }
  }

  EXPECT_EQ(rootfile_path, "OEBPS/content.opf");
}

TEST(XmlReaderEpub, ParseContentOpf) {
  StdioZipFile file;
  ASSERT_TRUE(file.open(fixture("basic.epub").c_str()));

  ZipReader zip;
  ASSERT_EQ(zip.open(file), ZipError::Ok);

  auto* entry = zip.find("OEBPS/content.opf");
  ASSERT_NE(entry, nullptr);

  std::vector<uint8_t> data;
  ASSERT_EQ(zip.extract(file, *entry, data), ZipError::Ok);

  MemoryXmlInput input(data.data(), data.size());
  uint8_t buf[4096];
  XmlReader reader;
  ASSERT_EQ(reader.open(input, buf, sizeof(buf)), XmlError::Ok);

  std::string title;
  std::vector<std::string> spine_idrefs;
  bool in_metadata = false;
  bool in_spine = false;

  XmlEvent ev;
  while (reader.next_event(ev) == XmlError::Ok) {
    if (ev.type == XmlEventType::EndOfFile)
      break;
    if (ev.type == XmlEventType::StartElement) {
      if (ev.name == "metadata") {
        in_metadata = true;
      } else if (ev.name == "spine") {
        in_spine = true;
      } else if (in_metadata && ev.name == "dc:title") {
        XmlEvent text_ev;
        if (reader.next_event(text_ev) == XmlError::Ok && text_ev.type == XmlEventType::Text) {
          title = to_string(text_ev.content);
        }
      } else if (in_spine && ev.name == "itemref") {
        auto idref = ev.attrs.get("idref");
        if (!idref.empty()) {
          spine_idrefs.push_back(to_string(idref));
        }
      }
    } else if (ev.type == XmlEventType::EndElement) {
      if (ev.name == "metadata")
        in_metadata = false;
      if (ev.name == "spine")
        in_spine = false;
    }
  }

  EXPECT_EQ(title, "Basic Test");
  EXPECT_GE(spine_idrefs.size(), 1u);
}

TEST(XmlReaderEpub, ParseXhtmlChapter) {
  StdioZipFile file;
  ASSERT_TRUE(file.open(fixture("basic.epub").c_str()));

  ZipReader zip;
  ASSERT_EQ(zip.open(file), ZipError::Ok);

  auto* entry = zip.find("OEBPS/chapter1.xhtml");
  ASSERT_NE(entry, nullptr);

  std::vector<uint8_t> data;
  ASSERT_EQ(zip.extract(file, *entry, data), ZipError::Ok);

  MemoryXmlInput input(data.data(), data.size());
  uint8_t buf[4096];
  XmlReader reader;
  ASSERT_EQ(reader.open(input, buf, sizeof(buf)), XmlError::Ok);

  std::vector<std::string> text_pieces;
  XmlEvent ev;
  while (reader.next_event(ev) == XmlError::Ok) {
    if (ev.type == XmlEventType::EndOfFile)
      break;
    if (ev.type == XmlEventType::Text) {
      text_pieces.push_back(to_string(ev.content));
    }
  }

  // Should contain chapter text
  std::string all_text;
  for (auto& t : text_pieces)
    all_text += t;
  EXPECT_NE(all_text.find("Hello, world!"), std::string::npos);
  EXPECT_NE(all_text.find("Chapter One"), std::string::npos);
}

TEST(XmlReaderEpub, ParseMultiChapterOpf) {
  StdioZipFile file;
  ASSERT_TRUE(file.open(fixture("multi_chapter.epub").c_str()));

  ZipReader zip;
  ASSERT_EQ(zip.open(file), ZipError::Ok);

  auto* entry = zip.find("OEBPS/content.opf");
  ASSERT_NE(entry, nullptr);

  std::vector<uint8_t> data;
  ASSERT_EQ(zip.extract(file, *entry, data), ZipError::Ok);

  MemoryXmlInput input(data.data(), data.size());
  uint8_t buf[4096];
  XmlReader reader;
  ASSERT_EQ(reader.open(input, buf, sizeof(buf)), XmlError::Ok);

  std::vector<std::string> spine_idrefs;
  bool in_spine = false;

  XmlEvent ev;
  while (reader.next_event(ev) == XmlError::Ok) {
    if (ev.type == XmlEventType::EndOfFile)
      break;
    if (ev.type == XmlEventType::StartElement && ev.name == "spine") {
      in_spine = true;
    } else if (ev.type == XmlEventType::EndElement && ev.name == "spine") {
      in_spine = false;
    } else if (in_spine && ev.type == XmlEventType::StartElement && ev.name == "itemref") {
      auto idref = ev.attrs.get("idref");
      if (!idref.empty())
        spine_idrefs.push_back(to_string(idref));
    }
  }

  EXPECT_EQ(spine_idrefs.size(), 3u);
}

// ---------------------------------------------------------------------------
// AttributeReader iteration
// ---------------------------------------------------------------------------

TEST(XmlAttributeReaderTest, IterateAll) {
  const char* attrs_str = " id=\"main\" class=\"big\" style=\"color:red\"";
  XmlAttributeReader reader;
  reader.data = attrs_str;
  reader.length = std::strlen(attrs_str);

  XmlAttributeReader::Iterator it(reader.data, reader.length);
  ASSERT_TRUE(it.has_next());
  auto a = it.next();
  EXPECT_EQ(to_string(a.name), "id");
  EXPECT_EQ(to_string(a.value), "main");

  ASSERT_TRUE(it.has_next());
  a = it.next();
  EXPECT_EQ(to_string(a.name), "class");
  EXPECT_EQ(to_string(a.value), "big");

  ASSERT_TRUE(it.has_next());
  a = it.next();
  EXPECT_EQ(to_string(a.name), "style");
  EXPECT_EQ(to_string(a.value), "color:red");

  EXPECT_FALSE(it.has_next());
}

TEST(XmlAttributeReaderTest, EmptyAttrs) {
  XmlAttributeReader reader;
  reader.data = nullptr;
  reader.length = 0;

  auto val = reader.get("anything");
  EXPECT_TRUE(val.empty());
}
