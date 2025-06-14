// PDFStructWalker.h

#pragma once

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

class PDFStructWalker {
 private:
  std::ostream& out;
  std::map<QPDFObjGen, int> pageObjToNumMap;
  std::unordered_map<int, std::array<double, 4>>& mcid2bbox;

 public:
  PDFStructWalker(std::ostream& out = std::cout, const std::unordered_map<int, std::array<double, 4>>& mcid2bbox = {});

  void buildPageObjectMap(QPDF& pdf);
  std::string get_structure_as_string(QPDFObjectHandle const& node);
  void ensureLayoutBBox(QPDFObjectHandle const& node);

  const std::map<QPDFObjGen, int>& getPageObjectMap() const;
  std::array<double, 4> getPageCropBoxFor(QPDFObjectHandle const& elem) const;

  const std::unordered_map<int, std::array<double, 4>>& getMcidBboxMap() const { return mcid2bbox; }
};
