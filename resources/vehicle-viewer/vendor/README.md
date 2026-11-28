# Three.js 0.180.0

Unmodified upstream files, bundled so the viewer needs no network connection or npm at runtime:

- `build/three.module.min.js`
- `build/three.core.min.js`
- `examples/jsm/controls/OrbitControls.js`
- `LICENSE`

Source: <https://registry.npmjs.org/three/-/three-0.180.0.tgz>

Archive SHA-256: `ad66d724565ee29a2467277fa84daa5ed0211d6b8d446e9ef29f6bae0cd14144`

Upstream repository: <https://github.com/mrdoob/three.js/tree/r180>

License: MIT. Preserve `LICENSE` when redistributing these files.

`meshopt_decoder.module.js` is the unmodified `examples/jsm/libs/meshopt_decoder.module.js`
from the same pinned archive. It embeds meshoptimizer 0.22's decoder, MIT, © 2016–2024
Arseny Kapoulkine; retain `meshoptimizer-LICENSE.md` when redistributing that tool.
Only `scripts/prepare-vehicle-asset.mjs` uses it. It is not embedded in the application.
The application resources list is explicit in `src/app/CMakeLists.txt`.
