#pragma once

#include <ruby.h>

#include <cstring>
#include <exception>
#include <string>
#include <type_traits>
#include <utility>

extern VALUE rb_eQpdfRubyError;  // QpdfRuby::Error, defined in Init_qpdf_ruby

namespace qpdf_ruby {

// Ruby raises by longjmp, which skips the destructors of every C++ object in the frames it leaves.
// So no Ruby exception may ever travel while a C++ object with a destructor is alive. The methods in
// qpdf_ruby.cpp keep to three rules:
//   1. Check and convert arguments as Ruby values first, before any C++ object exists (Check_Type,
//      #to_s, NUM2INT may all raise).
//   2. Build C++ values (std::string, containers) only inside the lambda given to guarded.
//   3. Turn a C++ result into Ruby values only through guarded_to_ruby.

// Runs C++ code from a Ruby method. A C++ exception becomes a QpdfRuby::Error only after the catch
// block - and every C++ object with a destructor - is gone.
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

// guarded, then `build(result)` to make the Ruby return value. Allocating Ruby objects can raise
// (NoMemoryError); rb_protect stops that while the C++ result is alive, the result is destroyed, and
// only then is the exception re-raised.
template <typename Fn, typename Build>
VALUE guarded_to_ruby(Fn&& fn, Build&& build) {
  int state = 0;
  VALUE out = Qnil;
  {
    auto result = guarded(std::forward<Fn>(fn));
    struct Call {
      decltype(result)* value;
      std::remove_reference_t<Build>* to_ruby;
    } call{&result, &build};
    out = rb_protect(
        [](VALUE arg) -> VALUE {
          auto* c = reinterpret_cast<Call*>(arg);
          return (*c->to_ruby)(*c->value);
        },
        reinterpret_cast<VALUE>(&call), &state);
  }
  if (state) rb_jump_tag(state);
  return out;
}

}  // namespace qpdf_ruby
