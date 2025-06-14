#include "struct_node.hpp"

std::string UnknownNode::to_string(int level, PDFStructWalker& walker) {
  std::ostringstream oss;

  IndentHelper::indent(oss, level);
  oss << "[Unhandled type: " << typeName << "]" << std::endl;

  return oss.str();
}