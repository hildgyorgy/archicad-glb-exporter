# Drop & View GLB Exporter for Archicad

An experimental native Archicad add-on that exports selected elements from the active 3D window to a self-contained GLB file. It is designed for the [Drop & View](https://hildgyorgy.github.io/drop-3d-view/) client-viewing workflow, while producing standard GLB files that can also be opened in other compatible viewers.

## Current release

**v0.1.0-alpha** is an experimental build for:

- Archicad 29
- Apple Silicon Macs (`arm64`)
- macOS 26 or later

Intel Macs, Windows, earlier versions of macOS and other Archicad versions are not currently supported.

## What is exported

The add-on uses the active Archicad 3D window as the source of truth. It exports the selected supported elements with the geometry and effective surface appearance shown there, including:

- 3D cuts and other geometry visible in the active 3D window;
- surface colours, transparency and embedded image textures;
- texture size and rotation;
- active Graphical Overrides;
- surface-based GLB mesh groups for visibility controls in compatible viewers.

Supported element families currently include walls, slabs, columns, beams, roofs, shells, stairs, railings, objects, lamps, Morphs, meshes/terrain, curtain walls, windows, doors and skylights. Connected windows, doors and skylights are included when their host wall, roof or shell is selected.

Lighting, shadows, the Archicad environment, cameras and 2D drawing information are not baked into the GLB.

## Installation

1. Download the latest macOS DMG from [GitHub Releases](https://github.com/hildgyorgy/archicad-glb-exporter/releases).
2. Open the DMG.
3. In Archicad 29, open **Options > Add-On Manager**.
4. Choose **Add**, then select `DropViewGLBExporter.bundle`.
5. Confirm that **Drop & View GLB Exporter** appears as an available add-on.

## Exporting

1. Open and activate an Archicad 3D window.
2. Prepare the geometry and appearance you want to share, including 3D cuts and Graphical Overrides where required.
3. Select the elements to export.
4. Choose **Drop & View GLB Exporter > Export selected 3D elements to GLB…**.
5. Choose the destination `.glb` file.
6. Open the result in [Drop & View](https://hildgyorgy.github.io/drop-3d-view/).

The export command is disabled outside the 3D window. If an element contains invalid polygons, the exporter attempts to export its valid geometry and reports the skipped parts after completion.

## Alpha feedback

This release is intended for testing on real Archicad projects. Before relying on an exported model, compare it with the active Archicad 3D view. Please report reproducible problems through [GitHub Issues](https://github.com/hildgyorgy/archicad-glb-exporter/issues) and include:

- the Archicad 29 build number and macOS version;
- the affected element type;
- the export report and screenshots of the Archicad and GLB views;
- a minimal example file where it can be shared legally.

Do not publish confidential project files in a public issue.

## Privacy

The add-on reads the selected model geometry locally and writes the chosen GLB file locally. It does not upload the Archicad model or require an online account.

## Licence

The compiled add-on is free to use for personal, educational and commercial projects, including client work. The source code remains proprietary and is not offered under an open-source licence. See [LICENSE](LICENSE) for the full terms.

The project includes Mapbox Earcut under the ISC License. See [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).

## Contact

Questions, feedback and permission requests: **[hild.gyorgy@freemail.hu](mailto:hild.gyorgy@freemail.hu)**

Copyright © 2026 György Hild.
