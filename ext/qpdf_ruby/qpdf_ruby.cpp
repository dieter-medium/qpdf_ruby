#include "qpdf_ruby.hpp"
#include "document_handle.hpp"
#include "mcid_bounds.hpp"
#include "pdf_struct_walker.hpp"
#include "pdfua.hpp"
#include "ruby_guard.hpp"
#include "struct_node.hpp"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <map>
#include <optional>
#include <string>
#include <vector>

VALUE rb_mQpdfRuby;
VALUE rb_cDocument;
VALUE rb_eQpdfRubyError;

using namespace qpdf_ruby;

// ---- the wrapped DocumentHandle ------------------------------------------------------------------

static void doc_free(void* ptr) { qpdf_ruby_close(static_cast<DocumentHandle*>(ptr)); }

static const rb_data_type_t document_type = {
    "QpdfRuby::Document",
    {nullptr, doc_free, nullptr},
    nullptr,
    nullptr,
    RUBY_TYPED_FREE_IMMEDIATELY,
};

static DocumentHandle* handle_of(VALUE self) {
  auto* h = static_cast<DocumentHandle*>(rb_check_typeddata(self, &document_type));
  if (!h) rb_raise(rb_eQpdfRubyError, "document is not open");
  return h;
}

static VALUE doc_alloc(VALUE klass) { return TypedData_Wrap_Struct(klass, &document_type, nullptr); }

// ---- arguments and results (see the rules in ruby_guard.hpp) ------------------------------------

// Ruby side, before any C++ object exists: these may raise.
static VALUE checked_string(VALUE value) {
  Check_Type(value, T_STRING);
  return value;
}

static VALUE checked_optional_string(VALUE value) { return NIL_P(value) ? Qnil : checked_string(value); }

static int collect_link_text(VALUE key, VALUE value, VALUE pairs) {
  rb_ary_push(pairs, rb_assoc_new(rb_obj_as_string(key), rb_obj_as_string(value)));
  return ST_CONTINUE;
}

// The link texts hash as an array of [String, String] pairs (keys and values through #to_s), or nil.
static VALUE checked_link_texts(VALUE hash) {
  if (NIL_P(hash)) return Qnil;
  Check_Type(hash, T_HASH);
  VALUE pairs = rb_ary_new();
  rb_hash_foreach(hash, collect_link_text, pairs);
  return pairs;
}

// C++ side, inside guarded only: reading checked values never raises.
static std::string cpp_string(VALUE str) { return std::string(RSTRING_PTR(str), RSTRING_LEN(str)); }

static std::optional<std::string> cpp_optional_string(VALUE str) {
  if (NIL_P(str)) return std::nullopt;
  return cpp_string(str);
}

static std::map<std::string, std::string> cpp_link_texts(VALUE pairs) {
  std::map<std::string, std::string> texts;
  if (NIL_P(pairs)) return texts;
  for (long i = 0; i < RARRAY_LEN(pairs); ++i) {
    VALUE pair = rb_ary_entry(pairs, i);
    texts[cpp_string(rb_ary_entry(pair, 0))] = cpp_string(rb_ary_entry(pair, 1));
  }
  return texts;
}

static VALUE ruby_string(std::string const& value) {
  return rb_utf8_str_new(value.data(), static_cast<long>(value.size()));
}

// ---- opening and writing ---------------------------------------------------------------------

// Calling initialize again (send(:initialize, ...)) closes the document opened before.
static VALUE doc_initialize(int argc, VALUE* argv, VALUE self) {
  VALUE filename = Qnil, password = Qnil;
  rb_scan_args(argc, argv, "11", &filename, &password);
  checked_string(filename);
  password = checked_optional_string(password);

  qpdf_ruby_close(static_cast<DocumentHandle*>(rb_check_typeddata(self, &document_type)));
  DATA_PTR(self) = nullptr;
  DATA_PTR(self) = guarded([&] {
    return DocumentHandle::open(cpp_string(filename), cpp_optional_string(password).value_or("")).release();
  });
  RB_GC_GUARD(filename);
  RB_GC_GUARD(password);
  return self;
}

static VALUE doc_from_memory(int argc, VALUE* argv, VALUE klass) {
  VALUE data = Qnil, password = Qnil;
  rb_scan_args(argc, argv, "11", &data, &password);
  checked_string(data);
  password = checked_optional_string(password);

  VALUE doc = doc_alloc(klass);
  DATA_PTR(doc) = guarded([&] {
    auto const* bytes = reinterpret_cast<unsigned char const*>(RSTRING_PTR(data));
    std::vector<unsigned char> copy(bytes, bytes + RSTRING_LEN(data));
    return DocumentHandle::open_memory("ruby-memory", std::move(copy), cpp_optional_string(password).value_or(""))
        .release();
  });
  RB_GC_GUARD(data);
  RB_GC_GUARD(password);
  return doc;
}

static VALUE doc_write(VALUE self, VALUE out_filename) {
  checked_string(out_filename);
  DocumentHandle* h = handle_of(self);
  guarded([&] { h->write(cpp_string(out_filename)); });
  RB_GC_GUARD(out_filename);
  return Qnil;
}

static VALUE doc_to_memory(VALUE self) {
  DocumentHandle* h = handle_of(self);
  return guarded_to_ruby([&] { return h->write_to_memory(); }, [](std::string const& bytes) {
    return rb_str_new(bytes.data(), static_cast<long>(bytes.size()));
  });
}

// ---- structure tree --------------------------------------------------------------------------

static QPDFObjectHandle struct_kids(QPDF& pdf) {
  QPDFObjectHandle struct_root = pdf.getRoot().getKey("/StructTreeRoot");
  if (!struct_root.isDictionary()) throw std::runtime_error("No StructTreeRoot found");
  return struct_root.getKey("/K");
}

static VALUE doc_show_structure(VALUE self) {
  DocumentHandle* h = handle_of(self);
  return guarded_to_ruby(
      [&] {
        QPDF& pdf = h->qpdf();
        QPDFObjectHandle kids = struct_kids(pdf);
        PDFStructWalker walker;
        walker.buildPageObjectMap(pdf);
        std::string out;
        if (kids.isArray()) {
          for (auto kid : kids.aitems()) out += walker.get_structure_as_string(kid);
        } else {
          out = walker.get_structure_as_string(kids);
        }
        return out;
      },
      ruby_string);
}

static VALUE doc_ensure_bbox(VALUE self) {
  DocumentHandle* h = handle_of(self);
  guarded([&] {
    QPDF& pdf = h->qpdf();
    QPDFObjectHandle kids = struct_kids(pdf);
    PDFStructWalker walker(std::cout, find_mcid_bounds(pdf));
    if (kids.isArray()) {
      for (auto kid : kids.aitems()) walker.ensureLayoutBBox(kid);
    } else {
      walker.ensureLayoutBBox(kids);
    }
  });
  return Qnil;
}

// ---- PDF/UA ----------------------------------------------------------------------------------

static VALUE counts_hash(pdfua::UntaggedCounts const& c) {
  VALUE hash = rb_hash_new();
  rb_hash_aset(hash, ID2SYM(rb_intern("paths")), LONG2NUM(c.paths));
  rb_hash_aset(hash, ID2SYM(rb_intern("texts")), LONG2NUM(c.texts));
  rb_hash_aset(hash, ID2SYM(rb_intern("xobjects")), LONG2NUM(c.xobjects));
  rb_hash_aset(hash, ID2SYM(rb_intern("shadings")), LONG2NUM(c.shadings));
  rb_hash_aset(hash, ID2SYM(rb_intern("inline_images")), LONG2NUM(c.inline_images));
  rb_hash_aset(hash, ID2SYM(rb_intern("total")), LONG2NUM(c.total()));
  return hash;
}

static VALUE doc_mark_untagged_content_as_artifacts(VALUE self) {
  DocumentHandle* h = handle_of(self);
  auto counts = guarded([&] { return pdfua::mark_untagged_content_as_artifacts(h->qpdf()); });
  return counts_hash(counts);
}

static VALUE doc_mark_paths_as_artifacts(VALUE self) {
  rb_category_warn(RB_WARN_CATEGORY_DEPRECATED,
                   "QpdfRuby::Document#mark_paths_as_artifacts is deprecated; use #mark_untagged_content_as_artifacts");
  return doc_mark_untagged_content_as_artifacts(self);
}

static VALUE doc_untagged_content(VALUE self) {
  DocumentHandle* h = handle_of(self);
  auto counts = guarded([&] { return pdfua::count_untagged_content(h->qpdf()); });
  return counts_hash(counts);
}

static VALUE doc_describe_links(int argc, VALUE* argv, VALUE self) {
  VALUE texts = Qnil;
  rb_scan_args(argc, argv, "01", &texts);
  VALUE pairs = checked_link_texts(texts);
  DocumentHandle* h = handle_of(self);
  long described = guarded([&] { return pdfua::describe_links(h->qpdf(), cpp_link_texts(pairs)); });
  RB_GC_GUARD(pairs);
  return LONG2NUM(described);
}

static VALUE doc_artifact_tagged_decorations(VALUE self) {
  DocumentHandle* h = handle_of(self);
  return LONG2NUM(guarded([&] { return pdfua::artifact_tagged_decorations(h->qpdf()); }));
}

static VALUE doc_wrap_list_bodies(VALUE self) {
  DocumentHandle* h = handle_of(self);
  return LONG2NUM(guarded([&] { return pdfua::wrap_list_bodies(h->qpdf()); }));
}

static VALUE doc_parent_tree_mismatches(VALUE self) {
  DocumentHandle* h = handle_of(self);
  return LONG2NUM(guarded([&] { return pdfua::count_parent_tree_mismatches(h->qpdf()); }));
}

static VALUE doc_map_nonstandard_roles(VALUE self) {
  DocumentHandle* h = handle_of(self);
  return LONG2NUM(guarded([&] { return pdfua::map_nonstandard_roles(h->qpdf()); }));
}

static VALUE doc_retag_grouping_figures(VALUE self) {
  DocumentHandle* h = handle_of(self);
  return LONG2NUM(guarded([&] { return pdfua::retag_grouping_figures(h->qpdf()); }));
}

static VALUE doc_figures_without_alt(VALUE self) {
  DocumentHandle* h = handle_of(self);
  return LONG2NUM(guarded([&] { return pdfua::count_figures_without_alt(h->qpdf()); }));
}

// Every Link annotation: page (1-based), target URI (nil for internal links) and description.
static VALUE doc_links(VALUE self) {
  DocumentHandle* h = handle_of(self);
  struct Link {
    int page;
    std::optional<std::string> uri;
    std::optional<std::string> contents;
  };
  auto collect = [&] {
    std::vector<Link> out;
    auto pages = h->qpdf().getAllPages();
    for (size_t i = 0; i < pages.size(); ++i) {
      QPDFObjectHandle annots = pages[i].getKey("/Annots");
      if (!annots.isArray()) continue;
      for (auto annot : annots.aitems()) {
        if (!annot.isDictionary() || !annot.getKey("/Subtype").isNameAndEquals("/Link")) continue;
        Link link{static_cast<int>(i) + 1, std::nullopt, std::nullopt};
        QPDFObjectHandle action = annot.getKey("/A");
        if (action.isDictionary() && action.getKey("/URI").isString()) link.uri = action.getKey("/URI").getUTF8Value();
        if (annot.getKey("/Contents").isString()) link.contents = annot.getKey("/Contents").getUTF8Value();
        out.push_back(link);
      }
    }
    return out;
  };
  return guarded_to_ruby(collect, [](std::vector<Link> const& links) {
    VALUE array = rb_ary_new();
    for (auto const& link : links) {
      VALUE hash = rb_hash_new();
      rb_hash_aset(hash, ID2SYM(rb_intern("page")), INT2NUM(link.page));
      rb_hash_aset(hash, ID2SYM(rb_intern("uri")), link.uri ? ruby_string(*link.uri) : Qnil);
      rb_hash_aset(hash, ID2SYM(rb_intern("contents")), link.contents ? ruby_string(*link.contents) : Qnil);
      rb_ary_push(array, hash);
    }
    return array;
  });
}

// The catalog's XMP metadata as a string, or nil.
static VALUE doc_metadata(VALUE self) {
  DocumentHandle* h = handle_of(self);
  return guarded_to_ruby(
      [&]() -> std::optional<std::string> {
        QPDFObjectHandle metadata = h->qpdf().getRoot().getKey("/Metadata");
        if (!metadata.isStream()) return std::nullopt;
        auto data = metadata.getStreamData(qpdf_dl_generalized);
        return std::string(reinterpret_cast<char const*>(data->getBuffer()), data->getSize());
      },
      [](std::optional<std::string> const& xmp) { return xmp ? ruby_string(*xmp) : Qnil; });
}

// The structure tree's RoleMap as {"Aside" => "Sect", ...}.
static VALUE doc_role_map(VALUE self) {
  DocumentHandle* h = handle_of(self);
  return guarded_to_ruby(
      [&] {
        std::map<std::string, std::string> out;
        QPDFObjectHandle tree = h->qpdf().getRoot().getKey("/StructTreeRoot");
        QPDFObjectHandle map = tree.isDictionary() ? tree.getKey("/RoleMap") : QPDFObjectHandle::newNull();
        if (map.isDictionary()) {
          for (auto [key, value] : map.ditems()) {
            if (value.isName()) out[key.substr(1)] = value.getName().substr(1);
          }
        }
        return out;
      },
      [](std::map<std::string, std::string> const& roles) {
        VALUE hash = rb_hash_new();
        for (auto const& [key, value] : roles) rb_hash_aset(hash, ruby_string(key), ruby_string(value));
        return hash;
      });
}

// rb_get_kwargs raises ArgumentError for an unknown keyword (a typo like tittle:) and leaves Qundef
// for one not given; this turns that into nil.
static VALUE given(VALUE value) { return value == Qundef ? Qnil : value; }

static VALUE doc_add_pdfua_identification(int argc, VALUE* argv, VALUE self) {
  VALUE kwargs = Qnil;
  rb_scan_args(argc, argv, ":", &kwargs);
  ID keys[1] = {rb_intern("title")};
  VALUE values[1];
  rb_get_kwargs(kwargs, keys, 0, 1, values);
  VALUE title = checked_optional_string(given(values[0]));
  DocumentHandle* h = handle_of(self);
  bool changed = guarded([&] { return pdfua::add_pdfua_identification(h->qpdf(), cpp_optional_string(title)); });
  RB_GC_GUARD(title);
  return changed ? Qtrue : Qfalse;
}

static VALUE report_hash(pdfua::Report const& report);

static VALUE doc_apply_pdfua_fixes(int argc, VALUE* argv, VALUE self) {
  VALUE kwargs = Qnil;
  rb_scan_args(argc, argv, ":", &kwargs);
  ID keys[2] = {rb_intern("link_texts"), rb_intern("title")};
  VALUE values[2];
  rb_get_kwargs(kwargs, keys, 0, 2, values);
  VALUE pairs = checked_link_texts(given(values[0]));
  VALUE title = checked_optional_string(given(values[1]));
  DocumentHandle* h = handle_of(self);

  VALUE out = guarded_to_ruby(
      [&] { return pdfua::apply(h->qpdf(), cpp_link_texts(pairs), cpp_optional_string(title)); }, report_hash);
  RB_GC_GUARD(pairs);
  RB_GC_GUARD(title);
  return out;
}

static VALUE report_hash(pdfua::Report const& report) {
  VALUE hash = rb_hash_new();
  rb_hash_aset(hash, ID2SYM(rb_intern("decorations")), LONG2NUM(report.decorations));
  rb_hash_aset(hash, ID2SYM(rb_intern("artifacts")), counts_hash(report.artifacts));
  rb_hash_aset(hash, ID2SYM(rb_intern("links")), LONG2NUM(report.links));
  rb_hash_aset(hash, ID2SYM(rb_intern("list_bodies")), LONG2NUM(report.list_bodies));
  rb_hash_aset(hash, ID2SYM(rb_intern("roles")), LONG2NUM(report.roles));
  rb_hash_aset(hash, ID2SYM(rb_intern("figure_groups")), LONG2NUM(report.figure_groups));
  rb_hash_aset(hash, ID2SYM(rb_intern("figures_without_alt")), LONG2NUM(report.figures_without_alt));
  rb_hash_aset(hash, ID2SYM(rb_intern("identified")), report.identified ? Qtrue : Qfalse);
  rb_hash_aset(hash, ID2SYM(rb_intern("unidentified_reason")),
               report.unidentified_reason ? ruby_string(*report.unidentified_reason) : Qnil);
  return hash;
}

// ---- encryption ------------------------------------------------------------------------------

static VALUE doc_encrypt(int argc, VALUE* argv, VALUE self) {
  VALUE kwargs = Qnil;
  ID keys[12] = {rb_intern("user_pw"),       rb_intern("owner_pw"),          rb_intern("encryption_revision"),
                 rb_intern("allow_print"),   rb_intern("allow_modify"),      rb_intern("allow_extract"),
                 rb_intern("accessibility"), rb_intern("assemble"),          rb_intern("annotate_and_form"),
                 rb_intern("form_filling"),  rb_intern("encrypt_metadata"), rb_intern("use_aes")};
  VALUE values[12];
  rb_scan_args(argc, argv, ":", &kwargs);
  rb_get_kwargs(kwargs, keys, 0, 12, values);

  // rb_get_kwargs leaves Qundef for keywords not given.
  auto or_default = [&](int i, VALUE fallback) { return values[i] == Qundef ? fallback : values[i]; };
  VALUE user_pw = checked_string(or_default(0, rb_str_new_cstr("")));
  VALUE owner_pw = checked_string(or_default(1, rb_str_new_cstr("")));
  int revision = NUM2INT(or_default(2, INT2NUM(4)));
  auto print = static_cast<qpdf_r3_print_e>(NUM2INT(or_default(3, INT2NUM(qpdf_r3p_low))));

  DocumentHandle* h = handle_of(self);
  guarded([&] {
    h->set_encryption(cpp_string(user_pw), cpp_string(owner_pw), revision, print, RTEST(or_default(4, Qfalse)),
                      RTEST(or_default(5, Qfalse)), RTEST(or_default(6, Qtrue)), RTEST(or_default(7, Qfalse)),
                      RTEST(or_default(8, Qfalse)), RTEST(or_default(9, Qfalse)), RTEST(or_default(10, Qtrue)),
                      RTEST(or_default(11, Qtrue)));
  });
  RB_GC_GUARD(user_pw);
  RB_GC_GUARD(owner_pw);
  return Qnil;
}

// ---- definitions -----------------------------------------------------------------------------

extern "C" {
RUBY_FUNC_EXPORTED void Init_qpdf_ruby(void) {
  rb_mQpdfRuby = rb_define_module("QpdfRuby");
  rb_eQpdfRubyError = rb_define_class_under(rb_mQpdfRuby, "Error", rb_eRuntimeError);
  rb_cDocument = rb_define_class_under(rb_mQpdfRuby, "Document", rb_cObject);
  rb_define_alloc_func(rb_cDocument, doc_alloc);

  rb_define_method(rb_cDocument, "initialize", RUBY_METHOD_FUNC(doc_initialize), -1);
  rb_define_singleton_method(rb_cDocument, "from_memory", RUBY_METHOD_FUNC(doc_from_memory), -1);
  rb_define_method(rb_cDocument, "write", RUBY_METHOD_FUNC(doc_write), 1);
  rb_define_method(rb_cDocument, "to_memory", RUBY_METHOD_FUNC(doc_to_memory), 0);

  rb_define_method(rb_cDocument, "show_structure", RUBY_METHOD_FUNC(doc_show_structure), 0);
  rb_define_method(rb_cDocument, "ensure_bbox", RUBY_METHOD_FUNC(doc_ensure_bbox), 0);

  rb_define_method(rb_cDocument, "mark_untagged_content_as_artifacts",
                   RUBY_METHOD_FUNC(doc_mark_untagged_content_as_artifacts), 0);
  rb_define_method(rb_cDocument, "mark_paths_as_artifacts", RUBY_METHOD_FUNC(doc_mark_paths_as_artifacts), 0);
  rb_define_method(rb_cDocument, "untagged_content", RUBY_METHOD_FUNC(doc_untagged_content), 0);
  rb_define_method(rb_cDocument, "describe_links", RUBY_METHOD_FUNC(doc_describe_links), -1);
  rb_define_method(rb_cDocument, "artifact_tagged_decorations", RUBY_METHOD_FUNC(doc_artifact_tagged_decorations), 0);
  rb_define_method(rb_cDocument, "wrap_list_bodies", RUBY_METHOD_FUNC(doc_wrap_list_bodies), 0);
  rb_define_method(rb_cDocument, "parent_tree_mismatches", RUBY_METHOD_FUNC(doc_parent_tree_mismatches), 0);
  rb_define_method(rb_cDocument, "map_nonstandard_roles", RUBY_METHOD_FUNC(doc_map_nonstandard_roles), 0);
  rb_define_method(rb_cDocument, "retag_grouping_figures", RUBY_METHOD_FUNC(doc_retag_grouping_figures), 0);
  rb_define_method(rb_cDocument, "figures_without_alt", RUBY_METHOD_FUNC(doc_figures_without_alt), 0);
  rb_define_method(rb_cDocument, "add_pdfua_identification", RUBY_METHOD_FUNC(doc_add_pdfua_identification), -1);
  rb_define_method(rb_cDocument, "apply_pdfua_fixes", RUBY_METHOD_FUNC(doc_apply_pdfua_fixes), -1);

  rb_define_method(rb_cDocument, "links", RUBY_METHOD_FUNC(doc_links), 0);
  rb_define_method(rb_cDocument, "metadata", RUBY_METHOD_FUNC(doc_metadata), 0);
  rb_define_method(rb_cDocument, "role_map", RUBY_METHOD_FUNC(doc_role_map), 0);

  rb_define_method(rb_cDocument, "encrypt", RUBY_METHOD_FUNC(doc_encrypt), -1);

  rb_define_const(rb_mQpdfRuby, "PRINT_FULL", INT2NUM(qpdf_r3p_full));
  rb_define_const(rb_mQpdfRuby, "PRINT_LOW", INT2NUM(qpdf_r3p_low));
  rb_define_const(rb_mQpdfRuby, "PRINT_NONE", INT2NUM(qpdf_r3p_none));
}
}  // extern "C"
