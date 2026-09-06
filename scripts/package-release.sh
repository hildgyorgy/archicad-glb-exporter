#!/bin/zsh
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
release_version="0.1.0-alpha"
archive_name="DropView-GLB-Exporter-AC29-macOS26-arm64-v${release_version}"
temporary_root="${TMPDIR%/}/dropview-glb-exporter-release"
build_dir="${temporary_root}/build"
stage_parent="${temporary_root}/stage"
stage_dir="${stage_parent}/${archive_name}"
output_zip="${project_root}/dist/${archive_name}.zip"
devkit_dir="${AC_API_DEVKIT_DIR:-${HOME}/Downloads/API}"

rm -rf "${build_dir}"
cmake -S "${project_root}" -B "${build_dir}" -G Xcode \
    -DAC_API_DEVKIT_DIR="${devkit_dir}" \
    -DAC_ADDON_NAME=DropViewGLBExporter \
    -DAC_ADDON_LANGUAGE=INT
cmake --build "${build_dir}" --config Release

bundle_path="${build_dir}/Release/DropViewGLBExporter.bundle"
if [[ ! -d "${bundle_path}" ]]; then
    print -u2 "Release bundle not found: ${bundle_path}"
    exit 1
fi

chmod -R u+w "${bundle_path}"
xattr -cr "${bundle_path}"

if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
    codesign --force --deep --strict --options runtime --timestamp \
        --sign "${CODESIGN_IDENTITY}" "${bundle_path}"
elif [[ "${ALLOW_ADHOC:-0}" == "1" ]]; then
    codesign --force --deep --strict --sign - "${bundle_path}"
else
    print -u2 "Set CODESIGN_IDENTITY to a Developer ID Application identity."
    print -u2 "For a local test package only, set ALLOW_ADHOC=1."
    exit 1
fi

codesign --verify --deep --strict --verbose=2 "${bundle_path}"

rm -rf "${stage_parent}"
mkdir -p "${stage_dir}" "${project_root}/dist"
ditto "${bundle_path}" "${stage_dir}/DropViewGLBExporter.bundle"
cp "${project_root}/README.md" "${stage_dir}/README.md"
cp "${project_root}/LICENSE" "${stage_dir}/LICENSE.txt"
cp "${project_root}/THIRD_PARTY_NOTICES.txt" "${stage_dir}/THIRD_PARTY_NOTICES.txt"

rm -f "${output_zip}"
(cd "${stage_parent}" && /usr/bin/zip -qry "${output_zip}" "${archive_name}")
shasum -a 256 "${output_zip}" > "${output_zip}.sha256"

print "Created ${output_zip}"
cat "${output_zip}.sha256"
