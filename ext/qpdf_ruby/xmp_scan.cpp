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

struct Element {
  std::string qname;
  Bindings bindings;
  bool rdf_root;
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
                : starts("<![CDATA[") ? !stack_.empty() && skip_past("]]>")
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
    if (stack_.back().rdf_root) out.rdf_close = lt;
    stack_.pop_back();
    return true;
  }

  bool start_tag(Scan& out) {
    ++pos_;
    std::string qname = name();
    if (qname.empty()) return false;
    if (stack_.empty() && seen_root_) return false;  // a second root element

    std::vector<std::pair<std::string, std::string>> attributes;
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
        attributes.emplace_back(attribute, value);
      }
    }

    std::string ns, local;
    if (!resolve(qname, bindings, false, ns, local)) return false;
    out.properties.insert({ns, local});
    for (auto const& [attribute, value] : attributes) {
      std::string attribute_ns, attribute_local;
      if (!resolve(attribute, bindings, true, attribute_ns, attribute_local)) return false;
      if (!attribute_ns.empty()) out.properties.insert({attribute_ns, attribute_local});
    }

    bool rdf_root = ns == kRdfNs && local == "RDF" && !seen_rdf_;
    if (rdf_root) seen_rdf_ = true;
    if (stack_.empty()) seen_root_ = true;
    if (self_closing) return true;
    stack_.push_back({qname, std::move(bindings), rdf_root});
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

}  // namespace qpdf_ruby::xmp
