#include "pdfua.hpp"

#include <qpdf/Pl_Buffer.hh>
#include <qpdf/QPDFNumberTreeObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFTokenizer.hh>

#include <functional>
#include <memory>
#include <set>
#include <vector>

namespace qpdf_ruby::pdfua {

namespace {

const std::set<std::string> kPathConstruction = {"m", "l", "c", "v", "y", "h", "re"};
const std::set<std::string> kClip = {"W", "W*"};
const std::set<std::string> kPathEnd = {"S", "s", "f", "F", "f*", "B", "B*", "b", "b*", "n"};
const std::set<std::string> kTextShow = {"Tj", "TJ", "'", "\""};

// True if a Form XObject (or anything it draws) carries marked content of its own.
bool form_has_marked_content(QPDFObjectHandle xobject, std::set<QPDFObjGen>& seen) {
  if (!xobject.isStream() || !seen.insert(xobject.getObjGen()).second) return false;
  QPDFObjectHandle dict = xobject.getDict();
  if (!dict.getKey("/Subtype").isNameAndEquals("/Form")) return false;
  if (dict.hasKey("/StructParents") || dict.hasKey("/StructParent")) return true;
  auto data = xobject.getStreamData(qpdf_dl_generalized);
  std::string text(reinterpret_cast<char const*>(data->getBuffer()), data->getSize());
  if (text.find("BDC") != std::string::npos || text.find("BMC") != std::string::npos) return true;
  QPDFObjectHandle resources = dict.getKey("/Resources");
  QPDFObjectHandle xobjects = resources.isDictionary() ? resources.getKey("/XObject") : QPDFObjectHandle::newNull();
  if (xobjects.isDictionary()) {
    for (auto const& [name, child] : xobjects.ditems()) {
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
    for (auto const& [name, xobject] : xobjects.ditems()) {
      std::set<QPDFObjGen> seen;
      if (form_has_marked_content(xobject, seen)) add_counts(counts, filter_contents(pdf, xobject, rewrite, visited));
    }
  }
  return counts;
}

UntaggedCounts walk_untagged(QPDF& pdf, bool rewrite) {
  UntaggedCounts counts;
  std::set<QPDFObjGen> visited;
  for (auto& page : pdf.getAllPages()) add_counts(counts, filter_contents(pdf, page, rewrite, visited));
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
    for (auto const& kid : k.aitems()) kids.push_back(kid);
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

  std::string name;
  if (dest.isName()) name = dest.getName().substr(1);
  if (dest.isString()) name = dest.getUTF8Value();
  if (!name.empty()) {
    QPDFObjectHandle named = pdf.getRoot().getKey("/Dests").getKey("/" + name);
    if (named.isDictionary()) named = named.getKey("/D");
    if (named.isArray()) dest = named;
  }

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

  for (auto& page : pdf.getAllPages()) {
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
        QPDFObjectHandle key = kid.getKey("/Obj").getKey("/StructParent");
        if (!key.isInteger()) {
          ok = false;
          break;
        }
        reparent.push_back([&pdf, parent_tree, key, lbody]() {
          QPDFNumberTreeObjectHelper(parent_tree, pdf).insert(key.getIntValue(), lbody);
        });
        continue;
      }
      QPDFObjectHandle mcid = kid.isInteger() ? kid : kid.getKey("/MCID");
      QPDFObjectHandle kid_page = kid.isDictionary() && kid.getKey("/Pg").isDictionary() ? kid.getKey("/Pg") : page;
      QPDFObjectHandle struct_parents =
          kid_page.isDictionary() ? kid_page.getKey("/StructParents") : QPDFObjectHandle::newNull();
      QPDFObjectHandle entries;
      if (!mcid.isInteger() || !struct_parents.isInteger() ||
          !QPDFNumberTreeObjectHelper(parent_tree, pdf).findObject(struct_parents.getIntValue(), entries) ||
          !entries.isArray() || mcid.getIntValue() >= entries.getArrayNItems()) {
        ok = false;
        break;
      }
      reparent.push_back([entries, mcid, lbody]() mutable { entries.setArrayItem(mcid.getIntValue(), lbody); });
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

bool add_pdfua_identification(QPDF& pdf, std::optional<std::string> const& title) {
  QPDFObjectHandle root = pdf.getRoot();

  QPDFObjectHandle prefs = root.getKey("/ViewerPreferences");
  if (!prefs.isDictionary()) {
    root.replaceKey("/ViewerPreferences", QPDFObjectHandle::newDictionary());
    prefs = root.getKey("/ViewerPreferences");
  }
  prefs.replaceKey("/DisplayDocTitle", QPDFObjectHandle::newBool(true));

  QPDFObjectHandle mark_info = root.getKey("/MarkInfo");
  if (!mark_info.isDictionary()) {
    root.replaceKey("/MarkInfo", QPDFObjectHandle::newDictionary());
    mark_info = root.getKey("/MarkInfo");
  }
  mark_info.replaceKey("/Marked", QPDFObjectHandle::newBool(true));

  std::string doc_title;
  QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
  if (title) {
    doc_title = *title;
    if (!info.isDictionary()) {
      info = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
      pdf.getTrailer().replaceKey("/Info", info);
    }
    info.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(doc_title));
  } else if (info.isDictionary() && info.getKey("/Title").isString()) {
    doc_title = info.getKey("/Title").getUTF8Value();
  }

  static const std::string ns = "xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\"";
  std::string title_xml =
      "<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">" + xml_escape(doc_title) + "</rdf:li></rdf:Alt></dc:title>";

  QPDFObjectHandle metadata = root.getKey("/Metadata");
  if (metadata.isStream()) {
    auto data = metadata.getStreamData(qpdf_dl_generalized);
    std::string xmp(reinterpret_cast<char const*>(data->getBuffer()), data->getSize());
    if (xmp.find("pdfuaid:part") != std::string::npos) return false;
    auto end = xmp.find("</rdf:RDF>");
    if (end == std::string::npos) throw std::runtime_error("existing XMP metadata has no rdf:RDF element");
    std::string description = "<rdf:Description rdf:about=\"\" " + ns +
                              " xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><pdfuaid:part>1</pdfuaid:part>" +
                              (xmp.find("dc:title") == std::string::npos && !doc_title.empty() ? title_xml : "") +
                              "</rdf:Description>\n";
    xmp.insert(end, description);
    metadata.replaceStreamData(xmp, QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
    return true;
  }

  std::string xmp =
      "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
      "<rdf:Description rdf:about=\"\" " + ns + " xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
      "<pdfuaid:part>1</pdfuaid:part>\n" + (doc_title.empty() ? "" : title_xml + "\n") +
      "</rdf:Description></rdf:RDF></x:xmpmeta>\n<?xpacket end=\"w\"?>";
  QPDFObjectHandle stream = QPDFObjectHandle::newStream(&pdf, xmp);
  stream.getDict().replaceKey("/Type", QPDFObjectHandle::newName("/Metadata"));
  stream.getDict().replaceKey("/Subtype", QPDFObjectHandle::newName("/XML"));
  root.replaceKey("/Metadata", stream);
  return true;
}

Report apply(QPDF& pdf, std::map<std::string, std::string> const& link_texts, std::optional<std::string> const& title) {
  Report report;
  report.artifacts = mark_untagged_content_as_artifacts(pdf);
  report.links = describe_links(pdf, link_texts);
  report.list_bodies = wrap_list_bodies(pdf);
  report.roles = map_nonstandard_roles(pdf);
  report.figure_groups = retag_grouping_figures(pdf);
  report.figures_without_alt = count_figures_without_alt(pdf);
  report.identified = add_pdfua_identification(pdf, title);
  return report;
}

}  // namespace qpdf_ruby::pdfua
