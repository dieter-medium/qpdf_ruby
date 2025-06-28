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
VALUE rb_eQpdfRubyError;

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

static VALUE doc_initialize(int argc, VALUE* argv, VALUE self) {
  VALUE filename, password;

  filename = Qnil;
  password = Qnil;

  rb_scan_args(argc, argv, "11", &filename, &password);  // 1 required, 1 optional

  Check_Type(filename, T_STRING);

  const char* pw = "";
  if (!NIL_P(password)) {
    Check_Type(password, T_STRING);
    pw = StringValueCStr(password);
  }

  try {
    DocumentHandle* h = qpdf_ruby::qpdf_ruby_open(StringValueCStr(filename), pw);
    if (!h) rb_sys_fail("qpdf_ruby_open");
    DATA_PTR(self) = h;
  } catch (const std::exception& e) {
    rb_raise(rb_eQpdfRubyError, "%s", e.what());
  }

  return self;
}

static VALUE doc_write(VALUE self, VALUE out_filename) {
  Check_Type(out_filename, T_STRING);
  DocumentHandle* h;
  Data_Get_Struct(self, DocumentHandle, h);
  if (qpdf_ruby::qpdf_ruby_write(h, StringValueCStr(out_filename)) == -1) rb_sys_fail("qpdf_ruby_write");

  return Qnil;
}

VALUE qpdf_ruby_write_memory(DocumentHandle* h) {
  if (!h) rb_sys_fail("Bad handle");
  std::string bytes;

  try {
    bytes = h->write_to_memory();
  } catch (const std::exception& e) {
    rb_raise(rb_eQpdfRubyError, "%s", e.what());
  }
  return rb_str_new(bytes.data(), bytes.size());
}

static VALUE doc_from_memory(VALUE klass, VALUE str, VALUE password) {
  Check_Type(str, T_STRING);
  Check_Type(password, T_STRING);
  DocumentHandle* h;
  try {
    h = qpdf_ruby_open_memory("ruby-memory", reinterpret_cast<unsigned char const*>(RSTRING_PTR(str)), RSTRING_LEN(str),
                              StringValueCStr(password));
  } catch (const std::exception& e) {
    rb_raise(rb_eQpdfRubyError, "%s", e.what());
  }

  if (!h) rb_sys_fail("qpdf_ruby_open_memory");

  VALUE obj = Data_Wrap_Struct(klass, 0, doc_free, h);
  return obj;
}

static VALUE doc_to_memory(VALUE self) {
  DocumentHandle* h;
  Data_Get_Struct(self, DocumentHandle, h);
  return qpdf_ruby_write_memory(h);  // returns a Ruby ::String
}

VALUE rb_qpdf_doc_set_encryption(int argc, VALUE* argv, VALUE self) {
  VALUE kwargs;
  ID keys[12];
  VALUE values[12] = {
      rb_str_new_cstr(""),  // user_pw
      rb_str_new_cstr(""),  // owner_pw
      INT2NUM(4),           // encryption_revision
      INT2NUM(1),           // allow_print
      Qfalse,               // allow_modify
      Qfalse,               // allow_extract
      Qtrue,                // accessibility
      Qfalse,               // assemble
      Qfalse,               // annotate_and_form
      Qfalse,               // form_filling
      Qtrue,                // encrypt_metadata
      Qtrue                 // use_aes
  };

  keys[0] = rb_intern("user_pw");
  keys[1] = rb_intern("owner_pw");
  keys[2] = rb_intern("encryption_revision");
  keys[3] = rb_intern("allow_print");
  keys[4] = rb_intern("allow_modify");
  keys[5] = rb_intern("allow_extract");
  keys[6] = rb_intern("accessibility");
  keys[7] = rb_intern("assemble");
  keys[8] = rb_intern("annotate_and_form");
  keys[9] = rb_intern("form_filling");
  keys[10] = rb_intern("encrypt_metadata");
  keys[11] = rb_intern("use_aes");

  rb_scan_args(argc, argv, ":", &kwargs);

  // Get values for each keyword, or Qnil if missing
  rb_get_kwargs(kwargs, keys, 0, 12, values);

  DocumentHandle* h;
  Data_Get_Struct(self, DocumentHandle, h);

  h->set_encryption(StringValueCStr(values[0]),                        // user_pw
                    StringValueCStr(values[1]),                        // owner_pw
                    NUM2INT(values[2]),                                // r
                    static_cast<qpdf_r3_print_e>(NUM2INT(values[3])),  // allow_print
                    RTEST(values[4]),                                  // allow_modify
                    RTEST(values[5]),                                  // allow_extract
                    RTEST(values[6]),                                  // accessibility
                    RTEST(values[7]),                                  // assemble
                    RTEST(values[8]),                                  // annotate_and_form
                    RTEST(values[9]),                                  // form_filling
                    RTEST(values[10]),                                 // encrypt_metadata
                    RTEST(values[11])                                  // use_aes
  );
  return Qnil;
}

RUBY_FUNC_EXPORTED "C" void Init_qpdf_ruby(void) {
  rb_mQpdfRuby = rb_define_module("QpdfRuby");
  rb_cDocument = rb_define_class_under(rb_mQpdfRuby, "Document", rb_cObject);
  rb_eQpdfRubyError = rb_define_class_under(rb_mQpdfRuby, "Error", rb_eStandardError);

  rb_define_alloc_func(rb_cDocument, doc_alloc);

  rb_define_method(rb_cDocument, "initialize", RUBY_METHOD_FUNC(doc_initialize), -1);
  rb_define_singleton_method(rb_cDocument, "from_memory", RUBY_METHOD_FUNC(doc_from_memory), 2);

  rb_define_method(rb_cDocument, "write", RUBY_METHOD_FUNC(doc_write), 1);
  rb_define_method(rb_cDocument, "to_memory", RUBY_METHOD_FUNC(doc_to_memory), 0);

  rb_define_method(rb_cDocument, "mark_paths_as_artifacts", RUBY_METHOD_FUNC(rb_qpdf_mark_paths_as_artifacts), 0);
  rb_define_method(rb_cDocument, "ensure_bbox", RUBY_METHOD_FUNC(rb_qpdf_ensure_bboxs), 0);
  rb_define_method(rb_cDocument, "show_structure", RUBY_METHOD_FUNC(rb_qpdf_get_structure_string), 0);
  rb_define_method(rb_cDocument, "encrypt", RUBY_METHOD_FUNC(rb_qpdf_doc_set_encryption), -1);

  rb_define_const(rb_mQpdfRuby, "PRINT_FULL", INT2NUM(qpdf_r3p_full));
  rb_define_const(rb_mQpdfRuby, "PRINT_LOW", INT2NUM(qpdf_r3p_low));
  rb_define_const(rb_mQpdfRuby, "PRINT_NONE", INT2NUM(qpdf_r3p_none));
}
