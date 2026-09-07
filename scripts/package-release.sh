#!/bin/zsh
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
release_version="${RELEASE_VERSION:-0.2.0-alpha}"
temporary_root="${TMPDIR%/}/dropview-glb-exporter-release"
stage_dir="${temporary_root}/stage/DropView-GLB-Exporter-AC28-29-macOS26-arm64-v${release_version}"
output_dmg="${project_root}/dist/DropView-GLB-Exporter-AC28-29-macOS26-arm64-v${release_version}.dmg"
ac28_devkit="${AC28_API_DEVKIT_DIR:-/tmp/ac-devkits/28}"
ac29_devkit="${AC29_API_DEVKIT_DIR:-${HOME}/Downloads/API}"

detect_version () {
    local devkit_dir="$1"
    local acapinc_file="${devkit_dir}/Support/Inc/ACAPinc.h"
    if [[ ! -f "${acapinc_file}" ]]; then
        print -u2 "Archicad API header not found: ${acapinc_file}"
        return 1
    fi
    sed -nE 's/^[[:space:]]*#define[[:space:]]+ServerMainVers_([0-9][0-9])00.*/\1/p' "${acapinc_file}" | tail -n 1
}

build_and_stage () {
    local expected_version="$1"
    local devkit_dir="$2"
    local detected_version="$(detect_version "${devkit_dir}")"
    local build_dir="${temporary_root}/build-ac${expected_version}"
    local version_dir="${stage_dir}/Archicad ${expected_version}"

    if [[ "${detected_version}" != "${expected_version}" ]]; then
        print -u2 "Expected Archicad ${expected_version} DevKit at ${devkit_dir}, detected ${detected_version:-unknown}."
        return 1
    fi

    rm -rf "${build_dir}"
    cmake -S "${project_root}" -B "${build_dir}" -G Xcode \
        -DAC_API_DEVKIT_DIR="${devkit_dir}" \
        -DAC_ADDON_NAME=DropViewGLBExporter \
        -DAC_ADDON_LANGUAGE=INT
    cmake --build "${build_dir}" --config Release

    local bundle_path="${build_dir}/Release/DropViewGLBExporter.bundle"
    if [[ ! -d "${bundle_path}" ]]; then
        print -u2 "Release bundle not found: ${bundle_path}"
        return 1
    fi

    mkdir -p "${version_dir}"
    chmod -R u+w "${bundle_path}"
    xattr -cr "${bundle_path}"
    ditto --norsrc "${bundle_path}" "${version_dir}/DropViewGLBExporter.bundle"

    local staged_bundle="${version_dir}/DropViewGLBExporter.bundle"
    xattr -cr "${staged_bundle}"
    if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
        codesign --force --deep --strict --options runtime --timestamp \
            --sign "${CODESIGN_IDENTITY}" "${staged_bundle}"
    elif [[ "${ALLOW_ADHOC:-0}" == "1" ]]; then
        codesign --force --deep --strict --sign - "${staged_bundle}"
    else
        print -u2 "Set CODESIGN_IDENTITY to a Developer ID Application identity."
        print -u2 "For a local test package only, set ALLOW_ADHOC=1."
        return 1
    fi
    codesign --verify --deep --strict --verbose=2 "${staged_bundle}"
}

rm -rf "${temporary_root}/stage"
mkdir -p "${stage_dir}" "${project_root}/dist"

build_and_stage 28 "${ac28_devkit}"
build_and_stage 29 "${ac29_devkit}"

cp "${project_root}/README.md" "${stage_dir}/README.md"
cp "${project_root}/LICENSE" "${stage_dir}/LICENSE.txt"
cp "${project_root}/THIRD_PARTY_NOTICES.txt" "${stage_dir}/THIRD_PARTY_NOTICES.txt"

rm -f "${output_dmg}" "${output_dmg}.sha256"
COPYFILE_DISABLE=1 hdiutil create \
    -volname "Drop & View GLB Exporter" \
    -srcfolder "${stage_dir}" \
    -format UDZO -ov "${output_dmg}"

if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
    codesign --force --timestamp --sign "${CODESIGN_IDENTITY}" "${output_dmg}"
else
    codesign --force --sign - "${output_dmg}"
fi

codesign --verify --strict --verbose=2 "${output_dmg}"
hdiutil verify "${output_dmg}"

verify_dir="${temporary_root}/verify"
rm -rf "${verify_dir}"
mount_dir="${verify_dir}/mount"
mkdir -p "${mount_dir}"
hdiutil attach -readonly -nobrowse -mountpoint "${mount_dir}" "${output_dmg}"
for version in 28 29; do
    codesign --verify --deep --strict --verbose=2 \
        "${mount_dir}/Archicad ${version}/DropViewGLBExporter.bundle"
done
hdiutil detach "${mount_dir}"

if [[ -n "${NOTARY_PROFILE:-}" ]]; then
    if [[ -z "${CODESIGN_IDENTITY:-}" ]]; then
        print -u2 "NOTARY_PROFILE requires a Developer ID CODESIGN_IDENTITY."
        exit 1
    fi
    xcrun notarytool submit "${output_dmg}" \
        --keychain-profile "${NOTARY_PROFILE}" --wait
    xcrun stapler staple "${output_dmg}"
    xcrun stapler validate "${output_dmg}"
fi

cd "${project_root}/dist"
shasum -a 256 "${output_dmg:t}" > "${output_dmg:t}.sha256"

print "Created ${output_dmg}"
cat "${output_dmg}.sha256"
