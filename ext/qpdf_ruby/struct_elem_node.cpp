#include "struct_node.hpp"

void StructElemNode::ensureLayoutBBox(PDFStructWalker& walker) {
  if (node.hasKey("/K")) {
    QPDFObjectHandle kids = node.getKey("/K");

    // Handle direct value (single child)
    if (kids.isInteger() || kids.isDictionary() || kids.isStream()) {
      std::unique_ptr<StructNode> childNode = StructNode::fromQPDF(kids);
      childNode->ensureLayoutBBox(walker);
    }
    // Handle array of children
    else if (kids.isArray()) {
      for (int i = 0; i < kids.getArrayNItems(); ++i) {
        QPDFObjectHandle kid = kids.getArrayItem(i);
        std::unique_ptr<StructNode> childNode = StructNode::fromQPDF(kid);
        childNode->ensureLayoutBBox(walker);
      }
    }
  }
}

std::string StructElemNode::to_string(int level, PDFStructWalker& walker) {
  std::ostringstream oss;
  std::string tag = getStructureTag();
  int pageNum = findPageNumber(walker);

  // Print opening tag with attributes
  printOpeningTag(oss, level, tag, pageNum, walker);

  // Process children
  processChildren(oss, level + 1, walker);

  // Print closing tag
  IndentHelper::indent(oss, level);
  oss << "</" << tag << ">" << std::endl;

  return oss.str();
}

std::string StructElemNode::getStructureTag() {
  std::string tag = "Unknown";
  if (node.hasKey("/S") && node.getKey("/S").isName()) {
    std::string rawName = node.getKey("/S").getName();
    tag = (rawName[0] == '/') ? rawName.substr(1) : rawName;
  }
  return tag;
}

int StructElemNode::findPageNumber(PDFStructWalker& walker) {
  int pageNum = -1;
  QPDFObjectHandle currentNode = this->node;

  while (currentNode.isDictionary()) {
    if (currentNode.hasKey("/Pg")) {
      QPDFObjectHandle page_ref = currentNode.getKey("/Pg");
      if (page_ref.isIndirect()) {
        const auto& page_map = walker.getPageObjectMap();
        auto og = page_ref.getObjGen();
        if (page_map.count(og)) {
          return page_map.at(og);
        }
      }
    }

    // Move to parent
    if (currentNode.hasKey("/P")) {
      currentNode = currentNode.getKey("/P");
    } else {
      break;
    }
  }
  return pageNum;
}

void StructElemNode::printOpeningTag(std::ostringstream& oss, int level, const std::string& tag, int pageNum,
                                     PDFStructWalker& walker) {
  IndentHelper::indent(oss, level);
  oss << "<" << tag;

  // Object ID
  if (node.isIndirect()) {
    oss << " obj=\"" << node.getObjectID() << " " << node.getGeneration() << "\"";
  }

  addAttributeIfPresent(oss, "/Alt", "Alt");
  addAttributeIfPresent(oss, "/ActualText", "ActualText");
  addAttributeIfPresent(oss, "/T", "Title");
  addAttributeIfPresent(oss, "/Lang", "Lang");
  addAttributeIfPresent(oss, "/ID", "ID");

  addClassAttribute(oss);
  addBboxAttribute(oss);

  // Artifact type
  if (tag == "Artifact" && node.hasKey("/Type") && node.getKey("/Type").isName()) {
    std::string artifactType = node.getKey("/Type").getName();
    oss << " ArtifactType=\"" << (artifactType[0] == '/' ? artifactType.substr(1) : artifactType) << "\"";
  }

  addNamespaceAttribute(oss);
  addAttributeIfPresent(oss, "/Type", "Type");

  if (pageNum > 0) {
    oss << " Page=\"" << pageNum << "\"";
  }

  oss << ">" << std::endl;
}

void StructElemNode::addAttributeIfPresent(std::ostringstream& oss, const std::string& key,
                                           const std::string& attributeName) {
  if (node.hasKey(key) && node.getKey(key).isString()) {
    oss << " " << attributeName << "=\"" << node.getKey(key).getStringValue() << "\"";
  }
}

void StructElemNode::addClassAttribute(std::ostringstream& oss) {
  if (!node.hasKey("/C")) return;

  oss << " Class=\"";
  QPDFObjectHandle classObj = node.getKey("/C");
  if (classObj.isName()) {
    std::string className = classObj.getName();
    oss << (className[0] == '/' ? className.substr(1) : className);
  } else if (classObj.isArray()) {
    for (int i = 0; i < classObj.getArrayNItems(); ++i) {
      if (i > 0) oss << " ";
      if (classObj.getArrayItem(i).isName()) {
        std::string className = classObj.getArrayItem(i).getName();
        oss << (className[0] == '/' ? className.substr(1) : className);
      }
    }
  }
  oss << "\"";
}

void StructElemNode::processChildren(std::ostringstream& oss, int level, PDFStructWalker& walker) {
  if (!node.hasKey("/K")) return;

  QPDFObjectHandle kids = node.getKey("/K");

  // Handle direct value (single child)
  if (!kids.isArray()) {
    std::unique_ptr<StructNode> childNode = StructNode::fromQPDF(kids);
    oss << childNode->to_string(level, walker);
    return;
  }

  // Handle array of children
  for (int i = 0; i < kids.getArrayNItems(); ++i) {
    QPDFObjectHandle kid = kids.getArrayItem(i);
    std::unique_ptr<StructNode> childNode = StructNode::fromQPDF(kid);
    oss << childNode->to_string(level, walker);
  }
}

void StructElemNode::addBboxAttribute(std::ostringstream& oss) {
  if (node.hasKey("/BBox") && node.getKey("/BBox").isArray()) {
    QPDFObjectHandle bbox = node.getKey("/BBox");
    if (bbox.getArrayNItems() == 4) {
      oss << " BBox=\"";
      for (int i = 0; i < 4; ++i) {
        if (i > 0) oss << " ";
        if (bbox.getArrayItem(i).isNumber()) {
          oss << bbox.getArrayItem(i).getNumericValue();
        }
      }
      oss << "\"";
    }
  }

  if (node.hasKey("/A")) {
    QPDFObjectHandle A = node.getKey("/A");
    if (A.isDictionary() && A.hasKey("/BBox")) {
      auto bbox = A.getKey("/BBox");
      oss << " BBox=\"[" << bbox.getArrayItem(0).getNumericValue() << ", " << bbox.getArrayItem(1).getNumericValue()
          << ", " << bbox.getArrayItem(2).getNumericValue() << ", " << bbox.getArrayItem(3).getNumericValue() << "]\"";
    } else if (A.isArray()) {
      for (int i = 0; i < A.getArrayNItems(); ++i) {
        if (A.getArrayItem(i).isDictionary() && A.getArrayItem(i).hasKey("/O") &&
            A.getArrayItem(i).getKey("/O").getName() == "/Layout" && A.getArrayItem(i).hasKey("/BBox")) {
          auto bbox = A.getArrayItem(i).getKey("/BBox");
          oss << " BBox=\"[" << bbox.getArrayItem(0).getNumericValue() << ", " << bbox.getArrayItem(1).getNumericValue()
              << ", " << bbox.getArrayItem(2).getNumericValue() << ", " << bbox.getArrayItem(3).getNumericValue()
              << "]\"";
          break;
        }
      }
    }
    oss << " ";
  }
}

void StructElemNode::addNamespaceAttribute(std::ostringstream& oss) {
  if (node.hasKey("/NS") && node.getKey("/NS").isDictionary()) {
    QPDFObjectHandle ns = node.getKey("/NS");
    if (ns.hasKey("/NS") && ns.getKey("/NS").isName()) {
      std::string nsName = ns.getKey("/NS").getName();
      oss << " NS=\"" << (nsName[0] == '/' ? nsName.substr(1) : nsName) << "\"";
    }
  }
}