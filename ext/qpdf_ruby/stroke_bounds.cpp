#include "stroke_bounds.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace qpdf_ruby::paths {

namespace {

constexpr double kEpsilon = 1e-9;

Point operator+(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
Point operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
Point operator*(Point a, double k) { return {a.x * k, a.y * k}; }
double length(Point a) { return std::hypot(a.x, a.y); }
bool same(Point a, Point b) { return length(a - b) < kEpsilon; }
Point unit(Point a) { return a * (1 / length(a)); }
Point left_normal(Point t) { return {-t.y, t.x}; }

class Bounds {
 public:
  explicit Bounds(Matrix const& m) : m_(m) {}

  void point(Point p) {
    double x = m_[0] * p.x + m_[2] * p.y + m_[4];
    double y = m_[1] * p.x + m_[3] * p.y + m_[5];
    add(x, y, 0, 0);
  }

  // A disk of radius r in user space becomes an ellipse; its box half-sizes follow from the matrix.
  void disk(Point c, double r) {
    double x = m_[0] * c.x + m_[2] * c.y + m_[4];
    double y = m_[1] * c.x + m_[3] * c.y + m_[5];
    add(x, y, r * std::hypot(m_[0], m_[2]), r * std::hypot(m_[1], m_[3]));
  }

  std::optional<Box> box() const {
    if (llx_ > urx_) return std::nullopt;
    return Box{llx_, lly_, urx_, ury_};
  }

 private:
  void add(double x, double y, double dx, double dy) {
    llx_ = std::min(llx_, x - dx);
    lly_ = std::min(lly_, y - dy);
    urx_ = std::max(urx_, x + dx);
    ury_ = std::max(ury_, y + dy);
  }

  Matrix const& m_;
  double llx_ = std::numeric_limits<double>::infinity();
  double lly_ = std::numeric_limits<double>::infinity();
  double urx_ = -std::numeric_limits<double>::infinity();
  double ury_ = -std::numeric_limits<double>::infinity();
};

// Direction a segment leaves its start / enters its end; nullopt for a segment of zero length.
std::optional<Point> start_tangent(Segment const& s) {
  for (int i = 1; i <= 3; ++i) {
    if ((s.curve || i == 3) && !same(s.p[i], s.p[0])) return unit(s.p[i] - s.p[0]);
  }
  return std::nullopt;
}

std::optional<Point> end_tangent(Segment const& s) {
  for (int i = 2; i >= 0; --i) {
    if ((s.curve || i == 0) && !same(s.p[3], s.p[i])) return unit(s.p[3] - s.p[i]);
  }
  return std::nullopt;
}

void cap(Bounds& out, Point at, Point outward, double r, StrokeStyle const& style) {
  if (style.cap == 1) {
    out.disk(at, r);
  } else if (style.cap == 2) {
    Point n = left_normal(outward) * r;
    Point tip = at + outward * r;
    out.point(tip + n);
    out.point(tip - n);
  }
}

// The join between a segment arriving in direction `a` and the next leaving in direction `b`.
void join(Bounds& out, Point at, Point a, Point b, double r, StrokeStyle const& style) {
  if (style.join == 1) {
    out.disk(at, r);
    return;
  }
  if (style.join != 0) return;  // bevel: the segments' own corners already bound it
  double cos_turn = std::clamp(a.x * b.x + a.y * b.y, -1.0, 1.0);
  double half = std::sqrt((1 + cos_turn) / 2);  // cos of half the turn angle
  if (cos_turn > 1 - kEpsilon || half < kEpsilon || 1 / half > style.miter_limit) return;  // straight, or beveled
  Point bisector = left_normal(a) + left_normal(b);
  if (length(bisector) < kEpsilon) return;
  bisector = unit(bisector);
  if (a.x * b.y - a.y * b.x > 0) bisector = bisector * -1;  // a left turn has its point on the right
  out.point(at + bisector * (r / half));
}

void stroke_subpath(Bounds& out, Subpath const& subpath, double r, StrokeStyle const& style) {
  std::vector<Segment const*> drawn;
  for (auto const& s : subpath.segments) {
    if (start_tangent(s)) drawn.push_back(&s);
  }
  if (drawn.empty()) {
    // A degenerate subpath is painted only with round caps, as a dot (ISO 32000-1, 8.5.3.2).
    if (style.cap == 1 && (subpath.closed || !subpath.segments.empty())) out.disk(subpath.start, r);
    return;
  }

  for (auto const* s : drawn) {
    if (s->curve) {
      for (auto const& p : s->p) out.disk(p, r);
    } else {
      Point n = left_normal(*start_tangent(*s)) * r;
      for (Point p : {s->p[0], s->p[3]}) {
        out.point(p + n);
        out.point(p - n);
      }
    }
  }
  for (size_t i = 0; i + 1 < drawn.size(); ++i) {
    join(out, drawn[i]->p[3], *end_tangent(*drawn[i]), *start_tangent(*drawn[i + 1]), r, style);
  }
  if (subpath.closed) {
    join(out, drawn.back()->p[3], *end_tangent(*drawn.back()), *start_tangent(*drawn.front()), r, style);
  } else {
    cap(out, drawn.front()->p[0], *start_tangent(*drawn.front()) * -1, r, style);
    cap(out, drawn.back()->p[3], *end_tangent(*drawn.back()), r, style);
  }
}

}  // namespace

std::optional<Box> fill_bounds(std::vector<Subpath> const& path, Matrix const& ctm) {
  Bounds out(ctm);
  for (auto const& subpath : path) {
    if (subpath.segments.empty()) continue;
    out.point(subpath.start);
    for (auto const& s : subpath.segments) {
      for (auto const& p : s.p) out.point(p);
    }
  }
  return out.box();
}

std::optional<Box> stroke_bounds(std::vector<Subpath> const& path, Matrix const& ctm, StrokeStyle const& style) {
  Bounds out(ctm);
  double r = std::fabs(style.width) / 2;
  for (auto const& subpath : path) stroke_subpath(out, subpath, r, style);
  return out.box();
}

}  // namespace qpdf_ruby::paths
