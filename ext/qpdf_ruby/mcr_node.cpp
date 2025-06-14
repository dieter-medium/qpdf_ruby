#include "struct_node.hpp"

McrNode::McrNode(int mcid, int pageObj, int pageGen, int pageNumber)
    : mcid(mcid), pageObj(pageObj), pageGen(pageGen), pageNumber(pageNumber) {}

std::string McrNode::to_string(int level, PDFStructWalker& walker) {
  std::ostringstream oss;

  IndentHelper::indent(oss, level);
  oss << "[MCR: MCID=" << mcid << " PageObj=" << pageObj << " Gen=" << pageGen;
  if (pageNumber > 0) {
    oss << " PageNumber=" << pageNumber;
  }
  oss << "]" << std::endl;

  return oss.str();
}

int McrNode::getMcid() const { return mcid; }
void McrNode::setPageNumber(int pageNum) { pageNumber = pageNum; }