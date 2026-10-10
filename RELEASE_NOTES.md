## What's changed

- Completed the v1.1.0 release with a validation fix: zero-length or cancelling smooth normals now fall back to the polygon normal, and glTF position accessor bounds retain exact float precision. This prevents invalid normals and accessor-bound errors without changing geometry detail, textures, or viewer brightness.
- Exports exact duplicate vertices only once within each glTF primitive, preserving positions, normals, texture coordinates, materials, and model detail.
- Uses 16-bit mesh indices where a primitive fits, retaining 32-bit indices for larger primitives. Together, these lossless changes substantially reduce exported GLB size; the reduction depends on the model.
- Aligns clear-glass selection rows to the left in the export settings dialog, removing the wide gaps around the check markers and giving surface descriptions more room.
- Includes Archicad 28, 29, and 30 add-ons for macOS and Windows.
- Retains the experimental Archicad Window Light metadata in `scenes[0].extras.dropView.windowLights` for viewer-side evaluation. It does not create glTF punctual lights or change viewer brightness.

## Requirements

- Archicad 28, 29, or 30
- Apple Silicon Mac with macOS 26 or later, or Windows 11 PC (x64)

Install the add-on for your Archicad version from the included macOS DMG or Windows ZIP. See the included README for usage and limitations.

**Updated v1.1.0 packages:** The macOS and Windows downloads were rebuilt after the validation fix. If you downloaded v1.1.0 before this update, download it again; the package checksums have changed. The version number remains 1.1.0.
