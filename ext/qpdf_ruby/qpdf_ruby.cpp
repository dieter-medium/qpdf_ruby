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

static std::string string_arg(VALUE value) {
  Check_Type(value, T_STRING);
  return std::string(RSTRING_PTR(value), RSTRING_LEN(value));
}

// ---- opening and writing ---------------------------------------------------------------------

static VALUE doc_initialize(int argc, VALUE* argv, VALUE self) {
  VALUE filename = Qnil, password = Qnil;
  rb_scan_args(argc, argv, "11", &filename, &password);
  std::string path = string_arg(filename);
  std::string pw = NIL_P(password) ? "" : string_arg(password);

  DATA_PTR(self) = guarded([&] { return DocumentHandle::open(path, pw).release(); });
  return self;
}

static VALUE doc_from_memory(int argc, VALUE* argv, VALUE klass) {
  VALUE data = Qnil, password = Qnil;
  rb_scan_args(argc, argv, "11", &data, &password);
  Check_Type(data, T_STRING);
  std::vector<unsigned char> bytes(RSTRING_PTR(data), RSTRING_PTR(data) + RSTRING_LEN(data));
  std::string pw = NIL_P(password) ? "" : string_arg(password);

  DocumentHandle* h =
      guarded([&] { return DocumentHandle::open_memory("ruby-memory", std::move(bytes), pw).release(); });
  return TypedData_Wrap_Struct(klass, &document_type, h);
}

static VALUE doc_write(VALUE self, VALUE out_filename) {
  DocumentHandle* h = handle_of(self);
  std::string path = string_arg(out_filename);
  guarded([&] { h->write(path); });
  return Qnil;
}

static VALUE doc_to_memory(VALUE self) {
  DocumentHandle* h = handle_of(self);
  std::string bytes = guarded([&] { return h->write_to_memory(); });
  return rb_str_new(bytes.data(), static_cast<long>(bytes.size()));
}

// ---- structure tree --------------------------------------------------------------------------

static QPDFObjectHandle struct_kids(QPDF& pdf) {
  QPDFObjectHandle struct_root = pdf.getRoot().getKey("/StructTreeRoot");
  if (!struct_root.isDictionary()) throw std::runtime_error("No StructTreeRoot found");
  return struct_root.getKey("/K");
}

static VALUE doc_show_structure(VALUE self) {
  DocumentHandle* h = handle_of(self);
  std::string result = guarded([&] {
    QPDF& pdf = h->qpdf();
    QPDFObjectHandle kids = struct_kids(pdf);
    PDFStructWalker walker;
    walker.buildPageObjectMap(pdf);
    std::string out;
    if (kids.isArray()) {
      for (auto const& kid : kids.aitems()) out += walker.get_structure_as_string(kid);
    } else {
      out = walker.get_structure_as_string(kids);
    }
    return out;
  });
  return rb_utf8_str_new(result.data(), static_cast<long>(result.size()));
}

static VALUE doc_ensure_bbox(VALUE self) {
  DocumentHandle* h = handle_of(self);
  guarded([&] {
    QPDF& pdf = h->qpdf();
    QPDFObjectHandle kids = struct_kids(pdf);
    PDFStructWalker walker(std::cout, find_mcid_bounds(pdf));
    if (kids.isArray()) {
      for (auto const& kid : kids.aitems()) walker.ensureLayoutBBox(kid);
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

static int collect_link_text(VALUE key, VALUE value, VALUE arg) {
  auto* texts = reinterpret_cast<std::map<std::string, std::string>*>(arg);
  VALUE k = rb_obj_as_string(key);
  VALUE v = rb_obj_as_string(value);
  (*texts)[std::string(RSTRING_PTR(k), RSTRING_LEN(k))] = std::string(RSTRING_PTR(v), RSTRING_LEN(v));
  return ST_CONTINUE;
}

static std::map<std::string, std::string> link_texts_arg(VALUE hash) {
  std::map<std::string, std::string> texts;
  if (NIL_P(hash)) return texts;
  Check_Type(hash, T_HASH);
  rb_hash_foreach(hash, collect_link_text, reinterpret_cast<VALUE>(&texts));
  return texts;
}

static std::optional<std::string> title_arg(VALUE title) {
  if (NIL_P(title)) return std::nullopt;
  return string_arg(title);
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
  DocumentHandle* h = handle_of(self);
  auto map = link_texts_arg(texts);
  return LONG2NUM(guarded([&] { return pdfua::describe_links(h->qpdf(), map); }));
}

static VALUE doc_wrap_list_bodies(VALUE self) {
  DocumentHandle* h = handle_of(self);
  return LONG2NUM(guarded([&] { return pdfua::wrap_list_bodies(h->qpdf()); }));
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

static VALUE ruby_string(std::string const& value) {
  return rb_utf8_str_new(value.data(), static_cast<long>(value.size()));
}

// Every Link annotation: page (1-based), target URI (nil for internal links) and description.
static VALUE doc_links(VALUE self) {
  DocumentHandle* h = handle_of(self);
  struct Link {
    int page;
    std::optional<std::string> uri;
    std::optional<std::string> contents;
  };
  auto links = guarded([&] {
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
  });
  VALUE array = rb_ary_new();
  for (auto const& link : links) {
    VALUE hash = rb_hash_new();
    rb_hash_aset(hash, ID2SYM(rb_intern("page")), INT2NUM(link.page));
    rb_hash_aset(hash, ID2SYM(rb_intern("uri")), link.uri ? ruby_string(*link.uri) : Qnil);
    rb_hash_aset(hash, ID2SYM(rb_intern("contents")), link.contents ? ruby_string(*link.contents) : Qnil);
    rb_ary_push(array, hash);
  }
  return array;
}

// The catalog's XMP metadata as a string, or nil.
static VALUE doc_metadata(VALUE self) {
  DocumentHandle* h = handle_of(self);
  auto xmp = guarded([&]() -> std::optional<std::string> {
    QPDFObjectHandle metadata = h->qpdf().getRoot().getKey("/Metadata");
    if (!metadata.isStream()) return std::nullopt;
    auto data = metadata.getStreamData(qpdf_dl_generalized);
    return std::string(reinterpret_cast<char const*>(data->getBuffer()), data->getSize());
  });
  return xmp ? ruby_string(*xmp) : Qnil;
}

// The structure tree's RoleMap as {"Aside" => "Sect", ...}.
static VALUE doc_role_map(VALUE self) {
  DocumentHandle* h = handle_of(self);
  auto roles = guarded([&] {
    std::map<std::string, std::string> out;
    QPDFObjectHandle map = h->qpdf().getRoot().getKey("/StructTreeRoot").getKey("/RoleMap");
    if (map.isDictionary()) {
      for (auto const& [key, value] : map.ditems()) {
        if (value.isName()) out[key.substr(1)] = value.getName().substr(1);
      }
    }
    return out;
  });
  VALUE hash = rb_hash_new();
  for (auto const& [key, value] : roles) rb_hash_aset(hash, ruby_string(key), ruby_string(value));
  return hash;
}

static VALUE kwarg(VALUE kwargs, char const* name) {
  if (NIL_P(kwargs)) return Qnil;
  return rb_hash_lookup2(kwargs, ID2SYM(rb_intern(name)), Qnil);
}

static VALUE doc_add_pdfua_identification(int argc, VALUE* argv, VALUE self) {
  VALUE kwargs = Qnil;
  rb_scan_args(argc, argv, ":", &kwargs);
  DocumentHandle* h = handle_of(self);
  auto title = title_arg(kwarg(kwargs, "title"));
  return guarded([&] { return pdfua::add_pdfua_identification(h->qpdf(), title); }) ? Qtrue : Qfalse;
}

static VALUE doc_apply_pdfua_fixes(int argc, VALUE* argv, VALUE self) {
  VALUE kwargs = Qnil;
  rb_scan_args(argc, argv, ":", &kwargs);
  DocumentHandle* h = handle_of(self);
  auto texts = link_texts_arg(kwarg(kwargs, "link_texts"));
  auto title = title_arg(kwarg(kwargs, "title"));

  auto report = guarded([&] { return pdfua::apply(h->qpdf(), texts, title); });
  VALUE hash = rb_hash_new();
  rb_hash_aset(hash, ID2SYM(rb_intern("artifacts")), counts_hash(report.artifacts));
  rb_hash_aset(hash, ID2SYM(rb_intern("links")), LONG2NUM(report.links));
  rb_hash_aset(hash, ID2SYM(rb_intern("list_bodies")), LONG2NUM(report.list_bodies));
  rb_hash_aset(hash, ID2SYM(rb_intern("roles")), LONG2NUM(report.roles));
  rb_hash_aset(hash, ID2SYM(rb_intern("figure_groups")), LONG2NUM(report.figure_groups));
  rb_hash_aset(hash, ID2SYM(rb_intern("figures_without_alt")), LONG2NUM(report.figures_without_alt));
  rb_hash_aset(hash, ID2SYM(rb_intern("identified")), report.identified ? Qtrue : Qfalse);
  return hash;
}

// ---- encryption ------------------------------------------------------------------------------

static VALUE doc_encrypt(int argc, VALUE* argv, VALUE self) {
  VALUE kwargs;
  ID keys[12] = {rb_intern("user_pw"),       rb_intern("owner_pw"),          rb_intern("encryption_revision"),
                 rb_intern("allow_print"),   rb_intern("allow_modify"),      rb_intern("allow_extract"),
                 rb_intern("accessibility"), rb_intern("assemble"),          rb_intern("annotate_and_form"),
                 rb_intern("form_filling"),  rb_intern("encrypt_metadata"), rb_intern("use_aes")};
  VALUE values[12];
  rb_scan_args(argc, argv, ":", &kwargs);
  rb_get_kwargs(kwargs, keys, 0, 12, values);

  // rb_get_kwargs leaves Qundef for keywords not given.
  auto or_default = [&](int i, VALUE fallback) { return values[i] == Qundef ? fallback : values[i]; };
  std::string user_pw = string_arg(or_default(0, rb_str_new_cstr("")));
  std::string owner_pw = string_arg(or_default(1, rb_str_new_cstr("")));
  int revision = NUM2INT(or_default(2, INT2NUM(4)));
  auto print = static_cast<qpdf_r3_print_e>(NUM2INT(or_default(3, INT2NUM(qpdf_r3p_low))));

  DocumentHandle* h = handle_of(self);
  guarded([&] {
    h->set_encryption(user_pw, owner_pw, revision, print, RTEST(or_default(4, Qfalse)), RTEST(or_default(5, Qfalse)),
                      RTEST(or_default(6, Qtrue)), RTEST(or_default(7, Qfalse)), RTEST(or_default(8, Qfalse)),
                      RTEST(or_default(9, Qfalse)), RTEST(or_default(10, Qtrue)), RTEST(or_default(11, Qtrue)));
  });
  return Qnil;
}

// ---- definitions -----------------------------------------------------------------------------

extern "C" __attribute__((visibility("default"))) void Init_qpdf_ruby(void) {
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
  rb_define_method(rb_cDocument, "wrap_list_bodies", RUBY_METHOD_FUNC(doc_wrap_list_bodies), 0);
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
