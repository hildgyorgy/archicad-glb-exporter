# Drop & View GLB Exporter for Archicad

A native Archicad add-on that exports the complete current content of the active 3D window to a self-contained GLB file. It is designed for the [Drop & View](https://hildgyorgy.github.io/drop-3d-view/) client-viewing workflow, while producing standard GLB files that can also be opened in other compatible viewers.

## Current release

The latest production release supports:

- Archicad 28 and 29
- Apple Silicon Macs (`arm64`) with macOS 26 or later
- Windows 11 PCs (`x64`)

Intel Macs and earlier versions of macOS are not currently supported.

## What is exported

The add-on uses the active Archicad 3D window as the source of truth. It exports every visible 3D body with the geometry and effective surface appearance shown there, including:

- 3D cuts and other geometry visible in the active 3D window;
- surface colours and embedded PNG, JPEG and TIFF base-colour textures;
- Archicad transparent and glass surfaces as thin glTF transmission materials with physical reflections/refraction;
- texture size and rotation;
- smooth shading for Archicad surfaces marked as curved, while preserving hard edges;
- active Graphical Overrides;
- selectable surface, layer or element-type GLB mesh groups for visibility controls in compatible viewers.

Supported element families currently include walls, slabs, columns, beams, roofs, shells, stairs, railings, objects, lamps, Morphs, meshes/terrain, curtain walls, windows, doors and skylights.

Lighting, shadows, the Archicad environment, cameras and 2D drawing information are not baked into the GLB.

### Materials and textures

Archicad surface transparency is exported as `KHR_materials_transmission`, with
`KHR_materials_ior` (1.5) and a roughness estimate derived from Archicad's
shininess. It is deliberately not exported as `alphaMode: BLEND`: glTF alpha
describes coverage, while transmission describes looking through glass. The
exporter treats panes as thin surfaces and does not emit `KHR_materials_volume`,
because the 3D model API does not provide reliable closed-volume and pane-
thickness information. Alpha cutout textures, such as leaves, remain
`alphaMode: MASK`.

The GLB writer supports embedded base-colour (sRGB), normal (linear), packed
metallic-roughness (roughness in G, metallic in B), occlusion (R) and emissive
(sRGB) texture channels, with image deduplication and the exported UV/sampler
settings. Archicad 28–29's `API_MaterialType` currently exposes only one surface
image through the 3D model API, so the add-on can populate the base-colour
channel but cannot retrieve separate normal, metallic-roughness, occlusion or
emissive maps from an Archicad surface. The serializer is ready to retain those
channels if a later API exposes them. Original surface name, index, material
type, transparency, specular percentage and shininess are preserved in each
material's `extras.archicad` object for compatibility and diagnostics. Emission
colour and attenuation are mapped to glTF's emissive factor; the original
attenuation is also retained in the extras object.

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
4. Choose one grouping mode: **Surface / Texture**, **Layer** or **Element type**. The exported GLB contains one
   independently identifiable node and mesh for each group; materials and textures remain attached to their geometry.
5. Choose the destination `.glb` file.
6. Open the result in [Drop & View](https://hildgyorgy.github.io/drop-3d-view/).

Only the selected grouping hierarchy is written to a GLB. Export the same Archicad view again with another mode when a
different visibility list is required. **Surface / Texture** preserves the original exporter grouping behavior.

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

## Support and feedback

As part of your normal delivery checks, compare exported models with the active Archicad 3D view. Please report reproducible problems through [GitHub Issues](https://github.com/hildgyorgy/archicad-glb-exporter/issues) and include:

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
