#pragma once

#define POINTERHOLDER_TRANSITION 1

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <iostream>
#include <stdexcept>  // For std::exception (if you add try-catch)
#include <vector>     // For std::vector
#include <string>     // For std::string
#include <regex>
#include <map>

#include "pdf_struct_walker.hpp"

class StructNode {
 public:
  virtual ~StructNode() = default;
  virtual std::string to_string(int level, PDFStructWalker& walker) = 0;
  void print(std::ostream& out, int level, PDFStructWalker& walker);
  virtual void ensureLayoutBBox(PDFStructWalker& walker);

  static std::unique_ptr<StructNode> fromQPDF(QPDFObjectHandle node);
};

class StructElemNode : public StructNode {
 protected:
  QPDFObjectHandle node;

 public:
  StructElemNode(QPDFObjectHandle n) : node(n) {}
  std::string to_string(int level, PDFStructWalker& walker) override;
  void ensureLayoutBBox(PDFStructWalker& walker) override;

 private:
  std::string getStructureTag();
  int findPageNumber(PDFStructWalker& walker);
  void printOpeningTag(std::ostringstream& oss, int level, const std::string& tag, int pageNum,
                       PDFStructWalker& walker);
  void addAttributeIfPresent(std::ostringstream& oss, const std::string& key, const std::string& attributeName);
  void addClassAttribute(std::ostringstream& oss);
  void addBboxAttribute(std::ostringstream& oss);
  void addNamespaceAttribute(std::ostringstream& oss);
  void processChildren(std::ostringstream& oss, int level, PDFStructWalker& walker);
};

class FigureNode : public StructElemNode {
 public:
  FigureNode(QPDFObjectHandle n) : StructElemNode(n) {}
  void ensureLayoutBBox(PDFStructWalker& walker) override;
};

class McidNode : public StructNode {
 private:
  int mcid;
  int pageNumber = -1;  // Default to -1 (unknown)
 public:
  McidNode(int id) : mcid(id) {}
  std::string to_string(int level, PDFStructWalker& walker) override;
  int getMcid() const;     // Declaration only
  void setPage(int page);  // Declaration only
};

class McrNode : public StructNode {
 public:
  McrNode(int mcid, int pageObj, int pageGen, int pageNumber = 0);
  std::string to_string(int level, PDFStructWalker& walker) override;
  int getMcid() const;
  void setPageNumber(int pageNum);

 private:
  int mcid;
  int pageObj;
  int pageGen;
  int pageNumber;
};

class ArrayNode : public StructNode {
 private:
  std::vector<std::unique_ptr<StructNode>> children;

 public:
  ArrayNode() = default;
  void addChild(std::unique_ptr<StructNode> child);
  std::string to_string(int level, PDFStructWalker& walker) override;
  void ensureLayoutBBox(PDFStructWalker& walker) override;
};

class StreamNode : public StructNode {
 private:
  size_t size;

 public:
  StreamNode(size_t streamSize) : size(streamSize) {}
  std::string to_string(int level, PDFStructWalker& walker) override;
};

class UnknownNode : public StructNode {
 private:
  std::string typeName;

 public:
  UnknownNode(const std::string& type) : typeName(type) {}
  std::string to_string(int level, PDFStructWalker& walker) override;
};

class IndentHelper {
 public:
  static void indent(std::ostream& out, int level) {
    for (int i = 0; i < level; ++i) {
      out << "  ";
    }
  }
};