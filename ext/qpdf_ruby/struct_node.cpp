#include "struct_node.hpp"

std::unique_ptr<StructNode> StructNode::fromQPDF(QPDFObjectHandle node) {
  if (node.isInteger()) {
    return std::make_unique<McidNode>(node.getIntValue());
  }

  if (node.isArray()) {
    auto arrayNode = std::make_unique<ArrayNode>();
    for (int i = 0; i < node.getArrayNItems(); ++i) {
      arrayNode->addChild(fromQPDF(node.getArrayItem(i)));
    }
    return arrayNode;
  }

  if (node.isDictionary()) {
    if (node.hasKey("/Type") && node.getKey("/Type").isName() && node.getKey("/Type").getName() == "MCR" &&
        node.hasKey("/MCID") && node.hasKey("/Pg")) {
      int mcid = node.getKey("/MCID").getIntValue();
      QPDFObjectHandle pg = node.getKey("/Pg");
      auto og = pg.getObjGen();
      // Optionally, pass page number if you can look it up here
      return std::make_unique<McrNode>(mcid, og.getObj(), og.getGen());
    }

    if (node.hasKey("/S") && node.getKey("/S").isName()) {
      std::string tag = node.getKey("/S").getName();
      if (tag == "/Figure") {
        return std::make_unique<FigureNode>(node);
      }
    }

    if (node.hasKey("/S") ||
        (node.hasKey("/Type") && node.getKey("/Type").isName() && node.getKey("/Type").getName() == "StructElem")) {
      return std::make_unique<StructElemNode>(node);
    }
    // Handle other dictionary types as needed
    return std::make_unique<UnknownNode>("Dictionary");
  }

  if (node.isStream()) {
    auto data = node.getStreamData();
    return std::make_unique<StreamNode>(data->getSize());
  }

  return std::make_unique<UnknownNode>(node.getTypeName());
}

void StructNode::print(std::ostream& out, int level, PDFStructWalker& walker) { out << this->to_string(level, walker); }

void StructNode::ensureLayoutBBox(PDFStructWalker& walker) {}