## What's changed

- Experimental Archicad Window Light metadata export in `scenes[0].extras.dropView.windowLights`.
- Preserves each matched lamp's element and library-part IDs, placement, on/off state, colour, size information, and scalar GDL parameters for viewer-side evaluation.
- Includes generated Archicad 3D `LIGHT` component directions and raw light settings when available, with an explicit query status for each lamp.
- Derives a glTF-world rectangular emitter's centre, inward normal, in-plane axes and metre dimensions for validated upright window lights with the observed numeric shape ID `iC4dDetAreaShape = 2`; leaves intensity uncalibrated.
- Does not create a glTF punctual light or change materials, glass, the global sun, or viewer brightness.
- Window Light recognition currently matches the Hungarian or English library-part name; validate other library variants and placements before general release.

## Requirements

- Archicad 28 or 29
- Apple Silicon Mac with macOS 26 or later, or Windows 11 PC (x64)

Install the add-on for your Archicad version from the included macOS DMG or Windows ZIP. See the included README for usage and limitations.
