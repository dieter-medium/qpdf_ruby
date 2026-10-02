#!/bin/bash
# Builds the extension with AddressSanitizer in a scratch copy of the working tree and runs the
# specs under it, one of two checks of the binding's memory handling (ext/qpdf_ruby/ruby_guard.hpp):
#   - Use-after-free, which often survives a plain run (the allocator hands the freed block back).
#   - Leaks, through LeakSanitizer at exit. MRI never frees its own heap, so on its own LSan reports
#     every Ruby object (24,000 allocations for 11 examples, measured 2026-10-02); bin/lsan.supp
#     suppresses Ruby's own allocation sites, which leaves the extension's leaks - it found one, the
#     Buffer write_to_memory never deleted.
# What LSan does NOT see: a C++ object whose destructor a Ruby raise longjmped over. Stale pointers
# left in dead stack memory make those blocks look reachable (2,000 such leaks on d472736: none
# reported). spec/qpdf_ruby/document_error_path_memory_spec.rb covers that class by watching RSS.
#
#   bin/asan-rspec.sh [RSPEC_ARGS...]    # default: everything except :verapdf and :memory
#
# ASAN_OPTIONS / LSAN_OPTIONS from the environment win (e.g. ASAN_OPTIONS=detect_leaks=0 for
# use-after-free only). Exits non-zero on a failing example, a memory error or a leak.
#
# Left out by default: the veraPDF examples start Java, which would inherit LD_PRELOAD, and the
# :memory examples measure RSS, which ASan's quarantine of freed memory inflates on its own.
# Needs gcc with libasan; the scratch copy lives under tmp/ (not /tmp, which may be noexec).
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "${repo}/tmp"
work="$(mktemp -d "${repo}/tmp/asan.XXXXXX")"
trap '[[ -n "${work}" && "${work}" == "${repo}/tmp/asan."* ]] && rm -rf "${work}"' EXIT

libasan="$(gcc -print-file-name=libasan.so)"
libstdcxx="$(gcc -print-file-name=libstdc++.so)"
if [[ "${libasan}" != /* || ! -e "${libasan}" ]]; then
  printf 'error: gcc has no libasan.so\n' >&2
  exit 1
fi

cd "${repo}"
git ls-files -co --exclude-standard | grep -v -e '^tmp/' -e '\.so$' -e '^Gemfile\.lock$' | tar -cf - -T - | tar -xf - -C "${work}"
mkdir -p "${work}/tmp/build"
cd "${work}/tmp/build"
ruby "${work}/ext/qpdf_ruby/extconf.rb" --with-cflags="-fsanitize=address -fno-omit-frame-pointer -g" \
  --with-ldflags="-fsanitize=address" >/dev/null
# mkmf passes --with-cflags to C only; the extension is C++.
sed -i 's/^CXXFLAGS = \(.*\)$/CXXFLAGS = \1 -fsanitize=address -fno-omit-frame-pointer -g/' Makefile
make -j4 >/dev/null
cp qpdf_ruby.so "${work}/lib/qpdf_ruby/"

cd "${work}"
if [[ $# -eq 0 ]]; then
  set -- --tag '~verapdf' --tag '~memory'
fi
# libstdc++ is preloaded too: without it ASan cannot find the real __cxa_throw in a C host process
# and aborts on the first C++ exception.
LD_PRELOAD="${libasan} ${libstdcxx}" ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}" \
  LSAN_OPTIONS="${LSAN_OPTIONS:-suppressions=${repo}/bin/lsan.supp:print_suppressions=0}" \
  bundle exec rspec "$@"
