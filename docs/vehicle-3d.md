# Offline 3D vehicle explorer

The viewer is a native Qt Quick panel hosting a local Three.js page through Qt WebEngine.
It requires no service, account, purchased asset, CDN, Node runtime or commercial Qt module.
Application code, original car geometry and generated lighting remain MIT. Three.js is MIT;
Qt WebEngine/WebChannel and Chromium retain their respective licenses.

## Build and use

Install the same version of Qt WebEngine, WebChannel and Positioning as the rest of Qt.
For aqtinstall, add `qtwebengine qtwebchannel qtpositioning` to the existing module list.
Configure and build normally. `ECUSIM_VEHICLE_3D` defaults to `ON`; missing modules produce
a warning and a diagram-only app. Explicitly disable it with:

```sh
cmake --preset debug -DECUSIM_VEHICLE_3D=OFF
```

Choose **3D** / **Diagram** above the vehicle column. **Expand** moves the same viewer into
a larger dialog without resetting its camera or selection. Escape or Close returns it.
`EcuDiagSim --vehicle-3d` opens the expanded workspace directly. Qt software-rendering sessions
default to the diagram; WebGL2 is required for the 3D viewer. Startup and renderer errors offer
a reload or a return to the diagram.

- Left drag: orbit. Scroll: zoom. Click a marker: inspect it. Camera presets and reset are available.
- Diagnostic: transparent shell, schematic assemblies, harnesses and animated signal pulses.
  Exterior: a modern four-door sedan with a painted body, glazing, wheel openings, alloy wheels,
  door seams and slim lights. Exterior mode hides the internal wiring and unselected markers.
- System/fault filters apply to markers and the accessible component selector.
- The inspector shows true/measured sensor values and signal/circuit state. It navigates to
  the existing fault editor or a controller's UDS scanner; selecting alone never injects faults.
- Fitted equipment determines the targets. Logical targets have explicit schematic locations.
  Future unmapped targets remain in the selector without inventing a physical location.
- Engine OBD adds the airflow/oxygen sensors and ignition components. TPMS adds four valve-mounted
  radio sensors. SRS adds its controller, squib monitors and belt switch. ESC adds steering-angle
  and yaw sensors. Their physical bodies, markers and links are removed when the pack is unfitted.
  The equipment strip uses `App.fitted`, including temporary equipment required by a lesson.
- **Wiring** shows the schematic connections; **Data flow** controls moving pulses. Colours group
  circuits by their owning ECU. A selected component highlights its route. Dashed violet paths
  are TPMS radio links, not physical wheel wiring. Solid paths are circuits or controller CAN links.
- Sensor pulses run toward the ECU; lamp, throttle and ignition commands run toward the component.
  SRS paths indicate circuit monitoring, not airbag deployment. Mechanical faults such as an oil
  pump failure do not acquire a fictitious electrical lead. Broken circuits, sleeping/unpowered
  receivers, failed radio reception and CAN bus-off/shorted branches suppress their affected flow.
  A controller's local sensor sampling can continue while it is bus-off.

## Boundaries and files

- `src/app/qml/VehicleWorkspace.qml`: view switching and expansion; existing navigation signals.
- `src/app/qml/Vehicle3D.qml`: a dedicated WebChannel bridge. Only a display snapshot and validated
  target/ECU navigation methods are published; the App singleton is never exposed to JavaScript.
- `resources/vehicle-viewer/vehicle-model.js`: original procedural sedan and schematic internals.
- `vehicle-state.js`: stable simulation ID → illustrative 3D anchor mapping, filters, click/drag logic.
- `vehicle-network.js`: fitted circuit/CAN/radio topology, routed harness geometry and instanced pulses.
- `viewer.js`: rendering, camera, markers, live inspector and bridge connection.
- `vendor`: unmodified pinned Three.js 0.180.0 and its full MIT license.

Snapshots are refreshed at 10 Hz only while the panel is visible. Signal animation renders at up
to 30 fps while the simulation runs and a visible link is active. Pause, reduced motion, a hidden
document/native panel, exterior mode or disabled wiring/flow stop that loop. Otherwise rendering
is requested only when state, camera or layout changes. Simulation speed affects decorative flow
speed within a readable limit; these pulses are not time-correlated CAN frames or actual wire traffic.
Camera control never advances the simulation. The existing 2D view and UI-free core stay available.
All page resources are compiled into Qt resources. Remote access, local filesystem access,
storage, popup windows and navigation away from the bundled page are disabled. CSP blocks network
connections; no server or remote debugging port is started by the application.

## Verification

The ordinary C++ test suite still exercises the simulator. If Node is installed, CTest also runs
the dependency-free viewer state/gesture tests. They can be run separately with:

```sh
node --test tests/vehicle-viewer.test.mjs
```

An optional real-browser test serves the bundled assets on an ephemeral loopback port, launches
isolated headless Chrome and verifies WebGL startup, marker selection, live values, drag rotation,
filtering, repair updates, equipment removal, camera controls, compact layout and zero external
requests. Its default fixture is synthetic; native Qt smoke testing is still needed for the WebChannel bridge.
It installs development tooling only under the ignored build directory:

```sh
npm install --prefix build/viewer-tests --ignore-scripts --no-audit --no-fund playwright-core@1.56.1
node scripts/test-vehicle-viewer.mjs
```

On macOS it uses the installed Google Chrome. Elsewhere set `ECUSIM_CHROME_PATH` to a Chrome/Chromium
executable. `ECUSIM_PLAYWRIGHT_MODULE` can point to a separately installed `playwright-core` module.

To also test all 16 equipment combinations using actual C++ view-model snapshots (adjust the
build directory for your preset), export fixtures from the isolated test settings and pass them
to the browser test. This checks actual fitted target sets, CAN/radio counts, live pulse movement,
and frozen render/pulse state when paused, hidden or using reduced motion:

```sh
ECUSIM_VIEWER_FIXTURES="$PWD/build/debug/equipment-snapshots.json" \
    build/debug/tst_app vehicleViewerEquipmentSnapshots
ECUSIM_VIEWER_FIXTURES="$PWD/build/debug/equipment-snapshots.json" \
    node scripts/test-vehicle-viewer.mjs
```

`ECUSIM_VIEWER_SCREENSHOT_DIR` optionally captures exterior and diagnostic previews. The offline
browser development API exposes `setState`, `setDisplayActive` and read-only `getRenderState`
measurements; none of these exposes the native App singleton or injects simulation faults.

Native smoke test (adjust the executable path on Windows/macOS):

```sh
EcuDiagSim --vehicle-3d --scenario 1 --size 1440x900 --after 8000 --screenshot vehicle-3d.png
```

## MVP limitations and production follow-up

The sedan is an original contemporary interaction model, with approximate component locations.
It is not an OEM model or a finished photorealistic asset. A production model can replace the
geometry builder while retaining the simulation ID mapping and the viewer/bridge interface.
Wheel/door animations, OEM-accurate harness routing and automated performance budgets are not
implemented. Labels prioritize selected/faulted targets and suppress overlaps; all components remain
available through the selector. External GLB import is not yet implemented. The renderer must be
tested on representative Windows, Linux and macOS hardware; local macOS testing alone does not
establish cross-platform graphics or packaging compatibility.

Three.js's full license and the project's notices are installed in the app. Before public
distribution, inventory the actually bundled Qt/Chromium libraries, ship their complete license
texts/notices, and provide the corresponding library sources or an appropriate offer through
a publisher-controlled arrangement. Merely linking to Qt's source website is insufficient.
Dynamic linking and instructions must preserve the user's applicable replacement/relinking rights.
This MVP does not automate that source publication or constitute a complete dependency-license audit.
