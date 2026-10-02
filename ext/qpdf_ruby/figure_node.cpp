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

bool is_layout_with_bbox(QPDFObjectHandle attrs) {
  return attrs.isDictionary() && attrs.getKey("/O").isNameAndEquals("/Layout") && attrs.hasKey("/BBox");
}

QPDFObjectHandle box_array(qpdf_ruby::Box const& box) {
  QPDFObjectHandle array = QPDFObjectHandle::newArray();
  for (double value : box) array.appendItem(QPDFObjectHandle::newReal(value, 4));
  return array;
}

}  // namespace

void FigureNode::ensureLayoutBBox(PDFStructWalker& walker) {
  StructElemNode::ensureLayoutBBox(walker);

  QPDFObjectHandle attrs = node.getKey("/A");
  if (is_layout_with_bbox(attrs)) return;
  if (attrs.isArray()) {
    for (auto item : attrs.aitems()) {
      if (is_layout_with_bbox(item)) return;
    }
  }

  std::optional<qpdf_ruby::Box> box;
  std::set<QPDFObjGen> seen;
  collect_bounds(node, walker.getMcidBounds(), box, seen);
  if (!box) {
    QPDFObjectHandle page = page_of(node);
    if (!page.isDictionary()) return;  // nothing to anchor a BBox to
    box = walker.getPageCropBoxFor(page);
  }

  // Keep every existing attribute object: add /BBox to a Layout dictionary, else append one.
  if (attrs.isDictionary() && attrs.getKey("/O").isNameAndEquals("/Layout")) {
    attrs.replaceKey("/BBox", box_array(*box));
    return;
  }
  QPDFObjectHandle layout = QPDFObjectHandle::newDictionary();
  layout.replaceKey("/O", QPDFObjectHandle::newName("/Layout"));
  layout.replaceKey("/BBox", box_array(*box));
  if (attrs.isArray()) {
    attrs.appendItem(layout);
  } else if (attrs.isDictionary()) {
    node.replaceKey("/A", QPDFObjectHandle::newArray({attrs, layout}));
  } else {
    node.replaceKey("/A", layout);
  }
}
