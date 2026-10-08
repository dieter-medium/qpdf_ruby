#pragma once

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace qpdf_ruby::xmp {

// A property of the document - a child element or a namespaced attribute of an rdf:Description
// directly under the packet's first rdf:RDF - with where it sits in the packet.
struct Property {
  std::string ns;
  std::string local;
  // [begin, end) of the whole element, or of the attribute with the white space before it.
  size_t begin = 0;
  size_t end = 0;
  // The text value, references resolved; empty for an element with child elements, CDATA or a
  // reference the scanner does not know (has_value false).
  std::string value;
  bool has_value = false;
};

// What the metadata steps need to know about an XMP packet, read with namespace scoping:
// comments, CDATA sections, processing instructions and a DOCTYPE are skipped, so a property that
// is only mentioned in one of them does not count, and neither does an element of the same name
// anywhere but on the document's own rdf:Description.
struct Scan {
  // Every tag closed in order, one root element, every prefix bound, attributes quoted and
  // separated. Not a validating parser: names and text outside the root are not checked.
  bool well_formed = false;
  // (namespace URI, local name) of every property of the document - XMP allows a simple property
  // as an element or as an attribute.
  std::set<std::pair<std::string, std::string>> properties;
  std::vector<Property> located;
  // Offset of the "<" of the end tag of the outermost rdf:RDF element; npos without one.
  size_t rdf_close = std::string::npos;

  bool has(std::string const& ns, std::string const& local) const { return properties.count({ns, local}) > 0; }
};

Scan scan(std::string const& xml);

// True if `size` bytes at `text` are UTF-8 made only of XML 1.0 characters (no NUL, no control
// character but tab, line feed and carriage return, no surrogate, no U+FFFE/U+FFFF) - what may go
// into an XMP packet. Allocates nothing and never throws.
bool valid_text(char const* text, size_t size) noexcept;

}  // namespace qpdf_ruby::xmp
