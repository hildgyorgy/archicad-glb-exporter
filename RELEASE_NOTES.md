# Drop & View GLB Exporter v0.1.0-alpha

This is the first experimental public build of the native GLB exporter for the Drop & View workflow.

## Requirements

- Archicad 29
- Apple Silicon Mac (`arm64`)
- macOS 26 or later

## Highlights

- Exports selected elements using the active Archicad 3D window geometry.
- Preserves visible 3D cuts, effective surface colours, transparency, embedded textures, texture size and rotation.
- Preserves the result of active Graphical Overrides.
- Supports major architectural, structural, library-object and site element families.
- Includes connected doors, windows and skylights with their selected hosts.
- Groups GLB meshes by effective surface.
- Continues past invalid polygons where possible and reports skipped geometry.

## Alpha notice

This build is intended for evaluation and feedback. Compare every exported model with the source Archicad 3D view before professional or client use. Please report reproducible problems through GitHub Issues without uploading confidential project material publicly.

## Installation

Open the DMG, then add `DropViewGLBExporter.bundle` from Archicad's **Options > Add-On Manager**.

See the included README for usage, limitations, privacy and licence information.
