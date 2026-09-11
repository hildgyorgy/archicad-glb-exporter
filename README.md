# Drop & View GLB Exporter for Archicad

An experimental native Archicad add-on that exports the complete current content of the active 3D window to a self-contained GLB file. It is designed for the [Drop & View](https://hildgyorgy.github.io/drop-3d-view/) client-viewing workflow, while producing standard GLB files that can also be opened in other compatible viewers.

## Current release

The latest published package is a beta build for:

- Archicad 28 and 29
- Apple Silicon Macs (`arm64`) with macOS 26 or later
- Windows 11 PCs (`x64`)

Intel Macs and earlier versions of macOS are not currently supported.

## What is exported

The add-on uses the active Archicad 3D window as the source of truth. It exports every visible 3D body with the geometry and effective surface appearance shown there, including:

- 3D cuts and other geometry visible in the active 3D window;
- surface colours, transparency and embedded image textures;
- texture size and rotation;
- active Graphical Overrides;
- surface-based GLB mesh groups for visibility controls in compatible viewers.

Supported element families currently include walls, slabs, columns, beams, roofs, shells, stairs, railings, objects, lamps, Morphs, meshes/terrain, curtain walls, windows, doors and skylights.

Lighting, shadows, the Archicad environment, cameras and 2D drawing information are not baked into the GLB.

## Installation

1. Download the latest package for your platform from [GitHub Releases](https://github.com/hildgyorgy/archicad-glb-exporter/releases).
2. Open the macOS DMG or extract the Windows ZIP.
3. Open the folder matching your Archicad major version.
4. In Archicad, open **Options > Add-On Manager**.
5. Choose **Add**, then select `DropViewGLBExporter.bundle` on macOS or `DropViewGLBExporter.apx` on Windows.
6. Confirm that **Drop & View GLB Exporter** appears as an available add-on.

## Exporting

1. Open and activate an Archicad 3D window.
2. Prepare the geometry and appearance you want to share, including 3D cuts and Graphical Overrides where required.
3. Choose **Drop & View GLB Exporter > Export active 3D window to GLB…**.
4. Choose the destination `.glb` file.
5. Open the result in [Drop & View](https://hildgyorgy.github.io/drop-3d-view/).

The export command is disabled outside the 3D window. If an element contains invalid polygons, the exporter attempts to export its valid geometry and reports the skipped parts after completion.

## Building for Archicad 28–29 on macOS

Use the API DevKit matching the target Archicad major version. Each major version requires its own bundle, but both builds use the same exporter source:

```sh
cmake -S . -B build-ac28 -G Xcode \
  -DAC_API_DEVKIT_DIR="/path/to/Archicad-28-API-DevKit" \
  -DAC_ADDON_NAME=DropViewGLBExporter \
  -DAC_ADDON_LANGUAGE=INT
cmake --build build-ac28 --config Release
```

The CMake configuration detects the DevKit version and accepts Archicad 28 and 29. macOS builds target Apple Silicon and macOS 26. The release packaging script also detects the DevKit version and includes it in the DMG filename.

## Beta feedback

This release is intended for testing on real Archicad projects. Before relying on an exported model, compare it with the active Archicad 3D view. Please report reproducible problems through [GitHub Issues](https://github.com/hildgyorgy/archicad-glb-exporter/issues) and include:

- the Archicad build number and operating-system version;
- the affected element type;
- the export report and screenshots of the Archicad and GLB views;
- a minimal example file where it can be shared legally.

Do not publish confidential project files in a public issue.

## Privacy

The add-on reads the active 3D window geometry locally and writes the chosen GLB file locally. It does not upload the Archicad model or require an online account.

## Licence

The compiled add-on is free to use for personal, educational and commercial projects, including client work. The source code remains proprietary and is not offered under an open-source licence. See [LICENSE](LICENSE) for the full terms.

The project includes Mapbox Earcut under the ISC License. See [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

## Contact

Questions, feedback and permission requests: **[hild.gyorgy@freemail.hu](mailto:hild.gyorgy@freemail.hu)**

Copyright © 2026 György Hild.
