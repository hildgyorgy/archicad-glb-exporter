# Drop & View GLB Exporter

This maintenance release improves clear-glass selection and restores alpha-cutout textures such as chain-link fencing. The active Archicad 3D window remains the complete source of exported geometry and appearance.

## Requirements

- Archicad 28 or 29
- Apple Silicon Mac (`arm64`) with macOS 26 or later, or Windows 11 PC (`x64`)

## Highlights

- Restores coverage-alpha cutouts for transparent RGBA library surfaces while keeping optical transmission separate from texture coverage.
- Lists every visible, non-cutout surface with at least 50% Archicad transparency and lets the architect choose which ones should become optically clear glass.
- Exports selected clear glass with `transmissionFactor: 0.98`, `roughnessFactor: 0.03`, IOR 1.5, full coverage alpha and no guessed volume thickness.
- Keeps unselected transparent, tinted, emissive and textured surfaces faithful to their own Archicad settings.
- Shows the source material type, transparency, specular value, shininess, emission and alpha-cutout status beside every clear-glass candidate.
- Preserves all original Archicad material values and records whether the clear-glass override was applied in `extras.archicad`.
- Exports every visible body in the active Archicad 3D window without requiring an Archicad selection.
- Preserves visible 3D cuts, effective surface colours, transparency, embedded textures, texture size and rotation.
- Exports transparent and glass surfaces as thin physical glTF materials using `KHR_materials_transmission` and `KHR_materials_ior`, without confusing optical transmission with coverage alpha.
- Keeps alpha-cutout foliage as `MASK`, omits unsafe volume/thickness guesses, and records the original Archicad surface parameters in glTF material extras.
- Supports embedded base-colour, normal, packed metallic-roughness, occlusion and emissive texture slots in the GLB writer; Archicad 28–29 currently exposes only the base-colour surface image through this export path.
- Converts TIFF surface textures to embedded PNG data during export, alongside direct PNG and JPEG support.
- Smooths shading across edges marked as curved by Archicad while preserving hard edges and the original polygon count.
- Preserves the result of active Graphical Overrides.
- Supports major architectural, structural, library-object and site element families.
- Includes visible doors, windows and skylights directly from the active 3D model.
- Offers Surface / Texture, Layer and Element type grouping at export time.
- Writes each selected group as a clearly named glTF node and mesh, with material-specific primitives inside it.
- Preserves materials and embedded textures in every grouping mode.
- Continues past invalid polygons where possible and reports skipped geometry.
- Uses the embedded texture as the glTF base colour without applying Archicad's surface colour a second time.
- Builds Archicad 28 and 29 packages for macOS and Windows from one GitHub Actions release workflow.
- Separates GLB serialization from Archicad model collection and allocates glTF buffer indices automatically.
- Adds portable GLB and triangulation regression tests for Debug and Release builds on macOS, Windows and Linux.
- Adds portable regression tests for curved-surface normal generation and hard-edge preservation.
- Applies stricter compiler warnings while keeping required Archicad SDK exceptions narrowly scoped.

## Verification note

Compare exported models with the source Archicad 3D view as part of your normal delivery checks. Please report reproducible problems through GitHub Issues without uploading confidential project material publicly.

## Installation

Open the DMG or extract the Windows ZIP, enter the folder matching your Archicad version, then add `DropViewGLBExporter.bundle` or `DropViewGLBExporter.apx` from Archicad's **Options > Add-On Manager**.

See the included README for usage, limitations, privacy and licence information.
