# Documentation artwork

Real Windows captures of EcuDiagSim 0.4.0, refreshed on 2026-11-28. The app was built
from this repository with Qt 6.10.3, WebEngine and WebChannel. Screenshots retain the
native app content at 2800 × 1505 pixels (1600 × 860 logical pixels at 175% scaling).
The 3D explorer is the app's own offline renderer, connected to the simulation.

| Asset | Purpose |
|---|---|
| `hero.png` | 1920 × 880 README banner composed from the exterior screenshot |
| `social-preview.png` | 1280 × 640 artwork for a repository social preview or release announcement |
| `vehicle-exterior.png` | Expanded explorer, Exterior mode, Perspective camera, dipped headlamps |
| `vehicle-diagnostic.png` | Expanded explorer, Diagnostic mode, all equipment fitted, wiring and data flow enabled |
| `vehicle-fault.png` | Expanded explorer with the front-left wheel speed sensor's open circuit selected |
| `overview.png` | Main workspace with the compact 3D view, cluster, driver controls and fault editor |
| `faults.png` | Active wheel speed sensor fault and its effect on the cluster |
| `can-trace.png` | Live decoded CAN traffic during the same fault |
| `diagnostics.png` | ABS UDS scanner after reading DTCs, including C0035-13 |
| `lessons.png` | Guided lesson workspace and scenario log |

The banner and social artwork add typography and crop/resize real screenshot pixels.
The screenshots have no painted-in controls, readings or model details. The optional
equipment in the showroom scene is fitted only for that scenario; it does not change
the user's saved equipment preferences. Screenshot scenarios are presentation fixtures,
not additional bundled lessons.

## Refresh

Build with the README's normal Windows instructions, including Qt WebEngine,
WebChannel and Positioning. Keep the desktop unlocked and the app windows visible.
Qt WebEngine can produce an empty capture when the window is hidden. Use a desktop
large enough for a 1600 × 860 logical-pixel window plus its title bar.

```powershell
./scripts/capture-readme.ps1 -Preset debug
```

This refreshes the overview, faults, trace, lessons and diagnostic 3D captures. The
interactive captures require selecting the visible controls before the 45-second
timer expires:

```powershell
./scripts/capture-readme.ps1 -Preset debug -Interactive -Only diagnostics,vehicle-exterior,vehicle-fault
```

- `diagnostics`: wait for the ABS lamp, click **Read DTCs**, then click **C0035-13**
  for its snapshot. Keep **UDS** and **ABS** selected and move the pointer off the card.
- `vehicle-exterior`: click **Exterior** and keep the **Perspective** camera.
- `vehicle-fault`: keep **Diagnostic** and **Perspective**, then select **Wheel speed sensor FL**
  from the component selector. Confirm the inspector shows a fault and a measured speed of zero.

`-QtDir` selects the Qt installation. `-QtModuleDir` can supply WebEngine/WebChannel/
Positioning from a second prefix. `-Skip3D` supports diagram-only builds, but those
captures should not replace the 3D gallery. `-AfterMs` adjusts the automatic delay.
The helper preserves interactive images unless `-Interactive` is specified.

Rebuild the artwork after checking the exterior screenshot:

```powershell
python -m pip install Pillow
python scripts/make-readme-assets.py
```

The artwork script uses Segoe UI on Windows, DejaVu Sans on Linux or Arial on macOS.
Its stage crop assumes the expanded explorer and the capture size above. Inspect
both generated images after changing the window size, DPI, camera or viewer layout.
Review every screenshot for complete rendering, readable controls, valid scenario
state and absence of unrelated desktop content before committing it.

## Attribution

The car visible in these assets adapts **Gourami - Luxury Saloon** by **Fishboe /
ministephen**, via **sevnworks**, under **CC BY 4.0**. The exterior was modified by
sevnworks and EcuDiagSim. Preserve attribution when reusing artwork containing it.
See the [full model attribution](../../resources/vehicle-viewer/assets/ATTRIBUTION.md)
and [CC BY 4.0 license](../../resources/vehicle-viewer/assets/CC-BY-4.0.txt).
The project's original code and documentation remain under its [MIT license](../../LICENSE).
