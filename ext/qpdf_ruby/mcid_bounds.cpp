#include "mcid_bounds.hpp"

#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <algorithm>
#include <limits>
#include <optional>
#include <set>
#include <stack>
#include <vector>

namespace qpdf_ruby {

namespace {

using Matrix = std::array<double, 6>;  // a b c d e f

constexpr Matrix kIdentity = {1, 0, 0, 1, 0, 0};

// m1 applied first, then m2 (PDF's row-vector convention: CTM' = m1 x CTM).
Matrix multiply(Matrix const& m1, Matrix const& m2) {
  return {m1[0] * m2[0] + m1[1] * m2[2],         m1[0] * m2[1] + m1[1] * m2[3],
          m1[2] * m2[0] + m1[3] * m2[2],         m1[2] * m2[1] + m1[3] * m2[3],
          m1[4] * m2[0] + m1[5] * m2[2] + m2[4], m1[4] * m2[1] + m1[5] * m2[3] + m2[5]};
}

std::pair<double, double> transform(Matrix const& m, double x, double y) {
  return {m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]};
}

struct Extent {
  double llx = std::numeric_limits<double>::infinity();
  double lly = std::numeric_limits<double>::infinity();
  double urx = -std::numeric_limits<double>::infinity();
  double ury = -std::numeric_limits<double>::infinity();

  bool empty() const { return llx > urx || lly > ury; }
  void add(double x, double y) {
    llx = std::min(llx, x);
    lly = std::min(lly, y);
    urx = std::max(urx, x);
    ury = std::max(ury, y);
  }
  void add_box(Matrix const& m, double x0, double y0, double x1, double y1) {
    for (auto [x, y] : {transform(m, x0, y0), transform(m, x1, y0), transform(m, x1, y1), transform(m, x0, y1)}) {
      add(x, y);
    }
  }
  Box box() const { return {llx, lly, urx, ury}; }
};

std::optional<Matrix> matrix_of(QPDFObjectHandle array) {
  if (!array.isArray() || array.getArrayNItems() != 6) return std::nullopt;
  Matrix m;
  for (int i = 0; i < 6; ++i) {
    if (!array.getArrayItem(i).isNumber()) return std::nullopt;
    m[i] = array.getArrayItem(i).getNumericValue();
  }
  return m;
}

int mcid_of(QPDFObjectHandle properties, QPDFObjectHandle resources) {
  if (properties.isName() && resources.isDictionary()) {
    QPDFObjectHandle named = resources.getKey("/Properties");
    properties = named.isDictionary() ? named.getKey(properties.getName()) : QPDFObjectHandle::newNull();
  }
  if (properties.isDictionary() && properties.getKey("/MCID").isInteger()) {
    return properties.getKey("/MCID").getIntValue();
  }
  return -1;
}

constexpr int kMaxFormDepth = 16;

class BoundsCollector : public QPDFObjectHandle::ParserCallbacks {
 public:
  // `owner` is the stream the content's MCIDs are numbered in (the page, or a Form XObject);
  // `ctm` the transformation it is drawn with; `active` the forms being walked (cycle guard).
  BoundsCollector(QPDFObjGen page, QPDFObjGen owner, QPDFObjectHandle resources, McidBounds& out, Matrix ctm,
                  std::set<QPDFObjGen>& active, int depth = 0)
      : page_(page), owner_(owner), resources_(std::move(resources)), out_(out), ctm_(ctm), active_(active),
        depth_(depth) {}

  void handleObject(QPDFObjectHandle obj, size_t, size_t) override {
    if (!obj.isOperator()) {
      operands_.push_back(obj);
      return;
    }
    std::string const op = obj.getOperatorValue();

    if (op == "q") {
      ctm_stack_.push(ctm_);
    } else if (op == "Q") {
      if (!ctm_stack_.empty()) {
        ctm_ = ctm_stack_.top();
        ctm_stack_.pop();
      }
    } else if (op == "cm") {
      std::vector<QPDFObjectHandle> six(operands_.end() - std::min<size_t>(6, operands_.size()), operands_.end());
      if (auto m = matrix_of(QPDFObjectHandle::newArray(six))) ctm_ = multiply(*m, ctm_);
    } else if (op == "BDC") {
      mcids_.push(operands_.size() >= 2 ? mcid_of(operands_.back(), resources_) : -1);
    } else if (op == "BMC") {
      mcids_.push(-1);
    } else if (op == "EMC") {
      if (!mcids_.empty()) mcids_.pop();
    } else if (op == "re" && operands_.size() >= 4) {
      double x = operands_[operands_.size() - 4].getNumericValue();
      double y = operands_[operands_.size() - 3].getNumericValue();
      double w = operands_[operands_.size() - 2].getNumericValue();
      double h = operands_[operands_.size() - 1].getNumericValue();
      path_.add_box(ctm_, x, y, x + w, y + h);
    } else if ((op == "m" || op == "l") && operands_.size() >= 2) {
      add_point(operands_.size() - 2);
    } else if ((op == "c") && operands_.size() >= 6) {
      for (size_t i : {6, 4, 2}) add_point(operands_.size() - i);
    } else if ((op == "v" || op == "y") && operands_.size() >= 4) {
      for (size_t i : {4, 2}) add_point(operands_.size() - i);
    } else if (op == "S" || op == "s" || op == "f" || op == "F" || op == "f*" || op == "B" || op == "B*" ||
               op == "b" || op == "b*") {
      if (!path_.empty()) record(path_);
      path_ = Extent();
    } else if (op == "n") {
      path_ = Extent();
    } else if (op == "Do" && !operands_.empty() && operands_.back().isName()) {
      draw_xobject(operands_.back().getName());
    }
    operands_.clear();
  }

  void handleEOF() override {}

 private:
  void add_point(size_t index) {
    auto [x, y] = transform(ctm_, operands_[index].getNumericValue(), operands_[index + 1].getNumericValue());
    path_.add(x, y);
  }

  void draw_xobject(std::string const& name) {
    QPDFObjectHandle xobjects = resources_.isDictionary() ? resources_.getKey("/XObject") : QPDFObjectHandle::newNull();
    QPDFObjectHandle xobject = xobjects.isDictionary() ? xobjects.getKey(name) : QPDFObjectHandle::newNull();
    if (!xobject.isStream()) return;
    QPDFObjectHandle dict = xobject.getDict();
    Extent extent;
    if (dict.getKey("/Subtype").isNameAndEquals("/Image")) {
      extent.add_box(ctm_, 0, 0, 1, 1);  // an image fills the unit square
    } else if (dict.getKey("/Subtype").isNameAndEquals("/Form")) {
      QPDFObjectHandle bbox = dict.getKey("/BBox");
      if (!bbox.isArray() || bbox.getArrayNItems() != 4) return;
      Matrix m = multiply(matrix_of(dict.getKey("/Matrix")).value_or(kIdentity), ctm_);
      extent.add_box(m, bbox.getArrayItem(0).getNumericValue(), bbox.getArrayItem(1).getNumericValue(),
                     bbox.getArrayItem(2).getNumericValue(), bbox.getArrayItem(3).getNumericValue());
      walk_form(xobject, m);
    }
    if (!extent.empty()) record(extent);
  }

  // The form's own MCIDs, keyed by the form; its resources default to the ones it is drawn with.
  void walk_form(QPDFObjectHandle form, Matrix const& m) {
    if (depth_ >= kMaxFormDepth || !active_.insert(form.getObjGen()).second) return;
    QPDFObjectHandle resources = form.getDict().getKey("/Resources");
    BoundsCollector inner(page_, form.getObjGen(), resources.isDictionary() ? resources : resources_, out_, m,
                          active_, depth_ + 1);
    form.parseAsContents(&inner);
    active_.erase(form.getObjGen());
  }

  void record(Extent const& extent) {
    int mcid = -1;
    std::stack<int> copy = mcids_;
    while (!copy.empty() && mcid < 0) {
      mcid = copy.top();
      copy.pop();
    }
    if (mcid < 0) return;
    McidKey key{page_, owner_, mcid};
    auto it = out_.find(key);
    out_[key] = it == out_.end() ? extent.box() : unite(it->second, extent.box());
  }

  QPDFObjGen page_;
  QPDFObjGen owner_;
  QPDFObjectHandle resources_;
  McidBounds& out_;
  std::vector<QPDFObjectHandle> operands_;
  Matrix ctm_;
  std::set<QPDFObjGen>& active_;
  int depth_;
  std::stack<Matrix> ctm_stack_;
  std::stack<int> mcids_;
  Extent path_;
};

}  // namespace

Box unite(Box const& a, Box const& b) {
  return {std::min(a[0], b[0]), std::min(a[1], b[1]), std::max(a[2], b[2]), std::max(a[3], b[3])};
}

McidBounds find_mcid_bounds(QPDF& pdf) {
  McidBounds bounds;
  for (QPDFObjectHandle page : pdf.getAllPages()) {
    QPDFPageObjectHelper helper(page);
    McidBounds on_page;
    std::set<QPDFObjGen> active;
    BoundsCollector collector(page.getObjGen(), page.getObjGen(), helper.getAttribute("/Resources", false), on_page,
                              kIdentity, active);
    helper.parseContents(&collector);

    QPDFObjectHandle crop = helper.getCropBox();  // falls back to the MediaBox; never copies
    for (auto& [key, box] : on_page) {
      if (crop.isArray() && crop.getArrayNItems() == 4) {
        box[0] = std::max(box[0], crop.getArrayItem(0).getNumericValue());
        box[1] = std::max(box[1], crop.getArrayItem(1).getNumericValue());
        box[2] = std::min(box[2], crop.getArrayItem(2).getNumericValue());
        box[3] = std::min(box[3], crop.getArrayItem(3).getNumericValue());
      }
      bounds[key] = box;
    }
  }
  return bounds;
}

}  // namespace qpdf_ruby
