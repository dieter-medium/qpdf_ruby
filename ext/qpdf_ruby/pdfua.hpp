#pragma once
#define POINTERHOLDER_TRANSITION 1

#include <qpdf/QPDF.hh>

#include <map>
#include <optional>
#include <string>
#include <vector>

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
// Covers page content and the Form XObjects it draws - not annotation appearance streams (/AP),
// tiling-pattern content or Type 3 glyph procedures, which Chromium does not emit today.
UntaggedCounts mark_untagged_content_as_artifacts(QPDF& pdf);

// The same walk without changing anything.
UntaggedCounts count_untagged_content(QPDF& pdf);

// Chromium tags CSS backgrounds and borders as content of the element they belong to - a Table's
// cell backgrounds, an <article>'s card - and a grouping element (Document, Part, Art, Sect, Div,
// BlockQuote, TOC, TOCI, Index, the table and list containers; after the RoleMap) must not hold
// content of its own (PAC: "Content in an inadmissible location", "Invalid use of a TR"). Content
// held directly by such an element that paints only shapes - no text, XObject or inline image, no
// nested tagged content - becomes /Artifact BMC ... EMC, and leaves the element's kids and the
// ParentTree. Content with text stays tagged. Returns how many pieces were turned into artifacts.
long artifact_tagged_decorations(QPDF& pdf);

// Gives each Link annotation without /Contents a description (7.18.1-2, 7.18.5-2): the text given
// for its URI in `texts`, else its structure element's /Alt or /ActualText, else the URI itself
// (a mailto: address without the scheme). An internal link uses the text given for "#<name>" of
// its named destination (a name in the catalog's /Dests, a string in the /Names /Dests name tree),
// else "Page N". Returns how many were described.
long describe_links(QPDF& pdf, std::map<std::string, std::string> const& texts);

// Moves an LI's content other than Lbl/LBody into a new LBody (7.2-20), structure elements as well
// as marked-content references, with the ParentTree entries of the moved content updated.
// Returns how many LBody elements were created.
long wrap_list_bodies(QPDF& pdf);

// Marked content and annotations whose ParentTree entry does not name the structure element that
// holds them - MCIDs looked up in the stream they are numbered in (a reference's /Stm, else the
// page), annotations through their /StructParent. Read-only; 0 for a consistent tree.
long count_parent_tree_mismatches(QPDF& pdf);

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

// Identifies the file as PDF/UA-1 in its XMP metadata (7.1-8) and sets DisplayDocTitle and
// Marked. An existing metadata stream keeps everything it has: pdfuaid:part and dc:title are each
// added only if missing (read with namespace scoping, element or attribute form; comments and CDATA
// do not count); an existing dc:title is never replaced. Without a stream, a new one is written.
// The title is `title` if given (it also becomes the information dictionary's /Title), else that
// /Title. Never throws for odd input: without a structure tree only the title and DisplayDocTitle
// are set (Marked and pdfuaid would claim a tagged file), and an existing packet that is not
// well-formed XML with an rdf:RDF element is left alone - apply() reports both as
// unidentified_reason. Returns true if anything changed.
bool add_pdfua_identification(QPDF& pdf, std::optional<std::string> const& title);

// Who made the file: the information dictionary's /Creator and /Producer, and XMP
// xmp:CreatorTool and pdf:Producer (the XMP twins PDF/A expects to match). A value that is not
// given, or empty, is left alone. The information dictionary is always set; XMP gets a property
// only where the packet has none - an existing one is kept and named in xmp_kept, as is every
// given property when the packet is not well-formed XML with an rdf:RDF element. Without a packet
// a new one is written.
struct DocumentInfoResult {
  bool changed = false;
  std::vector<std::string> xmp_kept;
};

DocumentInfoResult set_document_info(QPDF& pdf, std::optional<std::string> const& creator,
                                     std::optional<std::string> const& producer);

struct Report {
  long decorations = 0;
  UntaggedCounts artifacts;
  long links = 0;
  long list_bodies = 0;
  long roles = 0;
  long figure_groups = 0;
  long figures_without_alt = 0;
  bool identified = false;
  std::optional<std::string> unidentified_reason;  // why the file is not identified, if it is not
};

// All of the above, in the order that keeps each step's input intact.
Report apply(QPDF& pdf, std::map<std::string, std::string> const& link_texts, std::optional<std::string> const& title);

}  // namespace qpdf_ruby::pdfua
