#include "pdfua.hpp"

#include <qpdf/Pl_Buffer.hh>
#include <qpdf/QPDFNameTreeObjectHelper.hh>
#include <qpdf/QPDFNumberTreeObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFTokenizer.hh>

#include <functional>
#include <memory>
#include <regex>
#include <set>
#include <vector>

namespace qpdf_ruby::pdfua {

namespace {

const std::set<std::string> kPathConstruction = {"m", "l", "c", "v", "y", "h", "re"};
const std::set<std::string> kClip = {"W", "W*"};
const std::set<std::string> kPathEnd = {"S", "s", "f", "F", "f*", "B", "B*", "b", "b*", "n"};
const std::set<std::string> kTextShow = {"Tj", "TJ", "'", "\""};
// Graphics state and colour operators producers emit between a path's construction and its
// painting operator (Chromium: `... l 2 w S`). They change no geometry, so they stay part of the
// path object instead of ending it.
const std::set<std::string> kPathState = {"w",  "J",  "j", "M",  "d",  "ri", "i",  "gs", "CS", "cs",
                                          "SC", "SCN", "sc", "scn", "G", "g",  "RG", "rg", "K",  "k"};

// Notes whether a content stream uses a marked-content operator - as a token, so "BDC" inside a
// string or an inline image's data does not count.
class MarkedContentProbe : public QPDFObjectHandle::TokenFilter {
 public:
  void handleToken(QPDFTokenizer::Token const& token) override {
    if (token.getType() == QPDFTokenizer::tt_word && (token.getValue() == "BDC" || token.getValue() == "BMC")) {
      found = true;
    }
  }
  bool found = false;
};

// True if a Form XObject (or anything it draws) carries marked content of its own.
bool form_has_marked_content(QPDFObjectHandle xobject, std::set<QPDFObjGen>& seen) {
  if (!xobject.isStream() || !seen.insert(xobject.getObjGen()).second) return false;
  QPDFObjectHandle dict = xobject.getDict();
  if (!dict.getKey("/Subtype").isNameAndEquals("/Form")) return false;
  if (dict.hasKey("/StructParents") || dict.hasKey("/StructParent")) return true;
  MarkedContentProbe probe;
  xobject.filterAsContents(&probe, nullptr);
  if (probe.found) return true;
  QPDFObjectHandle resources = dict.getKey("/Resources");
  QPDFObjectHandle xobjects = resources.isDictionary() ? resources.getKey("/XObject") : QPDFObjectHandle::newNull();
  if (xobjects.isDictionary()) {
    for (auto [name, child] : xobjects.ditems()) {
      if (form_has_marked_content(child, seen)) return true;
    }
  }
  return false;
}

// Token filter behind mark_untagged_content_as_artifacts / count_untagged_content.
class UntaggedFilter : public QPDFObjectHandle::TokenFilter {
 public:
  UntaggedFilter(QPDFObjectHandle resources, bool rewrite) : resources_(std::move(resources)), rewrite_(rewrite) {}

  void handleToken(QPDFTokenizer::Token const& token) override {
    if (in_inline_image_) {
      object_.push_back(token);
      if (token.getType() == QPDFTokenizer::tt_word && token.getValue() == "EI") {
        emit_object(true);
        ++counts.inline_images;
        in_inline_image_ = false;
      }
      return;
    }
    if (token.getType() != QPDFTokenizer::tt_word) {
      pending_.push_back(token);
      return;
    }
    std::string const& op = token.getValue();

    if (op == "BMC" || op == "BDC" || op == "EMC") {
      flush_object_unwrapped();
      flush_pending();
      writeToken(token);
      if (op == "EMC") {
        if (depth_ > 0) --depth_;
      } else {
        ++depth_;
      }
      return;
    }
    if (depth_ > 0) {
      flush_pending();
      writeToken(token);
      return;
    }

    if (op == "BI") {
      flush_object_unwrapped();
      take_pending();
      object_.push_back(token);
      in_inline_image_ = true;
      return;
    }
    if (kPathConstruction.count(op) || (in_path_ && kClip.count(op))) {
      take_pending();
      object_.push_back(token);
      in_path_ = true;
      return;
    }
    if (in_path_ && kPathState.count(op)) {
      take_pending();
      object_.push_back(token);
      return;
    }
    if (in_path_ && kPathEnd.count(op)) {
      take_pending();
      object_.push_back(token);
      bool painted = op != "n";
      emit_object(painted);
      if (painted) ++counts.paths;
      in_path_ = false;
      return;
    }
    if (kTextShow.count(op) || op == "sh" || (op == "Do" && !draws_marked_form())) {
      take_pending();
      object_.push_back(token);
      emit_object(true);
      if (kTextShow.count(op)) ++counts.texts;
      else if (op == "sh") ++counts.shadings;
      else ++counts.xobjects;
      return;
    }

    flush_object_unwrapped();
    flush_pending();
    writeToken(token);
  }

  void handleEOF() override {
    flush_object_unwrapped();
    flush_pending();
  }

  UntaggedCounts counts;

 private:
  // The operand of the Do being handled is the last name among the pending tokens.
  bool draws_marked_form() {
    for (auto it = pending_.rbegin(); it != pending_.rend(); ++it) {
      if (it->getType() == QPDFTokenizer::tt_name) {
        QPDFObjectHandle xobjects =
            resources_.isDictionary() ? resources_.getKey("/XObject") : QPDFObjectHandle::newNull();
        if (!xobjects.isDictionary()) return false;
        std::set<QPDFObjGen> seen;
        return form_has_marked_content(xobjects.getKey(it->getValue()), seen);
      }
    }
    return false;
  }

  void take_pending() {
    object_.insert(object_.end(), pending_.begin(), pending_.end());
    pending_.clear();
  }

  void flush_pending() {
    for (auto const& t : pending_) writeToken(t);
    pending_.clear();
  }

  void flush_object_unwrapped() {
    for (auto const& t : object_) writeToken(t);
    object_.clear();
    in_path_ = false;
  }

  void emit_object(bool wrap) {
    bool artifact = wrap && rewrite_;
    if (artifact) write("/Artifact BMC\n");
    for (auto const& t : object_) writeToken(t);
    if (artifact) write("\nEMC\n");
    object_.clear();
  }

  QPDFObjectHandle resources_;
  bool rewrite_;
  std::vector<QPDFTokenizer::Token> pending_;
  std::vector<QPDFTokenizer::Token> object_;
  bool in_path_ = false;
  bool in_inline_image_ = false;
  int depth_ = 0;
};

void add_counts(UntaggedCounts& into, UntaggedCounts const& from) {
  into.paths += from.paths;
  into.texts += from.texts;
  into.xobjects += from.xobjects;
  into.shadings += from.shadings;
  into.inline_images += from.inline_images;
}

// Runs the filter over a page or Form XObject; when rewriting, replaces its content with the
// result. Form XObjects with marked content of their own are walked too (once each).
UntaggedCounts filter_contents(QPDF& pdf, QPDFObjectHandle owner, bool rewrite, std::set<QPDFObjGen>& visited) {
  UntaggedCounts counts;
  if (!visited.insert(owner.getObjGen()).second) return counts;

  QPDFPageObjectHelper helper(owner);
  QPDFObjectHandle resources = helper.getAttribute("/Resources", false);
  UntaggedFilter filter(resources, rewrite);
  Pl_Buffer buffer("pdfua artifacts");
  helper.filterContents(&filter, rewrite ? &buffer : nullptr);
  add_counts(counts, filter.counts);

  if (rewrite) {
    auto data = buffer.getBufferSharedPointer();
    if (owner.isStream()) {
      owner.replaceStreamData(data, QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
    } else {
      owner.replaceKey("/Contents", QPDFObjectHandle::newStream(&pdf, data));
    }
  }

  QPDFObjectHandle xobjects = resources.isDictionary() ? resources.getKey("/XObject") : QPDFObjectHandle::newNull();
  if (xobjects.isDictionary()) {
    for (auto [name, xobject] : xobjects.ditems()) {
      std::set<QPDFObjGen> seen;
      if (form_has_marked_content(xobject, seen)) add_counts(counts, filter_contents(pdf, xobject, rewrite, visited));
    }
  }
  return counts;
}

UntaggedCounts walk_untagged(QPDF& pdf, bool rewrite) {
  UntaggedCounts counts;
  std::set<QPDFObjGen> visited;
  for (QPDFObjectHandle page : pdf.getAllPages()) add_counts(counts, filter_contents(pdf, page, rewrite, visited));
  return counts;
}

QPDFObjectHandle struct_tree(QPDF& pdf) { return pdf.getRoot().getKey("/StructTreeRoot"); }

// The page a structure element's marked content lives on: its own /Pg or an ancestor's.
QPDFObjectHandle page_of(QPDFObjectHandle elem) {
  for (int guard = 0; elem.isDictionary() && guard < 256; ++guard) {
    if (elem.getKey("/Pg").isDictionary()) return elem.getKey("/Pg");
    elem = elem.getKey("/P");
  }
  return QPDFObjectHandle::newNull();
}

std::vector<QPDFObjectHandle> kids_of(QPDFObjectHandle elem) {
  std::vector<QPDFObjectHandle> kids;
  QPDFObjectHandle k = elem.getKey("/K");
  if (k.isArray()) {
    for (auto kid : k.aitems()) kids.push_back(kid);
  } else if (!k.isNull()) {
    kids.push_back(k);
  }
  return kids;
}

void each_struct_elem(QPDFObjectHandle node, std::function<void(QPDFObjectHandle)> const& fn,
                      std::set<QPDFObjGen>& seen, int depth = 0) {
  if (!node.isDictionary() || depth > 512) return;
  if (node.isIndirect() && !seen.insert(node.getObjGen()).second) return;
  if (node.hasKey("/S")) fn(node);
  for (auto& kid : kids_of(node)) each_struct_elem(kid, fn, seen, depth + 1);
}

void each_struct_elem(QPDF& pdf, std::function<void(QPDFObjectHandle)> const& fn) {
  std::set<QPDFObjGen> seen;
  QPDFObjectHandle tree = struct_tree(pdf);
  if (tree.isDictionary()) each_struct_elem(tree, fn, seen);
}

// The content stream an MCID is numbered in: a marked-content reference's /Stm (a Form XObject),
// else its /Pg, else the page of the structure element holding it. Null when /Stm is not a stream.
QPDFObjectHandle mcid_owner(QPDFObjectHandle kid, QPDFObjectHandle page) {
  if (kid.isDictionary() && kid.hasKey("/Stm")) {
    QPDFObjectHandle stm = kid.getKey("/Stm");
    return stm.isStream() ? stm : QPDFObjectHandle::newNull();
  }
  if (kid.isDictionary() && kid.getKey("/Pg").isDictionary()) return kid.getKey("/Pg");
  return page;
}

// The ParentTree array of an MCID owner (page or Form XObject), found through its /StructParents.
QPDFObjectHandle parent_tree_entries(QPDF& pdf, QPDFObjectHandle parent_tree, QPDFObjectHandle owner) {
  QPDFObjectHandle dict = owner.isStream() ? owner.getDict() : owner;
  QPDFObjectHandle key = dict.isDictionary() ? dict.getKey("/StructParents") : QPDFObjectHandle::newNull();
  QPDFObjectHandle entries;
  if (!parent_tree.isDictionary() || !key.isInteger() ||
      !QPDFNumberTreeObjectHelper(parent_tree, pdf).findObject(key.getIntValue(), entries) || !entries.isArray()) {
    return QPDFObjectHandle::newNull();
  }
  return entries;
}

std::string xml_escape(std::string const& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

}  // namespace

UntaggedCounts mark_untagged_content_as_artifacts(QPDF& pdf) { return walk_untagged(pdf, true); }

UntaggedCounts count_untagged_content(QPDF& pdf) { return walk_untagged(pdf, false); }

namespace {

// An internal link's destination: its name (a named destination) and the 1-based page it opens.
std::pair<std::string, int> link_destination(QPDF& pdf, QPDFObjectHandle annot) {
  QPDFObjectHandle dest = annot.getKey("/Dest");
  QPDFObjectHandle action = annot.getKey("/A");
  if (dest.isNull() && action.isDictionary() && action.getKey("/S").isNameAndEquals("/GoTo")) {
    dest = action.getKey("/D");
  }

  // A name is looked up in the catalog's /Dests dictionary (PDF 1.1), a string in the /Names /Dests
  // name tree (PDF 1.2+, possibly split into /Kids). Either value is the array or a dict with /D.
  std::string name;
  QPDFObjectHandle named;
  if (dest.isName()) {
    name = dest.getName().substr(1);
    QPDFObjectHandle dests = pdf.getRoot().getKey("/Dests");
    if (dests.isDictionary()) named = dests.getKey(dest.getName());
  } else if (dest.isString()) {
    name = dest.getUTF8Value();
    QPDFObjectHandle names = pdf.getRoot().getKey("/Names");
    QPDFObjectHandle tree = names.isDictionary() ? names.getKey("/Dests") : QPDFObjectHandle::newNull();
    if (tree.isDictionary() && !QPDFNameTreeObjectHelper(tree, pdf).findObject(name, named)) {
      named = QPDFObjectHandle::newNull();
    }
  }
  if (named.isDictionary()) named = named.getKey("/D");
  if (named.isArray()) dest = named;

  int page_number = 0;
  if (dest.isArray() && dest.getArrayNItems() > 0) {
    QPDFObjectHandle target = dest.getArrayItem(0);
    auto pages = pdf.getAllPages();
    for (size_t i = 0; i < pages.size(); ++i) {
      if (target.isIndirect() && pages[i].getObjGen() == target.getObjGen()) page_number = static_cast<int>(i) + 1;
    }
  }
  return {name, page_number};
}

}  // namespace

long describe_links(QPDF& pdf, std::map<std::string, std::string> const& texts) {
  QPDFObjectHandle tree = struct_tree(pdf);
  QPDFObjectHandle parent_tree = tree.isDictionary() ? tree.getKey("/ParentTree") : QPDFObjectHandle::newNull();
  long described = 0;

  for (QPDFObjectHandle page : pdf.getAllPages()) {
    QPDFObjectHandle annots = page.getKey("/Annots");
    if (!annots.isArray()) continue;
    for (auto annot : annots.aitems()) {
      if (!annot.isDictionary() || !annot.getKey("/Subtype").isNameAndEquals("/Link")) continue;
      QPDFObjectHandle contents = annot.getKey("/Contents");
      if (contents.isString() && !contents.getUTF8Value().empty()) continue;

      std::string uri;
      QPDFObjectHandle action = annot.getKey("/A");
      if (action.isDictionary() && action.getKey("/URI").isString()) uri = action.getKey("/URI").getUTF8Value();

      std::string text;
      if (auto it = texts.find(uri); !uri.empty() && it != texts.end()) text = it->second;

      if (text.empty() && parent_tree.isDictionary() && annot.getKey("/StructParent").isInteger()) {
        QPDFObjectHandle elem;
        QPDFNumberTreeObjectHelper numbers(parent_tree, pdf);
        if (numbers.findObject(annot.getKey("/StructParent").getIntValue(), elem) && elem.isDictionary()) {
          for (char const* key : {"/Alt", "/ActualText"}) {
            if (text.empty() && elem.getKey(key).isString()) text = elem.getKey(key).getUTF8Value();
          }
        }
      }
      if (text.empty() && uri.empty()) {
        auto [name, page_number] = link_destination(pdf, annot);
        if (auto it = texts.find("#" + name); !name.empty() && it != texts.end()) text = it->second;
        if (text.empty() && page_number > 0) text = "Page " + std::to_string(page_number);
      }
      if (text.empty()) text = uri.rfind("mailto:", 0) == 0 ? uri.substr(7) : uri;
      if (text.empty()) continue;

      annot.replaceKey("/Contents", QPDFObjectHandle::newUnicodeString(text));
      ++described;
    }
  }
  return described;
}

long wrap_list_bodies(QPDF& pdf) {
  QPDFObjectHandle tree = struct_tree(pdf);
  if (!tree.isDictionary()) return 0;
  QPDFObjectHandle parent_tree = tree.getKey("/ParentTree");
  long created = 0;

  std::vector<QPDFObjectHandle> items;
  each_struct_elem(pdf, [&](QPDFObjectHandle elem) {
    if (elem.getKey("/S").isNameAndEquals("/LI")) items.push_back(elem);
  });

  for (auto& item : items) {
    std::vector<QPDFObjectHandle> keep, body;
    for (auto& kid : kids_of(item)) {
      QPDFObjectHandle s = kid.isDictionary() ? kid.getKey("/S") : QPDFObjectHandle::newNull();
      if (s.isNameAndEquals("/Lbl") || s.isNameAndEquals("/LBody")) {
        keep.push_back(kid);
      } else {
        body.push_back(kid);
      }
    }
    if (body.empty()) continue;

    QPDFObjectHandle page = page_of(item);
    QPDFObjectHandle lbody = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
    lbody.replaceKey("/Type", QPDFObjectHandle::newName("/StructElem"));
    lbody.replaceKey("/S", QPDFObjectHandle::newName("/LBody"));
    lbody.replaceKey("/P", item);
    if (page.isDictionary()) lbody.replaceKey("/Pg", page);

    // Marked content and annotations moved into the LBody must name it as their parent.
    bool ok = true;
    std::vector<std::function<void()>> reparent;
    for (auto& kid : body) {
      if (kid.isDictionary() && kid.hasKey("/S")) {
        reparent.push_back([kid, lbody]() mutable { kid.replaceKey("/P", lbody); });
        continue;
      }
      if (!parent_tree.isDictionary()) {
        ok = false;
        break;
      }
      bool is_objr = kid.isDictionary() && kid.getKey("/Type").isNameAndEquals("/OBJR");
      if (is_objr) {
        QPDFObjectHandle object = kid.getKey("/Obj");
        QPDFObjectHandle key = object.isDictionary() ? object.getKey("/StructParent") : QPDFObjectHandle::newNull();
        if (!key.isInteger()) {
          ok = false;
          break;
        }
        reparent.push_back([&pdf, parent_tree, key, lbody]() mutable {
          QPDFNumberTreeObjectHelper(parent_tree, pdf).insert(key.getIntValue(), lbody);
        });
        continue;
      }
      // An MCID is numbered in its own content stream: a Form XObject's (/Stm) has its own
      // ParentTree entry, so the page's must not be touched for it.
      QPDFObjectHandle mcid = kid.isInteger() ? kid : kid.isDictionary() ? kid.getKey("/MCID") : QPDFObjectHandle::newNull();
      QPDFObjectHandle entries = parent_tree_entries(pdf, parent_tree, mcid_owner(kid, page));
      if (!mcid.isInteger() || !entries.isArray() || mcid.getIntValue() < 0 ||
          mcid.getIntValue() >= entries.getArrayNItems()) {
        ok = false;
        break;
      }
      int index = mcid.getIntValue();
      reparent.push_back([entries, index, lbody]() mutable { entries.setArrayItem(index, lbody); });
    }
    if (!ok) continue;  // leave an LI we cannot rewrite consistently as it is

    for (auto& apply : reparent) apply();
    lbody.replaceKey("/K", QPDFObjectHandle::newArray(body));
    keep.push_back(lbody);
    item.replaceKey("/K", QPDFObjectHandle::newArray(keep));
    ++created;
  }
  return created;
}

long count_parent_tree_mismatches(QPDF& pdf) {
  QPDFObjectHandle tree = struct_tree(pdf);
  if (!tree.isDictionary()) return 0;
  QPDFObjectHandle parent_tree = tree.getKey("/ParentTree");
  long mismatches = 0;

  each_struct_elem(pdf, [&](QPDFObjectHandle elem) {
    QPDFObjectHandle page = page_of(elem);
    for (auto& kid : kids_of(elem)) {
      if (kid.isDictionary() && kid.hasKey("/S")) continue;
      QPDFObjectHandle entry;
      if (kid.isDictionary() && kid.getKey("/Type").isNameAndEquals("/OBJR")) {
        QPDFObjectHandle object = kid.getKey("/Obj");
        QPDFObjectHandle key = object.isDictionary() ? object.getKey("/StructParent") : QPDFObjectHandle::newNull();
        if (!parent_tree.isDictionary() || !key.isInteger() ||
            !QPDFNumberTreeObjectHelper(parent_tree, pdf).findObject(key.getIntValue(), entry)) {
          entry = QPDFObjectHandle::newNull();
        }
      } else {
        QPDFObjectHandle mcid = kid.isInteger() ? kid : kid.isDictionary() ? kid.getKey("/MCID") : QPDFObjectHandle::newNull();
        if (!mcid.isInteger()) continue;
        QPDFObjectHandle entries = parent_tree_entries(pdf, parent_tree, mcid_owner(kid, page));
        long long index = mcid.getIntValue();
        entry = entries.isArray() && index >= 0 && index < entries.getArrayNItems()
                    ? entries.getArrayItem(static_cast<int>(index))
                    : QPDFObjectHandle::newNull();
      }
      if (!entry.isIndirect() || entry.getObjGen() != elem.getObjGen()) ++mismatches;
    }
  });
  return mismatches;
}

long map_nonstandard_roles(QPDF& pdf) {
  static const std::set<std::string> standard = {
      "/Document", "/Part",  "/Art",   "/Sect",     "/Div",      "/BlockQuote", "/Caption", "/TOC",     "/TOCI",
      "/Index",    "/NonStruct", "/Private", "/P",   "/H",        "/H1",         "/H2",      "/H3",      "/H4",
      "/H5",       "/H6",    "/L",     "/LI",       "/Lbl",      "/LBody",      "/Table",   "/TR",      "/TH",
      "/TD",       "/THead", "/TBody", "/TFoot",    "/Span",     "/Quote",      "/Note",    "/Reference", "/BibEntry",
      "/Code",     "/Link",  "/Annot", "/Ruby",     "/RB",       "/RT",         "/RP",      "/Warichu", "/WT",
      "/WP",       "/Figure", "/Formula", "/Form"};
  // PDF 2.0 types and their nearest PDF 1.7 equivalent.
  static const std::map<std::string, std::string> pdf2 = {
      {"/Aside", "/Sect"}, {"/Strong", "/Span"}, {"/Em", "/Span"},          {"/Title", "/P"},
      {"/Sub", "/Span"},   {"/FENote", "/Note"}, {"/DocumentFragment", "/Part"}};

  QPDFObjectHandle tree = struct_tree(pdf);
  if (!tree.isDictionary()) return 0;
  QPDFObjectHandle role_map = tree.getKey("/RoleMap");
  if (!role_map.isDictionary()) {
    role_map = QPDFObjectHandle::newDictionary();
    tree.replaceKey("/RoleMap", role_map);
    role_map = tree.getKey("/RoleMap");
  }

  long added = 0;
  each_struct_elem(pdf, [&](QPDFObjectHandle elem) {
    QPDFObjectHandle s = elem.getKey("/S");
    if (!s.isName()) return;
    std::string name = s.getName();
    if (standard.count(name) || role_map.hasKey(name)) return;
    auto it = pdf2.find(name);
    if (it == pdf2.end()) return;
    role_map.replaceKey(name, QPDFObjectHandle::newName(it->second));
    ++added;
  });
  return added;
}

namespace {

bool has_alternative(QPDFObjectHandle elem) {
  for (char const* key : {"/Alt", "/ActualText"}) {
    QPDFObjectHandle value = elem.getKey(key);
    if (value.isString() && !value.getUTF8Value().empty()) return true;
  }
  return false;
}

bool has_figure_below(QPDFObjectHandle elem, int depth = 0) {
  if (depth > 256) return false;
  for (auto& kid : kids_of(elem)) {
    if (!kid.isDictionary() || !kid.hasKey("/S")) continue;
    if (kid.getKey("/S").isNameAndEquals("/Figure") || has_figure_below(kid, depth + 1)) return true;
  }
  return false;
}

}  // namespace

long retag_grouping_figures(QPDF& pdf) {
  std::vector<QPDFObjectHandle> groups;
  each_struct_elem(pdf, [&](QPDFObjectHandle elem) {
    if (elem.getKey("/S").isNameAndEquals("/Figure") && !has_alternative(elem) && has_figure_below(elem)) {
      groups.push_back(elem);
    }
  });
  for (auto& elem : groups) elem.replaceKey("/S", QPDFObjectHandle::newName("/Div"));
  return static_cast<long>(groups.size());
}

long count_figures_without_alt(QPDF& pdf) {
  long count = 0;
  each_struct_elem(pdf, [&](QPDFObjectHandle elem) {
    if (elem.getKey("/S").isNameAndEquals("/Figure") && !has_alternative(elem)) ++count;
  });
  return count;
}

namespace {

constexpr char kPdfuaNs[] = "http://www.aiim.org/pdfua/ns/id/";
constexpr char kDcNs[] = "http://purl.org/dc/elements/1.1/";

// Every prefix the packet binds to `uri` (xmlns:prefix="uri"), wherever it is declared.
std::vector<std::string> xmlns_prefixes(std::string const& xmp, std::string const& uri) {
  static const std::regex declaration(R"re(xmlns:([A-Za-z_][A-Za-z0-9_.-]*)\s*=\s*(?:"([^"]*)"|'([^']*)'))re");
  std::vector<std::string> prefixes;
  for (std::sregex_iterator it(xmp.begin(), xmp.end(), declaration), end; it != end; ++it) {
    if ((*it)[2].str() == uri || (*it)[3].str() == uri) prefixes.push_back((*it)[1].str());
  }
  return prefixes;
}

bool is_xml_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// True if the packet carries property `local` of namespace `uri`, as an element (<p:local ...>) or
// in the attribute shorthand XMP allows on rdf:Description (p:local="...") - under any prefix the
// packet binds to `uri`. Not a full XML parser: a match inside a comment or CDATA counts too.
bool has_property(std::string const& xmp, std::string const& uri, std::string const& local) {
  for (auto const& prefix : xmlns_prefixes(xmp, uri)) {
    std::string const name = prefix + ":" + local;
    for (size_t at = xmp.find(name); at != std::string::npos; at = xmp.find(name, at + 1)) {
      size_t after = at + name.size();
      char next = after < xmp.size() ? xmp[after] : '\0';
      bool element = at > 0 && xmp[at - 1] == '<' && (is_xml_space(next) || next == '>' || next == '/');
      size_t equals = after;
      while (equals < xmp.size() && is_xml_space(xmp[equals])) ++equals;
      bool attribute = at > 0 && is_xml_space(xmp[at - 1]) && equals < xmp.size() && xmp[equals] == '=';
      if (element || attribute) return true;
    }
  }
  return false;
}

// Sets a boolean key unless it already has that value; true if it changed anything.
bool ensure_true(QPDFObjectHandle dict, std::string const& key) {
  QPDFObjectHandle value = dict.getKey(key);
  if (value.isBool() && value.getBoolValue()) return false;
  dict.replaceKey(key, QPDFObjectHandle::newBool(true));
  return true;
}

// The catalog's dictionary under `key`, created if missing.
QPDFObjectHandle catalog_dict(QPDFObjectHandle root, std::string const& key) {
  if (!root.getKey(key).isDictionary()) root.replaceKey(key, QPDFObjectHandle::newDictionary());
  return root.getKey(key);
}

}  // namespace

namespace {

constexpr char kRdfNs[] = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";

// Where the packet's rdf:RDF element closes, and the prefix it uses for the RDF namespace - found
// the same way as has_property, so not a full XML parser either. npos if no bound prefix closes one
// (a default-namespace RDF element, or a packet that is not XMP at all).
std::pair<size_t, std::string> rdf_close(std::string const& xmp) {
  for (auto const& prefix : xmlns_prefixes(xmp, kRdfNs)) {
    std::string const tag = "</" + prefix + ":RDF";
    for (size_t at = xmp.rfind(tag); at != std::string::npos; at = at == 0 ? std::string::npos : xmp.rfind(tag, at - 1)) {
      size_t after = at + tag.size();
      while (after < xmp.size() && is_xml_space(xmp[after])) ++after;
      if (after < xmp.size() && xmp[after] == '>') return {at, prefix};
    }
  }
  return {std::string::npos, ""};
}

// The identification step, with the reason when the file could not be identified.
bool identify(QPDF& pdf, std::optional<std::string> const& title, std::optional<std::string>& unidentified) {
  QPDFObjectHandle root = pdf.getRoot();
  bool changed = ensure_true(catalog_dict(root, "/ViewerPreferences"), "/DisplayDocTitle");

  std::string doc_title;
  QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
  if (title) {
    doc_title = *title;
    if (!info.isDictionary()) {
      info = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
      pdf.getTrailer().replaceKey("/Info", info);
    }
    QPDFObjectHandle current = info.getKey("/Title");
    if (!current.isString() || current.getUTF8Value() != doc_title) {
      info.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(doc_title));
      changed = true;
    }
  } else if (info.isDictionary() && info.getKey("/Title").isString()) {
    doc_title = info.getKey("/Title").getUTF8Value();
  }

  // Marked and pdfuaid both claim a tagged file - false without a structure tree.
  if (!struct_tree(pdf).isDictionary()) {
    unidentified = "no structure tree: the file is not tagged";
    return changed;
  }
  changed = ensure_true(catalog_dict(root, "/MarkInfo"), "/Marked") || changed;

  std::string const part_ns = std::string(" xmlns:pdfuaid=\"") + kPdfuaNs + "\"";
  std::string const dc_ns = std::string(" xmlns:dc=\"") + kDcNs + "\"";
  auto description = [&](std::string const& rdf, bool part, bool with_title) {
    return "<" + rdf + ":Description " + rdf + ":about=\"\"" + (part ? part_ns : "") + (with_title ? dc_ns : "") +
           ">" + (part ? "<pdfuaid:part>1</pdfuaid:part>" : "") +
           (with_title ? "<dc:title><" + rdf + ":Alt><" + rdf + ":li xml:lang=\"x-default\">" + xml_escape(doc_title) +
                             "</" + rdf + ":li></" + rdf + ":Alt></dc:title>"
                       : "") +
           "</" + rdf + ":Description>\n";
  };

  // An existing packet keeps everything it has; only what is missing goes into one new
  // Description, the identification and the title each checked on their own.
  QPDFObjectHandle metadata = root.getKey("/Metadata");
  if (metadata.isStream()) {
    auto data = metadata.getStreamData(qpdf_dl_generalized);
    std::string xmp(reinterpret_cast<char const*>(data->getBuffer()), data->getSize());
    bool add_part = !has_property(xmp, kPdfuaNs, "part");
    bool add_title = !doc_title.empty() && !has_property(xmp, kDcNs, "title");
    if (!add_part && !add_title) return changed;

    auto [end, rdf] = rdf_close(xmp);
    if (end == std::string::npos) {
      // Replacing the packet would lose what it holds; leave it, and say so.
      unidentified = "existing XMP metadata has no closing RDF element with a namespace prefix";
      return changed;
    }
    xmp.insert(end, description(rdf, add_part, add_title));
    metadata.replaceStreamData(xmp, QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
    return true;
  }

  std::string xmp = "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
                    "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"" +
                    std::string(kRdfNs) + "\">\n" + description("rdf", true, !doc_title.empty()) +
                    "</rdf:RDF></x:xmpmeta>\n<?xpacket end=\"w\"?>";
  QPDFObjectHandle stream = QPDFObjectHandle::newStream(&pdf, xmp);
  stream.getDict().replaceKey("/Type", QPDFObjectHandle::newName("/Metadata"));
  stream.getDict().replaceKey("/Subtype", QPDFObjectHandle::newName("/XML"));
  root.replaceKey("/Metadata", stream);
  return true;
}

}  // namespace

bool add_pdfua_identification(QPDF& pdf, std::optional<std::string> const& title) {
  std::optional<std::string> unidentified;
  return identify(pdf, title, unidentified);
}

Report apply(QPDF& pdf, std::map<std::string, std::string> const& link_texts, std::optional<std::string> const& title) {
  Report report;
  report.artifacts = mark_untagged_content_as_artifacts(pdf);
  report.links = describe_links(pdf, link_texts);
  report.list_bodies = wrap_list_bodies(pdf);
  report.roles = map_nonstandard_roles(pdf);
  report.figure_groups = retag_grouping_figures(pdf);
  report.figures_without_alt = count_figures_without_alt(pdf);
  report.identified = identify(pdf, title, report.unidentified_reason);
  return report;
}

}  // namespace qpdf_ruby::pdfua
