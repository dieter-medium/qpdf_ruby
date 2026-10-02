// PDFStructWalker.h

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

#include "mcid_bounds.hpp"

class PDFStructWalker {
 private:
  std::ostream& out;
  std::map<QPDFObjGen, int> pageObjToNumMap;
  qpdf_ruby::McidBounds mcid_bounds;

 public:
  explicit PDFStructWalker(std::ostream& out = std::cout, qpdf_ruby::McidBounds bounds = {});

  void buildPageObjectMap(QPDF& pdf);
  std::string get_structure_as_string(QPDFObjectHandle const& node);
  void ensureLayoutBBox(QPDFObjectHandle const& node);

  const std::map<QPDFObjGen, int>& getPageObjectMap() const;
  std::array<double, 4> getPageCropBoxFor(QPDFObjectHandle const& elem) const;

  const qpdf_ruby::McidBounds& getMcidBounds() const { return mcid_bounds; }
};
