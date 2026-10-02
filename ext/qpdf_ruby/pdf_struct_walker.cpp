#include "pdf_struct_walker.hpp"
#include "struct_node.hpp"

PDFStructWalker::PDFStructWalker(std::ostream& out, qpdf_ruby::McidBounds bounds)
    : out(out), mcid_bounds(std::move(bounds)) {}

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
  for (size_t i = 0; i < pages.size(); ++i) {
    // Map the page's object ID to its 1-based page number
    pageObjToNumMap[pages.at(i).getObjGen()] = static_cast<int>(i) + 1;
  }
}

const std::map<QPDFObjGen, int>& PDFStructWalker::getPageObjectMap() const { return pageObjToNumMap; }

std::array<double, 4> PDFStructWalker::getPageCropBoxFor(QPDFObjectHandle const& page_oh) const {
  // getCropBox() follows inheritance through the page tree and falls back to the MediaBox. Its
  // arguments would copy an inherited box into the page - a write in what is a read.
  QPDFObjectHandle crop = QPDFPageObjectHelper(page_oh).getCropBox();
  std::array<double, 4> r = {0, 0, 0, 0};
  if (crop.isArray() && crop.getArrayNItems() == 4) {
    for (int i = 0; i < 4; ++i) r[i] = crop.getArrayItem(i).getNumericValue();
  }
  return r;
}
