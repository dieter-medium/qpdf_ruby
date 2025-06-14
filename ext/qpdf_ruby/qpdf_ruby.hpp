#ifndef QPDF_RUBY_H
#define QPDF_RUBY_H 1

#include "ruby.h"

VALUE rb_qpdf_mark_paths_as_artifacts(VALUE self);
VALUE rb_qpdf_ensure_bboxs(VALUE self);
VALUE rb_qpdf_get_structure_string(VALUE self);

#endif /* QPDF_RUBY_H */
