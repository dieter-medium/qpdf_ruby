#include "xmp_scan.hpp"

#include <map>
#include <vector>

namespace qpdf_ruby::xmp {

namespace {

constexpr char kRdfNs[] = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
constexpr char kXmlNs[] = "http://www.w3.org/XML/1998/namespace";

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool is_name_end(char c) { return is_space(c) || c == '/' || c == '>' || c == '='; }

void append_utf8(std::string& out, unsigned long cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Attribute values with the predefined and numeric character references resolved - enough to
// compare namespace URIs. False for an unknown or broken reference.
bool decode(std::string const& raw, std::string& out) {
  static const std::map<std::string, char> named = {
      {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}};
  out.clear();
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] != '&') {
      out += raw[i];
      continue;
    }
    size_t semi = raw.find(';', i);
    if (semi == std::string::npos) return false;
    std::string ref = raw.substr(i + 1, semi - i - 1);
    if (auto it = named.find(ref); it != named.end()) {
      out += it->second;
    } else if (ref.size() > 1 && ref[0] == '#') {
      bool hex = ref[1] == 'x';
      std::string digits = ref.substr(hex ? 2 : 1);
      if (digits.empty() || digits.size() > 8) return false;
      unsigned long cp = 0;
      for (char d : digits) {
        int v = (d >= '0' && d <= '9') ? d - '0'
                : hex && d >= 'a' && d <= 'f' ? d - 'a' + 10
                : hex && d >= 'A' && d <= 'F' ? d - 'A' + 10
                                              : -1;
        if (v < 0) return false;
        cp = cp * (hex ? 16 : 10) + static_cast<unsigned long>(v);
      }
      if (cp == 0 || cp > 0x10FFFF) return false;
      append_utf8(out, cp);
    } else {
      return false;
    }
    i = semi;
  }
  return true;
}

using Bindings = std::map<std::string, std::string>;  // prefix ("" = default) -> namespace URI

// Where an element sits: the first rdf:RDF, an rdf:Description directly under it, a property of
// that Description, or anything else.
enum class Kind { Other, RdfRoot, Description, Property };

struct Element {
  std::string qname;
  Bindings bindings;
  Kind kind;
  size_t start = 0;          // the "<" of the start tag
  size_t content_start = 0;  // just after its ">"
  bool simple = true;        // no child element or CDATA inside
  std::string ns, local;
};

// The namespace of a qualified name under `bindings`; false for an unbound prefix.
bool resolve(std::string const& qname, Bindings const& bindings, bool is_attribute, std::string& ns,
             std::string& local) {
  size_t colon = qname.find(':');
  if (colon == std::string::npos) {
    local = qname;
    ns.clear();
    if (!is_attribute) {  // unprefixed attributes have no namespace; elements take the default
      auto it = bindings.find("");
      if (it != bindings.end()) ns = it->second;
    }
    return !local.empty();
  }
  std::string prefix = qname.substr(0, colon);
  local = qname.substr(colon + 1);
  if (prefix.empty() || local.empty() || local.find(':') != std::string::npos) return false;
  if (prefix == "xml") {
    ns = kXmlNs;
    return true;
  }
  auto it = bindings.find(prefix);
  if (it == bindings.end() || it->second.empty()) return false;
  ns = it->second;
  return true;
}

class Scanner {
 public:
  explicit Scanner(std::string const& xml) : xml_(xml) {}

  Scan run() {
    Scan out;
    if (!walk(out)) return Scan{};
    out.well_formed = stack_.empty() && seen_root_;
    if (!out.well_formed) return Scan{};
    return out;
  }

 private:
  bool skip_past(std::string const& end) {
    size_t at = xml_.find(end, pos_);
    if (at == std::string::npos) return false;
    pos_ = at + end.size();
    return true;
  }

  bool starts(std::string const& s) const { return xml_.compare(pos_, s.size(), s) == 0; }

  std::string name() {
    size_t start = pos_;
    while (pos_ < xml_.size() && !is_name_end(xml_[pos_])) ++pos_;
    return xml_.substr(start, pos_ - start);
  }

  void spaces() {
    while (pos_ < xml_.size() && is_space(xml_[pos_])) ++pos_;
  }

  bool walk(Scan& out) {
    while (pos_ < xml_.size()) {
      size_t lt = xml_.find('<', pos_);
      if (lt == std::string::npos) return true;
      pos_ = lt;
      bool ok = starts("<!--")      ? skip_past("-->")
                : starts("<![CDATA[") ? !stack_.empty() && (stack_.back().simple = false, skip_past("]]>"))
                : starts("<?")        ? skip_past("?>")
                : starts("<!DOCTYPE") ? doctype()
                : starts("</")        ? end_tag(out)
                : starts("<!")        ? false
                                      : start_tag(out);
      if (!ok) return false;
    }
    return true;
  }

  bool doctype() {
    if (seen_root_ || !stack_.empty()) return false;
    for (int brackets = 0; pos_ < xml_.size(); ++pos_) {
      char c = xml_[pos_];
      if (c == '[') ++brackets;
      if (c == ']') --brackets;
      if (c == '>' && brackets == 0) {
        ++pos_;
        return true;
      }
    }
    return false;
  }

  bool end_tag(Scan& out) {
    size_t lt = pos_;
    pos_ += 2;
    std::string qname = name();
    spaces();
    if (pos_ >= xml_.size() || xml_[pos_] != '>' || stack_.empty() || stack_.back().qname != qname) return false;
    ++pos_;
    Element const& closed = stack_.back();
    if (closed.kind == Kind::RdfRoot) out.rdf_close = lt;
    if (closed.kind == Kind::Property) {
      Property property{closed.ns, closed.local, closed.start, pos_, "", false};
      if (closed.simple) property.has_value = decode(xml_.substr(closed.content_start, lt - closed.content_start), property.value);
      if (!property.has_value) property.value.clear();
      out.located.push_back(std::move(property));
    }
    stack_.pop_back();
    return true;
  }

  bool start_tag(Scan& out) {
    size_t lt = pos_;
    if (!stack_.empty()) stack_.back().simple = false;
    ++pos_;
    std::string qname = name();
    if (qname.empty()) return false;
    if (stack_.empty() && seen_root_) return false;  // a second root element

    struct Attribute {
      std::string qname, value;
      size_t begin, end;
    };
    std::vector<Attribute> attributes;
    Bindings bindings = stack_.empty() ? Bindings{} : stack_.back().bindings;
    bool self_closing = false;
    while (true) {
      size_t before = pos_;
      spaces();
      if (pos_ >= xml_.size()) return false;
      if (xml_[pos_] == '>') {
        ++pos_;
        break;
      }
      if (starts("/>")) {
        pos_ += 2;
        self_closing = true;
        break;
      }
      if (pos_ == before) return false;  // attributes must be separated by white space
      size_t attribute_begin = before;
      std::string attribute = name();
      spaces();
      if (attribute.empty() || pos_ >= xml_.size() || xml_[pos_] != '=') return false;
      ++pos_;
      spaces();
      if (pos_ >= xml_.size() || (xml_[pos_] != '"' && xml_[pos_] != '\'')) return false;
      char quote = xml_[pos_++];
      size_t close = xml_.find(quote, pos_);
      if (close == std::string::npos) return false;
      std::string raw = xml_.substr(pos_, close - pos_), value;
      pos_ = close + 1;
      if (raw.find('<') != std::string::npos || !decode(raw, value)) return false;
      if (attribute == "xmlns") {
        bindings[""] = value;
      } else if (attribute.rfind("xmlns:", 0) == 0) {
        // An empty prefix is no name; undeclaring a prefix is XML 1.1 only.
        if (attribute.size() == 6 || value.empty()) return false;
        bindings[attribute.substr(6)] = value;
      } else {
        attributes.push_back({attribute, value, attribute_begin, pos_});
      }
    }

    std::string ns, local;
    if (!resolve(qname, bindings, false, ns, local)) return false;
    Kind parent = stack_.empty() ? Kind::Other : stack_.back().kind;
    Kind kind = Kind::Other;
    if (ns == kRdfNs && local == "RDF" && !seen_rdf_) {
      kind = Kind::RdfRoot;
      seen_rdf_ = true;
    } else if (parent == Kind::RdfRoot && ns == kRdfNs && local == "Description") {
      kind = Kind::Description;
    } else if (parent == Kind::Description) {
      kind = Kind::Property;
      out.properties.insert({ns, local});
    }
    for (auto const& attribute : attributes) {
      std::string attribute_ns, attribute_local;
      if (!resolve(attribute.qname, bindings, true, attribute_ns, attribute_local)) return false;
      bool property = kind == Kind::Description && !attribute_ns.empty() && attribute_ns != kRdfNs && attribute_ns != kXmlNs;
      if (!property) continue;
      out.properties.insert({attribute_ns, attribute_local});
      out.located.push_back({attribute_ns, attribute_local, attribute.begin, attribute.end, attribute.value, true});
    }

    if (stack_.empty()) seen_root_ = true;
    if (self_closing) {
      if (kind == Kind::Property) out.located.push_back({ns, local, lt, pos_, "", true});
      return true;
    }
    stack_.push_back({qname, std::move(bindings), kind, lt, pos_, true, ns, local});
    return true;
  }

  std::string const& xml_;
  size_t pos_ = 0;
  std::vector<Element> stack_;
  bool seen_rdf_ = false;  // only the first rdf:RDF element counts
  bool seen_root_ = false;
};

}  // namespace

Scan scan(std::string const& xml) { return Scanner(xml).run(); }

bool valid_text(char const* text, size_t size) noexcept {
  auto const* bytes = reinterpret_cast<unsigned char const*>(text);
  for (size_t i = 0; i < size;) {
    unsigned char lead = bytes[i];
    unsigned long cp;
    size_t length;
    if (lead < 0x80) {
      cp = lead;
      length = 1;
    } else if ((lead & 0xE0) == 0xC0) {
      cp = lead & 0x1F;
      length = 2;
    } else if ((lead & 0xF0) == 0xE0) {
      cp = lead & 0x0F;
      length = 3;
    } else if ((lead & 0xF8) == 0xF0) {
      cp = lead & 0x07;
      length = 4;
    } else {
      return false;
    }
    if (i + length > size) return false;
    for (size_t k = 1; k < length; ++k) {
      if ((bytes[i + k] & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (bytes[i + k] & 0x3F);
    }
    static constexpr unsigned long kMinimum[] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < kMinimum[length]) return false;  // an overlong form
    bool allowed = cp == 0x9 || cp == 0xA || cp == 0xD || (cp >= 0x20 && cp <= 0xD7FF) ||
                   (cp >= 0xE000 && cp <= 0xFFFD) || (cp >= 0x10000 && cp <= 0x10FFFF);
    if (!allowed) return false;
    i += length;
  }
  return true;
}

}  // namespace qpdf_ruby::xmp
