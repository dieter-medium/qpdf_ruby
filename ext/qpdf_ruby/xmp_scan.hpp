#pragma once

#include <set>
#include <string>
#include <utility>

namespace qpdf_ruby::xmp {

// What the identification step needs to know about an XMP packet, read with namespace scoping:
// comments, CDATA sections, processing instructions and a DOCTYPE are skipped, so a property that
// is only mentioned in one of them does not count.
struct Scan {
  // Every tag closed in order, one root element, every prefix bound, attributes quoted and
  // separated. Not a validating parser: names and text outside the root are not checked.
  bool well_formed = false;
  // (namespace URI, local name) of every element and every namespaced attribute - XMP allows a
  // simple property in either form.
  std::set<std::pair<std::string, std::string>> properties;
  // Offset of the "<" of the end tag of the outermost rdf:RDF element; npos without one.
  size_t rdf_close = std::string::npos;

  bool has(std::string const& ns, std::string const& local) const { return properties.count({ns, local}) > 0; }
};

Scan scan(std::string const& xml);

}  // namespace qpdf_ruby::xmp
