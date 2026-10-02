#pragma once

#include <ruby.h>

#include <cstring>
#include <exception>
#include <string>
#include <utility>

extern VALUE rb_eQpdfRubyError;  // QpdfRuby::Error, defined in Init_qpdf_ruby

namespace qpdf_ruby {

// Runs C++ code from a Ruby method. A C++ exception becomes a QpdfRuby::Error only after the catch
// block - and every C++ object with a destructor - is gone: rb_raise longjmps, and doing that from
// inside a catch block or across live objects skips the unwinding C++ relies on (leaks, or worse).
template <typename Fn>
auto guarded(Fn&& fn) -> decltype(fn()) {
  char message[1024] = {0};
  {
    std::string what;
    try {
      return std::forward<Fn>(fn)();
    } catch (const std::exception& e) {
      what = e.what();
    } catch (...) {
      what = "unknown C++ exception";
    }
    std::strncpy(message, what.c_str(), sizeof(message) - 1);
  }
  rb_raise(rb_eQpdfRubyError, "%s", message);
}

}  // namespace qpdf_ruby
