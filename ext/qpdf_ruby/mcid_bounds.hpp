#pragma once
#define POINTERHOLDER_TRANSITION 1

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjGen.hh>

#include <array>
#include <map>
#include <tuple>

namespace qpdf_ruby {

// [llx, lly, urx, ury] in default user space.
using Box = std::array<double, 4>;

// MCIDs are unique per content stream only: the page's own content, or a Form XObject's (which a
// marked-content reference names with /Stm). Bounds are keyed by (page, stream, MCID); for the
// page's own content the stream is the page itself.
using McidKey = std::tuple<QPDFObjGen, QPDFObjGen, int>;
using McidBounds = std::map<McidKey, Box>;

// The area each piece of marked content paints: images, Form XObjects (their /BBox under their
// /Matrix) and painted paths, under the full CTM (cm concatenates, q/Q save and restore), clipped
// to the page's crop box (content wholly outside it gets no entry). Text adds nothing: its extent
// needs font metrics, so a Figure made only of text falls back to the crop box. Content belongs to the innermost enclosing MCID of its own stream; a Form
// XObject's content is also walked, under its /Matrix and the CTM it is drawn with, for the MCIDs
// numbered in that form.
McidBounds find_mcid_bounds(QPDF& pdf);

Box unite(Box const& a, Box const& b);

}  // namespace qpdf_ruby
