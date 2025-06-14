// x_object_finder.hpp
#pragma once

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QIntC.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QTC.hh>
#include <qpdf/QUtil.hh>
#include <qpdf/BufferInputSource.hh>
#include <qpdf/QPDFTokenizer.hh>
#include <qpdf/Buffer.hh>

#include <string>
#include <vector>
#include <iostream>
#include <map>
#include <stack>
#include <regex>
#include <sstream>
#include <deque>

struct ImageInfo {
  int mcid;
  double width;
  double height;
  std::array<double, 6> cm_matrix;            // stores the transformation matrix values
  std::array<double, 4> bbox = {0, 0, 0, 0};  // user-space bounding box [llx, lly, urx, ury]

  ImageInfo() : mcid(-1), width(0), height(0), cm_matrix{0, 0, 0, 0, 0, 0} {}

  ImageInfo(int m, double w, double h, const std::array<double, 6>& cm) : mcid(m), width(w), height(h), cm_matrix(cm) {}

  std::string to_string() const {
    std::ostringstream oss;
    oss << "ImageInfo(mcid=" << mcid << ", width=" << width << ", height=" << height << ", cm_matrix=[";
    for (size_t i = 0; i < cm_matrix.size(); ++i) {
      oss << cm_matrix[i];
      if (i != cm_matrix.size() - 1) oss << ", ";
    }
    oss << "])";
    return oss.str();
  }
};

class PDFImageMapper {
 public:
  explicit PDFImageMapper(int target_mcid);

  void find(QPDF& pdf);
  // Parses the page's content stream to find the XObject name for the MCID
  void find(QPDFPageObjectHelper& page);

  void push_cm(double value);

  // Expose internal map for external use
  const std::map<std::string, ImageInfo>& getImageMap() const { return image_to_mcid; }

 private:
  int target_mcid;
  bool in_target_mcid;
  std::map<std::string, ImageInfo> image_to_mcid;
  std::deque<double> cm_fixed_size_queue;
};