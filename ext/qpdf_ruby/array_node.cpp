#include "struct_node.hpp"

void ArrayNode::addChild(std::unique_ptr<StructNode> child) { children.push_back(std::move(child)); }

std::string ArrayNode::to_string(int level, PDFStructWalker& walker) {
  std::ostringstream oss;
  for (const auto& child : children) {
    oss << child->to_string(level, walker);
  }
  return oss.str();
}

void ArrayNode::ensureLayoutBBox(PDFStructWalker& walker) {
  for (const auto& child : children) {
    child->ensureLayoutBBox(walker);
  }
}