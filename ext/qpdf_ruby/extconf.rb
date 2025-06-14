# frozen_string_literal: true

require "mkmf"

# Makes all symbols private by default to avoid unintended conflict
# with other gems. To explicitly export symbols you can use RUBY_FUNC_EXPORTED
# selectively, or entirely remove this flag.
append_cflags("-fvisibility=hidden")

if with_config("qpdf-dir")
  qpdf_include_dir, qpdf_lib_dir = dir_config("qpdf")
  $INCFLAGS << " -I#{qpdf_include_dir}"
  $LDFLAGS << " -L#{qpdf_lib_dir} -lqpdf"
else
  $LDFLAGS << " -lqpdf"
end

if RbConfig::CONFIG["host_os"] =~ /darwin/
  $LDFLAGS << " -Wl,-search_paths_first -Wl,-headerpad_max_install_names -Wl,-multiply_defined,suppress"
  $LDFLAGS << " -Wl,-undefined,dynamic_lookup"
end

$CXXFLAGS << " -std=c++17"

create_makefile("qpdf_ruby/qpdf_ruby")
