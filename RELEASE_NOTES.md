# Drop & View GLB Exporter v0.2.0-alpha

This experimental release adds Archicad 28 and Windows support and improves the visual match of textured surfaces.

## Requirements

- Archicad 28 or 29
- Apple Silicon Mac (`arm64`) with macOS 26 or later, or Windows 11 PC (`x64`)

## Highlights

- Exports selected elements using the active Archicad 3D window geometry.
- Preserves visible 3D cuts, effective surface colours, transparency, embedded textures, texture size and rotation.
- Preserves the result of active Graphical Overrides.
- Supports major architectural, structural, library-object and site element families.
- Includes connected doors, windows and skylights with their selected hosts.
- Groups GLB meshes by effective surface.
- Continues past invalid polygons where possible and reports skipped geometry.
- Uses the embedded texture as the glTF base colour without applying Archicad's surface colour a second time.

## Alpha notice

This build is intended for evaluation and feedback. Compare every exported model with the source Archicad 3D view before professional or client use. Please report reproducible problems through GitHub Issues without uploading confidential project material publicly.

## Installation

Open the DMG or extract the Windows ZIP, enter the folder matching your Archicad version, then add `DropViewGLBExporter.bundle` or `DropViewGLBExporter.apx` from Archicad's **Options > Add-On Manager**.

See the included README for usage, limitations, privacy and licence information.
