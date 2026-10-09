## What's changed

- Adds Archicad 30 support while continuing to build separate add-ons for Archicad 28 and 29.
- Packages the Archicad 28, 29 and 30 add-ons together in the macOS DMG and Windows ZIP.
- Includes experimental Archicad Window Light metadata in `scenes[0].extras.dropView.windowLights` for viewer-side evaluation.
- Preserves matched lamp IDs, placement, on/off state, colour, size information, scalar GDL parameters, and generated Archicad 3D `LIGHT` data when available.
- This experimental metadata does not create glTF punctual lights or change materials, glass, the global sun, or viewer brightness.
- Window Light recognition currently matches Hungarian or English library-part names; other library variants and placements may need validation.

## Requirements

- Archicad 28, 29 or 30
- Apple Silicon Mac with macOS 26 or later, or Windows 11 PC (x64)

Install the add-on for your Archicad version from the included macOS DMG or Windows ZIP. See the included README for usage and limitations.
