#pragma once

#include "mcid_bounds.hpp"

#include <array>
#include <optional>
#include <vector>

namespace qpdf_ruby::paths {

using Matrix = std::array<double, 6>;  // a b c d e f, PDF's row-vector convention

struct Point {
  double x = 0;
  double y = 0;
};

// A line uses `p[0]` and `p[3]`; a cubic Bezier curve all four.
struct Segment {
  bool curve = false;
  std::array<Point, 4> p;
};

struct Subpath {
  Point start;
  std::vector<Segment> segments;
  bool closed = false;
};

// The graphics state parameters that shape a stroke (w, J, j, M).
struct StrokeStyle {
  double width = 1;
  int cap = 0;   // 0 butt, 1 round, 2 projecting square
  int join = 0;  // 0 miter, 1 round, 2 bevel
  double miter_limit = 10;
};

// The area a path covers in default user space when filled: the hull of its points (a curve
// lies inside the hull of its control points). Empty for a path that has no segment.
std::optional<Box> fill_bounds(std::vector<Subpath> const& path, Matrix const& ctm);

// The area a stroke of the path paints in default user space: every segment widened by half the
// line width, plus its caps and joins as the style draws them, all under the CTM (a round cap or
// join becomes an ellipse). Curves are bounded by a disk of half the line width around each
// control point - exact for lines, a little generous for curves. Dashes are ignored: they only
// remove paint. Empty for a path that paints nothing.
std::optional<Box> stroke_bounds(std::vector<Subpath> const& path, Matrix const& ctm, StrokeStyle const& style);

}  // namespace qpdf_ruby::paths
