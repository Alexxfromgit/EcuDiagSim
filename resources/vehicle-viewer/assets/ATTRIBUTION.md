# Sedan exterior — CC BY 4.0

**Gourami - Luxury Saloon** by **Fishboe / ministephen**.

- Original: https://sketchfab.com/3d-models/gourami-luxury-saloon-bf29aca039c447ab913608e3f6818875
- Artist: https://fishboe.com/pages/3DModels.html
- License: Creative Commons Attribution 4.0 International,
  https://creativecommons.org/licenses/by/4.0/ (full text: `CC-BY-4.0.txt`).
- Original public model metadata confirms CC Attribution, commercial use allowed:
  https://api.sketchfab.com/v3/models/bf29aca039c447ab913608e3f6818875

The source included here is the modified version distributed by **sevnworks** in **Moonline**,
whose public credits identify the Gourami model and license the modified models under CC BY 4.0:
https://sevnworks.itch.io/moonline

Public distribution source (base64-wrapped GLB, decoded without changing its bytes):
https://html-classic.itch.zone/html/20750825/models/raijin_hi.glb.txt

`sedan-source.glb` SHA-256:
`643e658811c646144e2a7c69b18e7f36ff7888d3991639e9e361c7423fd2c905`

EcuDiagSim modifications: decode compressed geometry offline; reorient and normalize to a
4.96 m body length; smoothly lift the greenhouse to a 1.41 m roof while leaving the lower body
and wheel arches unchanged; separate lamp surfaces; split four doors along the modelled shut
lines; remove the hood ornament; cover fictional wheel badges; replace materials and add lamps,
a plate, interior, diagnostic assemblies and animations.
The prepared exterior in `sedan-mesh.js` remains **CC BY 4.0**, not the repository's MIT license.
No endorsement by the original artist or intermediate contributor is implied.

Rebuild with `node scripts/prepare-vehicle-asset.mjs` from the repository root. The GLB and
meshoptimizer decoder are development sources; the app only embeds the prepared geometry.
No Moonline game code, textures, sound, or other game assets are included.

The independently authored EcuDiagSim viewer code, additive diagnostic assemblies and generated
lighting remain under the repository's MIT license. Preserve this attribution and the CC BY
license when redistributing the exterior, including binary application packages.
