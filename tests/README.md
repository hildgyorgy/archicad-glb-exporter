# Portable writer fixtures

Configure with `cmake -S . -B build-tests -DDROPVIEW_TESTS_ONLY=ON`, build with
`cmake --build build-tests --config Release`, then run
`ctest --test-dir build-tests --build-config Release --output-on-failure`.

`DropViewGlbWriterFixture <path>/writer-test.glb` generates two useful policy
examples:

- `writer-test.glb`: `asset.extras.dropView.materialWorkbench.policy` is `"disabled"`; includes design credits, initial view, sun and window lights.
- `writer-test.glb.enabled.glb`: policy is `"enabled"`; has no design credits, initial view, sun or window-light metadata.

The validator also checks the default-disabled policy with no other Drop & View
metadata, as well as valid JSON and GLB chunk alignment. These files are test
fixtures with synthetic geometry and images, not Archicad exports.
