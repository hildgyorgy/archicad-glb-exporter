# Drop & View GLB Exporter v1.2.0

## What's new

- Designers can choose **ALLOW MATERIAL EDITING IN DROP & VIEW** for each export. It starts unchecked every time, so a newly exported GLB is view-only in the planned public Material Workbench unless the designer explicitly opts in.
- Every GLB records the choice in `asset.extras.dropView.materialWorkbench.policy` as `"disabled"` or `"enabled"`, with `schemaVersion: 1`. The policy is present even when no design credits, initial view, sun or window-light metadata is available.
- The export settings dialog has more room for the new choice, its explanation and the clear-glass surface list. The existing design-credit preference and clear-glass behavior are unchanged.

This is an editing policy for Drop & View, not encryption or copy protection. The GLB remains a standard file that other compatible software can open and edit. Existing viewers may ignore the new optional metadata.

## Included platforms

- Archicad 28, 29 and 30 on Apple Silicon Macs with macOS 26 or later.
- Archicad 28, 29 and 30 on Windows 11 x64.

Install the add-on matching your Archicad version from the macOS DMG or Windows ZIP. The v1.1.0 lossless geometry optimizations, validation fixes, glass support and window-light metadata remain included.
