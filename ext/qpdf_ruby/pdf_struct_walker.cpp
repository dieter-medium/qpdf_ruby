#include "pdf_struct_walker.hpp"
#include "struct_node.hpp"

PDFStructWalker::PDFStructWalker(std::ostream& out, const std::unordered_map<int, std::array<double, 4>>& mcid2bbox)
    : out(out), mcid2bbox(const_cast<std::unordered_map<int, std::array<double, 4>>&>(mcid2bbox)) {}

std::string PDFStructWalker::get_structure_as_string(QPDFObjectHandle const& node) {
  std::unique_ptr<StructNode> structNode = StructNode::fromQPDF(node);

  return structNode->to_string(0, *this);
}

void PDFStructWalker::ensureLayoutBBox(QPDFObjectHandle const& node) {
  std::unique_ptr<StructNode> structNode = StructNode::fromQPDF(node);

  structNode->ensureLayoutBBox(*this);
}

void PDFStructWalker::buildPageObjectMap(QPDF& pdf) {
  pageObjToNumMap.clear();
  std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
  for (int i = 0; i < pages.size(); ++i) {
    // Map the page's object ID to its 1-based page number
    pageObjToNumMap[pages.at(i).getObjGen()] = i + 1;
  }
}

const std::map<QPDFObjGen, int>& PDFStructWalker::getPageObjectMap() const { return pageObjToNumMap; }

std::array<double, 4> PDFStructWalker::getPageCropBoxFor(QPDFObjectHandle const& page_oh) const {
  auto inherited = [](QPDFObjectHandle node, char const* key) -> QPDFObjectHandle {
    while (!node.isNull()) {
      if (auto val = node.getKey(key); !val.isNull()) return val;
      node = node.getKey("/Parent");
    }
    return QPDFObjectHandle();  // null ⇒ not found
  };

  QPDFObjectHandle crop = inherited(page_oh, "/CropBox");
  if (crop.isNull()) crop = inherited(page_oh, "/MediaBox");  // spec default

  std::array<double, 4> r;
  for (size_t i = 0; i < 4; ++i) r[i] = crop.getArrayItem(i).getNumericValue();

  return r;
}
