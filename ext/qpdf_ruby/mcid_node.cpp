#include "struct_node.hpp"

int McidNode::getMcid() const { return mcid; }

void McidNode::setPage(int page) { pageNumber = page; }

std::string McidNode::to_string(int level, PDFStructWalker& walker) {
  std::ostringstream oss;
  IndentHelper::indent(oss, level);
  oss << "[MCID: " << mcid;

  oss << "]" << std::endl;

  return oss.str();
}