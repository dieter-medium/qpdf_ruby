#pragma once
#define POINTERHOLDER_TRANSITION 1

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjGen.hh>

#include <array>
#include <map>
#include <utility>

namespace qpdf_ruby {

// [llx, lly, urx, ury] in default user space.
using Box = std::array<double, 4>;

// MCIDs are unique per page only, so bounds are keyed by (page, MCID).
using McidKey = std::pair<QPDFObjGen, int>;
using McidBounds = std::map<McidKey, Box>;

// The area each piece of marked content paints: images, Form XObjects (their /BBox under their
// /Matrix) and painted paths, under the full CTM (cm concatenates, q/Q save and restore), clipped
// to the page's crop box. Content belongs to the innermost enclosing MCID.
McidBounds find_mcid_bounds(QPDF& pdf);

Box unite(Box const& a, Box const& b);

}  // namespace qpdf_ruby
