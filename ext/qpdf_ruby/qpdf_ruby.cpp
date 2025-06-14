#include "qpdf_ruby.hpp"
#include "pdf_struct_walker.hpp"
#include "struct_node.hpp"
#include "pdf_image_mapper.hpp"
#include "document_handle.hpp"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include <iostream>
#include <stdexcept>  // For std::exception (if you add try-catch)
#include <vector>     // For std::vector
#include <string>     // For std::string
#include <regex>

VALUE rb_mQpdfRuby;
VALUE rb_cDocument;

using namespace qpdf_ruby;

VALUE rb_qpdf_get_structure_string(VALUE self) {
  DocumentHandle* h;
  Data_Get_Struct(self, DocumentHandle, h);

  try {
    QPDF& pdf = h->qpdf();

    QPDFObjectHandle catalog = pdf.getRoot();
    QPDFObjectHandle struct_root = catalog.getKey("/StructTreeRoot");
    if (!struct_root.isDictionary()) {
      rb_raise(rb_eRuntimeError, "No StructTreeRoot found");
    }
    QPDFObjectHandle topKids = struct_root.getKey("/K");

    PDFStructWalker walker(std::cout);  // For now, std::cout, unless you pass another stream
    walker.buildPageObjectMap(pdf);

    std::string result;
    if (topKids.isArray()) {
      for (int i = 0; i < topKids.getArrayNItems(); ++i) {
        result += walker.get_structure_as_string(topKids.getArrayItem(i));
      }
    } else {
      result = walker.get_structure_as_string(topKids);
    }

    return rb_str_new(result.c_str(), result.length());
  } catch (const std::exception& e) {
    rb_raise(rb_eRuntimeError, "Error: %s", e.what());
  }
  return Qnil;
}

VALUE rb_qpdf_mark_paths_as_artifacts(VALUE self) {
  DocumentHandle* h;
  Data_Get_Struct(self, DocumentHandle, h);
  try {
    QPDF& pdf = h->qpdf();

    std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
    std::regex path_regex(R"((?:[-+]?\d*\.?\d+(?:e[-+]?\d+)?\s+){4}re\s+(?:S|s|f\*?|F|B\*?|b\*?|n))",
                          std::regex::ECMAScript | std::regex::optimize);

    for (auto& page_obj : pages) {
      QPDFPageObjectHelper poh(page_obj);
      std::vector<QPDFObjectHandle> contents = poh.getPageContents();
      std::vector<QPDFObjectHandle> new_contents_array;

      for (auto& content_stream : contents) {
        if (content_stream.isStream()) {
          // Use std::shared_ptr instead of PointerHolder
          std::shared_ptr<Buffer> stream_buffer = content_stream.getStreamData();
          std::string stream_data_str(reinterpret_cast<char*>(stream_buffer->getBuffer()), stream_buffer->getSize());

          std::string new_stream_data_str = std::regex_replace(stream_data_str, path_regex, "/Artifact BMC\n$&\nEMC");

          // Create a new Buffer with std::shared_ptr
          std::shared_ptr<Buffer> new_buffer = std::make_shared<Buffer>(new_stream_data_str.length());
          memcpy(new_buffer->getBuffer(), new_stream_data_str.data(), new_stream_data_str.length());

          QPDFObjectHandle new_stream = QPDFObjectHandle::newStream(&pdf, new_buffer);
          new_contents_array.push_back(new_stream);
        } else {
          new_contents_array.push_back(content_stream);
        }
      }

      if (new_contents_array.size() == 1) {
        page_obj.replaceKey("/Contents", new_contents_array[0]);
      } else {
        page_obj.replaceKey("/Contents", QPDFObjectHandle::newArray(new_contents_array));
      }
    }
  } catch (const QPDFExc& e) {  // Catching specific QPDF exceptions is good
    rb_raise(rb_eRuntimeError, "QPDF Error: %s (filename: %s)", e.what(), e.getFilename().c_str());
  } catch (const std::exception& e) {  // Fallback for other standard exceptions
    rb_raise(rb_eRuntimeError, "Error: %s", e.what());
  }

  return Qnil;
}

VALUE rb_qpdf_ensure_bboxs(VALUE self) {
  DocumentHandle* h;
  Data_Get_Struct(self, DocumentHandle, h);

  try {
    QPDF& pdf = h->qpdf();

    QPDFObjectHandle catalog = pdf.getRoot();
    QPDFObjectHandle struct_root = catalog.getKey("/StructTreeRoot");
    if (!struct_root.isDictionary()) {
      rb_raise(rb_eRuntimeError, "No StructTreeRoot found");
    }
    QPDFObjectHandle topKids = struct_root.getKey("/K");

    PDFImageMapper finder(0);

    finder.find(pdf);

    std::unordered_map<int, std::array<double, 4>> mcid2bbox;
    for (const auto& kv : finder.getImageMap()) {
      if (kv.second.mcid >= 0) mcid2bbox[kv.second.mcid] = kv.second.bbox;
    }

    PDFStructWalker walker(std::cout, mcid2bbox);  // For now, std::cout, unless you pass another stream

    if (topKids.isArray()) {
      for (int i = 0; i < topKids.getArrayNItems(); ++i) {
        walker.ensureLayoutBBox(topKids.getArrayItem(i));
      }
    } else {
      walker.ensureLayoutBBox(topKids);
    }
  } catch (const std::exception& e) {
    rb_raise(rb_eRuntimeError, "Error: %s", e.what());
  }
  return Qnil;
}

static void doc_free(void* ptr) { qpdf_ruby::qpdf_ruby_close(static_cast<qpdf_ruby::DocumentHandle*>(ptr)); }

static VALUE doc_alloc(VALUE klass) { return Data_Wrap_Struct(klass, /* mark */ 0, doc_free, nullptr); }

static VALUE doc_initialize(VALUE self, VALUE filename) {
  Check_Type(filename, T_STRING);
  DocumentHandle* h = qpdf_ruby::qpdf_ruby_open(StringValueCStr(filename));
  if (!h) rb_sys_fail("qpdf_ruby_open");
  DATA_PTR(self) = h;

  return self;
}

static VALUE doc_write(VALUE self, VALUE out_filename) {
  DocumentHandle* h;
  Data_Get_Struct(self, DocumentHandle, h);
  if (qpdf_ruby::qpdf_ruby_write(h, StringValueCStr(out_filename)) == -1) rb_sys_fail("qpdf_ruby_write");

  return Qnil;
}

VALUE qpdf_ruby_write_memory(DocumentHandle* h) {
  if (!h) rb_sys_fail("Bad handle");
  std::string bytes = h->write_to_memory();
  return rb_str_new(bytes.data(), bytes.size());
}

static VALUE doc_from_memory(VALUE klass, VALUE str, VALUE password) {
  Check_Type(str, T_STRING);
  Check_Type(password, T_STRING);

  DocumentHandle* h = qpdf_ruby_open_memory("ruby-memory", reinterpret_cast<unsigned char const*>(RSTRING_PTR(str)),
                                            RSTRING_LEN(str), StringValueCStr(password));

  if (!h) rb_sys_fail("qpdf_ruby_open_memory");

  VALUE obj = Data_Wrap_Struct(klass, 0, doc_free, h);
  return obj;
}

static VALUE doc_to_memory(VALUE self) {
  DocumentHandle* h;
  Data_Get_Struct(self, DocumentHandle, h);
  return qpdf_ruby_write_memory(h);  // returns a Ruby ::String
}

RUBY_FUNC_EXPORTED "C" void Init_qpdf_ruby(void) {
  rb_mQpdfRuby = rb_define_module("QpdfRuby");
  rb_cDocument = rb_define_class_under(rb_mQpdfRuby, "Document", rb_cObject);

  rb_define_alloc_func(rb_cDocument, doc_alloc);

  rb_define_method(rb_cDocument, "initialize", RUBY_METHOD_FUNC(doc_initialize), 1);
  rb_define_singleton_method(rb_cDocument, "from_memory", RUBY_METHOD_FUNC(doc_from_memory), 2);

  rb_define_method(rb_cDocument, "write", RUBY_METHOD_FUNC(doc_write), 1);
  rb_define_method(rb_cDocument, "to_memory", RUBY_METHOD_FUNC(doc_to_memory), 0);

  rb_define_method(rb_cDocument, "mark_paths_as_artifacts", RUBY_METHOD_FUNC(rb_qpdf_mark_paths_as_artifacts), 0);
  rb_define_method(rb_cDocument, "ensure_bbox", RUBY_METHOD_FUNC(rb_qpdf_ensure_bboxs), 0);
  rb_define_method(rb_cDocument, "show_structure", RUBY_METHOD_FUNC(rb_qpdf_get_structure_string), 0);
}
