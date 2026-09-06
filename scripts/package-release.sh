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

rm -rf "${stage_parent}"
mkdir -p "${stage_dir}" "${project_root}/dist"
ditto "${bundle_path}" "${stage_dir}/DropViewGLBExporter.bundle"
cp "${project_root}/README.md" "${stage_dir}/README.md"
cp "${project_root}/LICENSE" "${stage_dir}/LICENSE.txt"
cp "${project_root}/THIRD_PARTY_NOTICES.txt" "${stage_dir}/THIRD_PARTY_NOTICES.txt"

staged_bundle="${stage_dir}/DropViewGLBExporter.bundle"
xattr -cr "${staged_bundle}"

# Sign only after the bundle reaches its final staging location. Copying a signed
# bundle can invalidate its Mach-O signature on some macOS/File Provider setups.
if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
    codesign --force --deep --strict --options runtime --timestamp \
        --sign "${CODESIGN_IDENTITY}" "${staged_bundle}"
elif [[ "${ALLOW_ADHOC:-0}" == "1" ]]; then
    codesign --force --deep --strict --sign - "${staged_bundle}"
else
    print -u2 "Set CODESIGN_IDENTITY to a Developer ID Application identity."
    print -u2 "For a local test package only, set ALLOW_ADHOC=1."
    exit 1
fi

codesign --verify --deep --strict --verbose=2 "${staged_bundle}"

rm -f "${output_zip}"
ditto -c -k --keepParent "${stage_dir}" "${output_zip}"

# Verify the artifact users will actually download, rather than only the staging
# copy that preceded compression.
verify_dir="${temporary_root}/verify"
rm -rf "${verify_dir}"
mkdir -p "${verify_dir}"
ditto -x -k "${output_zip}" "${verify_dir}"
codesign --verify --deep --strict --verbose=2 \
    "${verify_dir}/${archive_name}/DropViewGLBExporter.bundle"

if [[ -n "${NOTARY_PROFILE:-}" ]]; then
    if [[ -z "${CODESIGN_IDENTITY:-}" ]]; then
        print -u2 "NOTARY_PROFILE requires a Developer ID CODESIGN_IDENTITY."
        exit 1
    fi
    xcrun notarytool submit "${output_zip}" \
        --keychain-profile "${NOTARY_PROFILE}" --wait
fi

shasum -a 256 "${output_zip}" > "${output_zip}.sha256"

print "Created ${output_zip}"
cat "${output_zip}.sha256"
