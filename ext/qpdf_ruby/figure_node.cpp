#include "struct_node.hpp"

static void add_bbox(PDFStructWalker& walker, QPDFObjectHandle node) {
  int mcid = -std::numeric_limits<int>::infinity();

  if (node.getKey("/K").isInteger()) {
    mcid = node.getKey("/K").getIntValue();
  }

  // Get the bbox from the walker or use defaults

  QPDFObjectHandle pageObj;
  QPDFObjectHandle currentNode = node;
  bool found = false;

  // Try to find /Pg by walking up the parent chain
  while (currentNode.isDictionary()) {
    if (currentNode.hasKey("/Pg")) {
      pageObj = currentNode.getKey("/Pg");
      found = true;
      break;
    }

    if (currentNode.hasKey("/P")) {
      currentNode = currentNode.getKey("/P");
    } else {
      break;  // No more parents to check
    }
  }

  if (!found && node.hasKey("/K") && node.getKey("/K").isArray()) {
    QPDFObjectHandle kids = node.getKey("/K");
    for (int i = 0; i < kids.getArrayNItems(); ++i) {
      QPDFObjectHandle kid = kids.getArrayItem(i);
      if (kid.isDictionary() && kid.hasKey("/Pg")) {
        pageObj = kid.getKey("/Pg");
        found = true;
        break;
      }
    }
  }

  if (!found || !pageObj.isIndirect()) {
    std::cerr << "No /Pg key found for MCID " << mcid << ", cannot add BBox." << std::endl;
    return;
  }

  auto media_box = walker.getPageCropBoxFor(pageObj);

  double llx = media_box[0], lly = media_box[1], urx = media_box[2], ury = media_box[3];
  auto const& bbox_map = walker.getMcidBboxMap();

  // Use a try/catch to prevent failures when accessing the map
  try {
    if (bbox_map.count(mcid) > 0) {
      auto const& b = bbox_map.at(mcid);
      llx = b[0];
      lly = b[1];
      urx = b[2];
      ury = b[3];
    }
  } catch (const std::exception& e) {
    std::cerr << "Error accessing mcid " << mcid << " in bbox map: " << e.what() << std::endl;
    // Continue with default values
  }

  QPDFObjectHandle attrs;
  attrs = QPDFObjectHandle::newDictionary();
  attrs.replaceKey("/O", QPDFObjectHandle::newName("/Layout"));

  node.replaceKey("/A", attrs);

  if (!attrs.hasKey("/BBox")) {
    QPDFObjectHandle arr = QPDFObjectHandle::newArray();
    arr.appendItem(QPDFObjectHandle::newReal(llx));
    arr.appendItem(QPDFObjectHandle::newReal(lly));
    arr.appendItem(QPDFObjectHandle::newReal(urx));
    arr.appendItem(QPDFObjectHandle::newReal(ury));

    attrs.replaceKey("/BBox", arr);
  }
}

void FigureNode::ensureLayoutBBox(PDFStructWalker& walker) {
  StructElemNode::ensureLayoutBBox(walker);

  // Bail if a BBox is already present *anywhere* in /A
  if (node.hasKey("/A")) {
    QPDFObjectHandle A = node.getKey("/A");
    auto has_bbox = [](QPDFObjectHandle const& dict) {
      QPDFObjectHandle non_const_dict = dict;
      return non_const_dict.isDictionary() && non_const_dict.hasKey("/O") &&
             non_const_dict.getKey("/O").getName() == "/Layout" && non_const_dict.hasKey("/BBox");
    };

    if ((A.isDictionary() && has_bbox(A)) || (A.isArray() && [&]() -> bool {
          for (int i = 0; i < A.getArrayNItems(); ++i) {
            if (has_bbox(A.getArrayItem(i))) {
              return true;
            }
          }
          return false;
        }())) {
      return;  // nothing to do
    }
  }

  add_bbox(walker, node);
}