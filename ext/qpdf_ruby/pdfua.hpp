#pragma once
#define POINTERHOLDER_TRANSITION 1

#include <qpdf/QPDF.hh>

#include <map>
#include <optional>
#include <string>

// Fixes for the PDF/UA-1 gaps of Chromium's tagged PDF (print-to-PDF with generateTaggedPDF): it
// writes no /Artifact sections, no LBody, no link descriptions, no XMP identification, and uses
// PDF 2.0 structure types without a RoleMap. Each step only adds what is missing and leaves
// everything else in place.
namespace qpdf_ruby::pdfua {

// Painting operators outside any marked content, per kind - what PDF/UA-1 7.1-3 rejects.
struct UntaggedCounts {
  long paths = 0;
  long texts = 0;
  long xobjects = 0;
  long shadings = 0;
  long inline_images = 0;
  long total() const { return paths + texts + xobjects + shadings + inline_images; }
};

// Wraps every painting operation outside marked content in /Artifact BMC ... EMC: paths (with
// their construction operators), text shows, XObjects, shadings, inline images. Content inside
// marked content is never touched, and neither is a Form XObject that carries marked content of
// its own (wrapping it would nest tagged content in an artifact, 7.1-2). Returns the counts.
UntaggedCounts mark_untagged_content_as_artifacts(QPDF& pdf);

// The same walk without changing anything.
UntaggedCounts count_untagged_content(QPDF& pdf);

// Gives each Link annotation without /Contents a description (7.18.1-2, 7.18.5-2): the text given
// for its URI in `texts`, else its structure element's /Alt or /ActualText, else the URI itself
// (a mailto: address without the scheme). An internal link uses the text given for "#<name>" of
// its named destination, else "Page N". Returns how many were described.
long describe_links(QPDF& pdf, std::map<std::string, std::string> const& texts);

// Moves an LI's content other than Lbl/LBody into a new LBody (7.2-20), structure elements as well
// as marked-content references, with the ParentTree entries of the moved content updated.
// Returns how many LBody elements were created.
long wrap_list_bodies(QPDF& pdf);

// Maps PDF 2.0 structure types Chromium emits (Aside, Strong, Em, ...) to PDF 1.7 standard types
// in the RoleMap, unless the RoleMap already maps them (7.1-5). Returns how many were added.
long map_nonstandard_roles(QPDF& pdf);

// Chromium tags an HTML <figure> as a Figure without alternative text around the image's own
// Figure (which carries the alt text) and its Caption (7.3-1). Such a grouping Figure - no /Alt or
// /ActualText of its own, but a Figure below it - becomes a Div; nothing is lost, the caption stays
// readable. Returns how many were retagged.
long retag_grouping_figures(QPDF& pdf);

// Figures that still have neither /Alt nor /ActualText - only a person can write those (7.3-1).
long count_figures_without_alt(QPDF& pdf);

// Identifies the file as PDF/UA-1 in its XMP metadata (7.1-8) - extending an existing metadata
// stream, or writing a new one with dc:title - and sets DisplayDocTitle and Marked. The title is
// `title` if given, else the document information dictionary's /Title. Returns false if the file
// was already identified.
bool add_pdfua_identification(QPDF& pdf, std::optional<std::string> const& title);

struct Report {
  UntaggedCounts artifacts;
  long links = 0;
  long list_bodies = 0;
  long roles = 0;
  long figure_groups = 0;
  long figures_without_alt = 0;
  bool identified = false;
};

// All of the above, in the order that keeps each step's input intact.
Report apply(QPDF& pdf, std::map<std::string, std::string> const& link_texts, std::optional<std::string> const& title);

}  // namespace qpdf_ruby::pdfua
