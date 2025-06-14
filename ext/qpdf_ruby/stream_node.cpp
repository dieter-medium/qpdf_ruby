#include "struct_node.hpp"

std::string StreamNode::to_string(int level, PDFStructWalker& walker) {
  std::ostringstream oss;
  IndentHelper::indent(oss, level);
  oss << "[Stream: length=" << size;

  oss << "]" << std::endl;
  return oss.str();
}