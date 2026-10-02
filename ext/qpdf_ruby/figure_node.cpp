#include "struct_node.hpp"

#include <optional>
#include <set>

namespace {

// The page a structure element's content lives on: its own /Pg or the nearest ancestor's.
QPDFObjectHandle page_of(QPDFObjectHandle elem) {
  for (int guard = 0; elem.isDictionary() && guard < 256; ++guard) {
    if (elem.getKey("/Pg").isDictionary()) return elem.getKey("/Pg");
    elem = elem.getKey("/P");
  }
  return QPDFObjectHandle::newNull();
}

// Unites the bounds of every piece of marked content below `elem`: direct MCIDs (in the content of
// the element's page), marked-content references (in their /Stm if given, else their own /Pg's
// content) and nested structure elements.
void collect_bounds(QPDFObjectHandle elem, qpdf_ruby::McidBounds const& bounds, std::optional<qpdf_ruby::Box>& out,
                    std::set<QPDFObjGen>& seen, int depth = 0) {
  if (depth > 256 || (elem.isIndirect() && !seen.insert(elem.getObjGen()).second)) return;
  QPDFObjectHandle page = page_of(elem);

  auto add = [&](QPDFObjectHandle on_page, QPDFObjectHandle owner, int mcid) {
    if (!on_page.isDictionary() || !(owner.isDictionary() || owner.isStream())) return;
    auto it = bounds.find({on_page.getObjGen(), owner.getObjGen(), mcid});
    if (it == bounds.end()) return;
    out = out ? qpdf_ruby::unite(*out, it->second) : it->second;
  };

  QPDFObjectHandle kids = elem.getKey("/K");
  std::vector<QPDFObjectHandle> list;
  if (kids.isArray()) {
    for (auto kid : kids.aitems()) list.push_back(kid);
  } else if (!kids.isNull()) {
    list.push_back(kids);
  }

  for (auto& kid : list) {
    if (kid.isInteger()) {
      add(page, page, kid.getIntValue());
    } else if (kid.isDictionary() && kid.getKey("/Type").isNameAndEquals("/MCR") && kid.getKey("/MCID").isInteger()) {
      QPDFObjectHandle on_page = kid.getKey("/Pg").isDictionary() ? kid.getKey("/Pg") : page;
      // A /Stm that is not a stream names nothing we can look up: no bounds rather than the page's.
      QPDFObjectHandle owner = kid.hasKey("/Stm") ? kid.getKey("/Stm") : on_page;
      if (kid.hasKey("/Stm") && !owner.isStream()) continue;
      add(on_page, owner, kid.getKey("/MCID").getIntValue());
    } else if (kid.isDictionary() && kid.hasKey("/S")) {
      collect_bounds(kid, bounds, out, seen, depth + 1);
    }
  }
}

bool is_layout(QPDFObjectHandle attrs) {
  return attrs.isDictionary() && attrs.getKey("/O").isNameAndEquals("/Layout");
}

// The element's Layout attribute dictionary - /A itself, or the first one in an /A array - or null.
QPDFObjectHandle layout_of(QPDFObjectHandle attrs) {
  if (is_layout(attrs)) return attrs;
  if (attrs.isArray()) {
    for (auto item : attrs.aitems()) {
      if (is_layout(item)) return item;
    }
  }
  return QPDFObjectHandle::newNull();
}

// Structure types whose content is a line of text: a Figure among them is part of that line.
const std::set<std::string> kInlineParents = {"/P",    "/H",        "/H1",      "/H2",  "/H3",  "/H4",   "/H5",
                                              "/H6",   "/Lbl",      "/Span",    "/Quote", "/Note", "/Reference",
                                              "/BibEntry", "/Code", "/Link",    "/Annot", "/Ruby", "/Warichu",
                                              "/Strong", "/Em"};

// True if the Figure sits in a line of text - its nearest parent that is not a NonStruct (which
// only groups, ISO 32000-1 14.8.4.2) is one of kInlineParents.
bool in_inline_context(QPDFObjectHandle elem) {
  QPDFObjectHandle parent = elem.getKey("/P");
  for (int guard = 0; guard < 64 && parent.isDictionary(); ++guard) {
    if (!parent.getKey("/S").isNameAndEquals("/NonStruct")) break;
    parent = parent.getKey("/P");
  }
  QPDFObjectHandle type = parent.isDictionary() ? parent.getKey("/S") : QPDFObjectHandle::newNull();
  return type.isName() && kInlineParents.count(type.getName()) > 0;
}

QPDFObjectHandle box_array(qpdf_ruby::Box const& box) {
  QPDFObjectHandle array = QPDFObjectHandle::newArray();
  for (double value : box) array.appendItem(QPDFObjectHandle::newReal(value, 4));
  return array;
}

}  // namespace

// Gives the Figure a Layout attribute with a /BBox (its painted bounds) and, outside a line of text,
// /Placement /Block - without the placement PAC 2024 warns of a "possibly inappropriate use" of the
// Figure (checked 2026-10-02). Existing values are kept; an existing attribute object is extended.
void FigureNode::ensureLayoutBBox(PDFStructWalker& walker) {
  StructElemNode::ensureLayoutBBox(walker);

  QPDFObjectHandle attrs = node.getKey("/A");
  QPDFObjectHandle layout = layout_of(attrs);
  bool needs_bbox = !(layout.isDictionary() && layout.hasKey("/BBox"));
  bool needs_placement = !(layout.isDictionary() && layout.hasKey("/Placement")) && !in_inline_context(node);
  if (!needs_bbox && !needs_placement) return;

  std::optional<qpdf_ruby::Box> box;
  if (needs_bbox) {
    std::set<QPDFObjGen> seen;
    collect_bounds(node, walker.getMcidBounds(), box, seen);
    QPDFObjectHandle page = page_of(node);
    if (!box && page.isDictionary()) box = walker.getPageCropBoxFor(page);  // else nothing to anchor it to
  }
  if (!box && !needs_placement) return;

  bool attached = layout.isDictionary();
  if (!attached) {
    layout = QPDFObjectHandle::newDictionary();
    layout.replaceKey("/O", QPDFObjectHandle::newName("/Layout"));
  }
  if (box) layout.replaceKey("/BBox", box_array(*box));
  if (needs_placement) layout.replaceKey("/Placement", QPDFObjectHandle::newName("/Block"));
  if (attached) return;

  // Keep every existing attribute object: append the new Layout dictionary to them.
  if (attrs.isArray()) {
    attrs.appendItem(layout);
  } else if (attrs.isDictionary()) {
    node.replaceKey("/A", QPDFObjectHandle::newArray({attrs, layout}));
  } else {
    node.replaceKey("/A", layout);
  }
}
