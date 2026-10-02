#!/bin/bash
# Builds the extension with AddressSanitizer in a scratch copy of the working tree and runs the
# specs under it. A use-after-free often survives a plain run - the allocator hands the freed block
# straight back - so this is how to check the binding's memory handling (ext/qpdf_ruby/ruby_guard.hpp).
#
#   bin/asan-rspec.sh [RSPEC_ARGS...]    # default: everything except the veraPDF examples
#
# The veraPDF examples are left out by default: they start Java, which would inherit LD_PRELOAD.
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
  set -- --tag '~verapdf'
fi
# libstdc++ is preloaded too: without it ASan cannot find the real __cxa_throw in a C host process
# and aborts on the first C++ exception.
LD_PRELOAD="${libasan} ${libstdcxx}" ASAN_OPTIONS="detect_leaks=0:halt_on_error=1" bundle exec rspec "$@"
