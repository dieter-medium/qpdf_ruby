#include "mcid_bounds.hpp"
#include "stroke_bounds.hpp"

#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <algorithm>
#include <cmath>
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

// The parts of the graphics state that decide where paint lands; q/Q save and restore all of it.
struct GraphicsState {
  Matrix ctm = kIdentity;
  paths::StrokeStyle stroke;
};

class BoundsCollector : public QPDFObjectHandle::ParserCallbacks {
 public:
  // `owner` is the stream the content's MCIDs are numbered in (the page, or a Form XObject);
  // `state` the graphics state it is drawn with; `active` the forms being walked (cycle guard).
  BoundsCollector(QPDFObjGen page, QPDFObjGen owner, QPDFObjectHandle resources, McidBounds& out,
                  GraphicsState state, std::set<QPDFObjGen>& active, int depth = 0)
      : page_(page), owner_(owner), resources_(std::move(resources)), out_(out), state_(state), active_(active),
        depth_(depth) {}

  void handleObject(QPDFObjectHandle obj, size_t, size_t) override {
    if (!obj.isOperator()) {
      operands_.push_back(obj);
      return;
    }
    std::string const op = obj.getOperatorValue();

    if (op == "q") {
      state_stack_.push(state_);
    } else if (op == "Q") {
      if (!state_stack_.empty()) {
        state_ = state_stack_.top();
        state_stack_.pop();
      }
    } else if (op == "cm") {
      std::vector<QPDFObjectHandle> six(operands_.end() - std::min<size_t>(6, operands_.size()), operands_.end());
      if (auto m = matrix_of(QPDFObjectHandle::newArray(six))) state_.ctm = multiply(*m, state_.ctm);
    } else if (op == "w" && number(1)) {
      state_.stroke.width = *number(1);
    } else if (op == "J" && number(1)) {
      state_.stroke.cap = static_cast<int>(*number(1));
    } else if (op == "j" && number(1)) {
      state_.stroke.join = static_cast<int>(*number(1));
    } else if (op == "M" && number(1)) {
      state_.stroke.miter_limit = *number(1);
    } else if (op == "gs" && !operands_.empty() && operands_.back().isName()) {
      apply_ext_gstate(operands_.back().getName());
    } else if (op == "BDC") {
      mcids_.push(operands_.size() >= 2 ? mcid_of(operands_.back(), resources_) : -1);
    } else if (op == "BMC") {
      mcids_.push(-1);
    } else if (op == "EMC") {
      if (!mcids_.empty()) mcids_.pop();
    } else if (op == "m" && point(1)) {
      path_.push_back({*point(1), {}, false});
      current_ = *point(1);
    } else if (op == "l" && point(1)) {
      add_segment(false, current_, current_, *point(1));
    } else if (op == "c" && point(3)) {
      add_segment(true, *point(3), *point(2), *point(1));
    } else if (op == "v" && point(2)) {
      add_segment(true, current_, *point(2), *point(1));
    } else if (op == "y" && point(2)) {
      add_segment(true, *point(2), *point(1), *point(1));
    } else if (op == "h") {
      close_subpath();
    } else if (op == "re" && number(4)) {
      double x = *number(4), y = *number(3), w = *number(2), h = *number(1);
      path_.push_back({{x, y}, {}, false});
      current_ = {x, y};
      for (paths::Point p : {paths::Point{x + w, y}, paths::Point{x + w, y + h}, paths::Point{x, y + h}}) {
        add_segment(false, current_, current_, p);
      }
      close_subpath();
    } else if (op == "f" || op == "F" || op == "f*") {
      paint(true, false);
    } else if (op == "S") {
      paint(false, true);
    } else if (op == "s") {
      close_subpath();
      paint(false, true);
    } else if (op == "B" || op == "B*") {
      paint(true, true);
    } else if (op == "b" || op == "b*") {
      close_subpath();
      paint(true, true);
    } else if (op == "n") {
      path_.clear();
    } else if (op == "Do" && !operands_.empty() && operands_.back().isName()) {
      draw_xobject(operands_.back().getName());
    }
    operands_.clear();
  }

  void handleEOF() override {}

 private:
  // The n-th numeric operand from the end (1 = the last), if there is one.
  // (A copy of the handle: QPDF 11's accessors are not const.)
  std::optional<double> number(size_t from_end) const {
    if (operands_.size() < from_end) return std::nullopt;
    QPDFObjectHandle operand = operands_[operands_.size() - from_end];
    if (!operand.isNumber()) return std::nullopt;
    return operand.getNumericValue();
  }

  // The n-th coordinate pair from the end (1 = the last two operands).
  std::optional<paths::Point> point(size_t from_end) const {
    auto x = number(2 * from_end);
    auto y = number(2 * from_end - 1);
    if (!x || !y) return std::nullopt;
    return paths::Point{*x, *y};
  }

  // After h, a segment without a new m starts a new subpath at the closed one's start.
  void add_segment(bool curve, paths::Point p1, paths::Point p2, paths::Point p3) {
    if (path_.empty()) return;  // no current point
    if (path_.back().closed) path_.push_back({current_, {}, false});
    path_.back().segments.push_back({curve, {current_, p1, p2, p3}});
    current_ = p3;
  }

  void close_subpath() {
    if (path_.empty() || path_.back().closed) return;
    paths::Subpath& subpath = path_.back();
    paths::Point start = subpath.start;
    if (std::hypot(current_.x - start.x, current_.y - start.y) > 1e-9) add_segment(false, current_, current_, start);
    path_.back().closed = true;
    current_ = start;
  }

  void paint(bool fill, bool stroke) {
    std::optional<Box> box;
    auto unite_with = [&](std::optional<Box> const& more) {
      if (more) box = box ? unite(*box, *more) : *more;
    };
    if (fill) unite_with(paths::fill_bounds(path_, state_.ctm));
    if (stroke) unite_with(paths::stroke_bounds(path_, state_.ctm, state_.stroke));
    if (box) record(*box);
    path_.clear();
  }

  // Line width, cap, join and miter limit set through an ExtGState (/LW /LC /LJ /ML).
  void apply_ext_gstate(std::string const& name) {
    QPDFObjectHandle states = resources_.isDictionary() ? resources_.getKey("/ExtGState") : QPDFObjectHandle::newNull();
    QPDFObjectHandle gs = states.isDictionary() ? states.getKey(name) : QPDFObjectHandle::newNull();
    if (!gs.isDictionary()) return;
    if (gs.getKey("/LW").isNumber()) state_.stroke.width = gs.getKey("/LW").getNumericValue();
    if (gs.getKey("/LC").isInteger()) state_.stroke.cap = gs.getKey("/LC").getIntValueAsInt();
    if (gs.getKey("/LJ").isInteger()) state_.stroke.join = gs.getKey("/LJ").getIntValueAsInt();
    if (gs.getKey("/ML").isNumber()) state_.stroke.miter_limit = gs.getKey("/ML").getNumericValue();
  }

  void draw_xobject(std::string const& name) {
    QPDFObjectHandle xobjects = resources_.isDictionary() ? resources_.getKey("/XObject") : QPDFObjectHandle::newNull();
    QPDFObjectHandle xobject = xobjects.isDictionary() ? xobjects.getKey(name) : QPDFObjectHandle::newNull();
    if (!xobject.isStream()) return;
    QPDFObjectHandle dict = xobject.getDict();
    Extent extent;
    if (dict.getKey("/Subtype").isNameAndEquals("/Image")) {
      extent.add_box(state_.ctm, 0, 0, 1, 1);  // an image fills the unit square
    } else if (dict.getKey("/Subtype").isNameAndEquals("/Form")) {
      QPDFObjectHandle bbox = dict.getKey("/BBox");
      if (!bbox.isArray() || bbox.getArrayNItems() != 4) return;
      Matrix m = multiply(matrix_of(dict.getKey("/Matrix")).value_or(kIdentity), state_.ctm);
      extent.add_box(m, bbox.getArrayItem(0).getNumericValue(), bbox.getArrayItem(1).getNumericValue(),
                     bbox.getArrayItem(2).getNumericValue(), bbox.getArrayItem(3).getNumericValue());
      walk_form(xobject, m);
    }
    if (!extent.empty()) record(extent.box());
  }

  // The form's own MCIDs, keyed by the form; it starts from the graphics state it is drawn with,
  // and its resources default to the ones it is drawn with.
  void walk_form(QPDFObjectHandle form, Matrix const& m) {
    if (depth_ >= kMaxFormDepth || !active_.insert(form.getObjGen()).second) return;
    QPDFObjectHandle resources = form.getDict().getKey("/Resources");
    GraphicsState inner_state = state_;
    inner_state.ctm = m;
    BoundsCollector inner(page_, form.getObjGen(), resources.isDictionary() ? resources : resources_, out_,
                          inner_state, active_, depth_ + 1);
    form.parseAsContents(&inner);
    active_.erase(form.getObjGen());
  }

  void record(Box const& box) {
    int mcid = -1;
    std::stack<int> copy = mcids_;
    while (!copy.empty() && mcid < 0) {
      mcid = copy.top();
      copy.pop();
    }
    if (mcid < 0) return;
    McidKey key{page_, owner_, mcid};
    auto it = out_.find(key);
    out_[key] = it == out_.end() ? box : unite(it->second, box);
  }

  QPDFObjGen page_;
  QPDFObjGen owner_;
  QPDFObjectHandle resources_;
  McidBounds& out_;
  std::vector<QPDFObjectHandle> operands_;
  GraphicsState state_;
  std::set<QPDFObjGen>& active_;
  int depth_;
  std::stack<GraphicsState> state_stack_;
  std::stack<int> mcids_;
  std::vector<paths::Subpath> path_;
  paths::Point current_;
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
                              GraphicsState{}, active);
    helper.parseContents(&collector);

    QPDFObjectHandle crop = helper.getCropBox();  // falls back to the MediaBox; never copies
    for (auto& [key, box] : on_page) {
      if (crop.isArray() && crop.getArrayNItems() == 4) {
        box[0] = std::max(box[0], crop.getArrayItem(0).getNumericValue());
        box[1] = std::max(box[1], crop.getArrayItem(1).getNumericValue());
        box[2] = std::min(box[2], crop.getArrayItem(2).getNumericValue());
        box[3] = std::min(box[3], crop.getArrayItem(3).getNumericValue());
      }
      // Content entirely outside the crop box is invisible: no bounds rather than an inverted box.
      if (box[0] > box[2] || box[1] > box[3]) continue;
      bounds[key] = box;
    }
  }
  return bounds;
}

}  // namespace qpdf_ruby
