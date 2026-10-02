#!/bin/bash
# Installs the veraPDF CLI, which spec/acceptance/chromium_pdfua_spec.rb runs to check the fixed
# PDFs against PDF/UA-1. Not packaged for Debian/Ubuntu: the upstream greenfield installer is an
# IzPack jar, run headless with the CLI pack only. Needs java, curl, unzip and sha256sum.
#
#   bin/install-verapdf.sh [INSTALL_DIR]    # default: $HOME/verapdf; prints the verapdf path
#
# The sha256 pins the release checked against its PGP signature (key
# 13DD102B4DD69354D12DE5A83184863278B17FE7, Carl Wilson <techlead@verapdf.org>) on 2026-09-29;
# re-check the signature when bumping.
set -euo pipefail

VERAPDF_VERSION="1.30.2"
VERAPDF_SHA256="6cc6341cb1af644044054b81f00a6590a7918abb18f762243de115258bcad838"

usage() {
  printf 'usage: %s [INSTALL_DIR]\n' "$(basename "$0")" >&2
  printf '  INSTALL_DIR  absolute directory to install into (default: %s)\n' "${HOME}/verapdf" >&2
}

case "${1:-}" in
  -h|--help) usage; exit 0 ;;
esac
if [[ $# -gt 1 ]]; then
  usage
  exit 1
fi

install_dir="${1:-${HOME}/verapdf}"
if [[ "${install_dir}" != /* ]]; then
  printf 'error: INSTALL_DIR must be an absolute path, got %s\n' "${install_dir}" >&2
  exit 1
fi

for tool in java curl unzip sha256sum; do
  if ! command -v "${tool}" >/dev/null; then
    printf 'error: %s is required\n' "${tool}" >&2
    exit 1
  fi
done

tmp_dir="$(mktemp -d)"
trap 'rm -rf "${tmp_dir}"' EXIT

zip_name="verapdf-greenfield-${VERAPDF_VERSION}-installer.zip"
series="$(printf '%s' "${VERAPDF_VERSION}" | cut -d. -f1-2)"
curl -fsSL -o "${tmp_dir}/${zip_name}" "https://software.verapdf.org/releases/${series}/${zip_name}"
printf '%s  %s\n' "${VERAPDF_SHA256}" "${tmp_dir}/${zip_name}" | sha256sum -c - >&2
unzip -q "${tmp_dir}/${zip_name}" -d "${tmp_dir}/unpacked"

printf '%s\n' \
  '<?xml version="1.0" encoding="UTF-8" standalone="no"?>' \
  '<AutomatedInstallation langpack="eng">' \
  '  <com.izforge.izpack.panels.htmlhello.HTMLHelloPanel id="welcome"/>' \
  "  <com.izforge.izpack.panels.target.TargetPanel id=\"install_dir\"><installpath>${install_dir}</installpath></com.izforge.izpack.panels.target.TargetPanel>" \
  '  <com.izforge.izpack.panels.packs.PacksPanel id="sdk_pack_select">' \
  '    <pack index="0" name="veraPDF GUI" selected="false"/>' \
  '    <pack index="1" name="veraPDF CLI" selected="true"/>' \
  '    <pack index="2" name="veraPDF Documentation" selected="false"/>' \
  '    <pack index="3" name="veraPDF Sample Plugins" selected="false"/>' \
  '  </com.izforge.izpack.panels.packs.PacksPanel>' \
  '  <com.izforge.izpack.panels.install.InstallPanel id="install"/>' \
  '  <com.izforge.izpack.panels.finish.FinishPanel id="finish"/>' \
  '</AutomatedInstallation>' > "${tmp_dir}/auto-install.xml"

installer="$(find "${tmp_dir}/unpacked" -name 'verapdf-izpack-installer-*.jar' | head -n 1)"
if [[ -z "${installer}" ]]; then
  printf 'error: no IzPack installer jar in %s\n' "${zip_name}" >&2
  exit 1
fi
java -jar "${installer}" "${tmp_dir}/auto-install.xml" >&2

"${install_dir}/verapdf" --version >&2
printf '%s\n' "${install_dir}/verapdf"
