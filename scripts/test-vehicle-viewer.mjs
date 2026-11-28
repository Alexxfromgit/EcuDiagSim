#!/usr/bin/env node
// Optional real-browser smoke test. See docs/vehicle-3d.md for the development-only dependency.
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFile, mkdir } from "node:fs/promises";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import path from "node:path";

const require = createRequire(import.meta.url);
const { chromium } = require(process.env.ECUSIM_PLAYWRIGHT_MODULE || "../build/viewer-tests/node_modules/playwright-core");
const root = fileURLToPath(new URL("../resources/vehicle-viewer/", import.meta.url));
const server = createServer(async (request, response) => {
    const pathname = new URL(request.url, "http://localhost").pathname;
    const filename = path.resolve(root, "." + (pathname === "/" ? "/index.html" : pathname));
    if (!filename.startsWith(root)) { response.writeHead(403).end(); return; }
    try {
        const body = await readFile(filename);
        response.setHeader("Content-Type", ({ ".html": "text/html", ".js": "text/javascript", ".css": "text/css" })[path.extname(filename)] || "application/octet-stream");
        response.end(body);
    } catch { response.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
let browser;
try {
    browser = await chromium.launch({ headless: true, executablePath: process.env.ECUSIM_CHROME_PATH ||
        (process.platform === "darwin" ? "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" : undefined) });
    const page = await browser.newPage({ viewport: { width: 1200, height: 760 } });
    const errors = [];
    const externalRequests = [];
    page.on("pageerror", error => errors.push(error.message));
    page.on("request", request => { if (!request.url().startsWith("http://127.0.0.1:")) externalRequests.push(request.url()); });
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.waitForSelector('body[data-viewer-ready="true"]');
    const snapshot = {
        running: true,
        targets: [
            { id: "wss_fl", name: "Wheel speed FL", ecu: "ABS", kind: "Sensors" },
            { id: "ect", name: "Coolant temperature", ecu: "ECM", kind: "Sensors" },
            { id: "ecm", name: "Engine control module", ecu: "ECM", kind: "Control units" },
            { id: "abs", name: "ABS controller", ecu: "ABS", kind: "Control units" },
            { id: "srs", name: "Airbag controller", ecu: "SRS", kind: "Control units" }
        ],
        sensors: [
            { id: "wss_fl", shortName: "Wheel FL", trueValue: 50, value: 0, unit: "km/h", digital: true, circuit: "Open circuit", faulted: true, fault: "Open circuit" },
            { id: "ect", shortName: "Coolant", trueValue: 89, value: 89, unit: "°C", voltage: 1.72, circuit: "OK", faulted: false }
        ],
        ecus: [{ name: "ECM", state: "ok", diagIndex: 0 }, { name: "ABS", state: "fault", diagIndex: 2 }, { name: "SRS", state: "sleep", diagIndex: 4 }],
        faults: [{ targetId: "wss_fl", type: "Open circuit" }], vehicle: { lowBeam: true }
    };
    await page.evaluate(state => window.vehicleViewer.setState(state), snapshot);
    await page.locator('[data-target="wss_fl"]').click();
    assert.equal(await page.locator("#selected-name").textContent(), "Wheel speed FL");
    assert.match(await page.locator("#selected-values").textContent(), /50.00 km\/h/);
    assert.match(await page.locator("#selected-values").textContent(), /0.00 km\/h/);
    assert.equal(await page.locator("#selected-status").textContent(), "Fault active");
    const before = await page.locator('[data-target="ect"]').boundingBox();
    const stage = await page.locator("#stage").boundingBox();
    await page.mouse.move(stage.x + stage.width * .5, stage.y + stage.height * .7);
    await page.mouse.down();
    await page.mouse.move(stage.x + stage.width * .7, stage.y + stage.height * .55, { steps: 12 });
    await page.mouse.up();
    await page.waitForTimeout(100);
    const after = await page.locator('[data-target="ect"]').boundingBox();
    assert.ok(Math.hypot(before.x - after.x, before.y - after.y) > 10, "drag must rotate the car");
    assert.equal(await page.locator("#target-select").inputValue(), "wss_fl", "rotation must not change selection");
    await page.locator("#system").selectOption("ABS");
    await page.locator("#faults-only").check();
    assert.equal(await page.locator("#target-count").textContent(), "2");
    assert.equal(await page.locator('[data-target="ect"]').isVisible(), false);
    const repaired = structuredClone(snapshot);
    repaired.sensors[0].faulted = false; repaired.faults = []; repaired.ecus[1].state = "ok";
    await page.evaluate(state => window.vehicleViewer.setState(state), repaired);
    assert.equal(await page.locator("#target-count").textContent(), "0");
    assert.equal(await page.locator("#target-select").inputValue(), "");
    await page.locator("#faults-only").uncheck();
    await page.locator("#system").selectOption("");
    repaired.ecus = repaired.ecus.filter(ecu => ecu.name !== "SRS");
    repaired.targets = repaired.targets.filter(target => target.id !== "srs");
    await page.evaluate(state => window.vehicleViewer.setState(state), repaired);
    assert.equal(await page.locator('[data-target="srs"]').count(), 0);
    await page.locator("#exterior").click();
    assert.equal(await page.locator("#exterior").getAttribute("aria-pressed"), "true");
    await page.locator("#reset").click();
    if (process.env.ECUSIM_VIEWER_SCREENSHOT_DIR) {
        await mkdir(process.env.ECUSIM_VIEWER_SCREENSHOT_DIR, { recursive: true });
        await page.locator("#target-select").selectOption("");
        await page.screenshot({ path: path.join(process.env.ECUSIM_VIEWER_SCREENSHOT_DIR, "vehicle-exterior.png") });
    }
    await page.locator("#diagnostic").click();
    if (process.env.ECUSIM_VIEWER_SCREENSHOT_DIR)
        await page.screenshot({ path: path.join(process.env.ECUSIM_VIEWER_SCREENSHOT_DIR, "vehicle-diagnostic.png") });
    await page.locator('[data-view="perspective"]').click();
    await page.locator("#target-select").selectOption("ect");
    await page.locator("#focus").click();
    await page.locator("#reset").click();
    if (process.env.ECUSIM_VIEWER_FIXTURES) {
        const cases = JSON.parse(await readFile(process.env.ECUSIM_VIEWER_FIXTURES, "utf8"));
        assert.equal(cases.length, 16);
        for (const state of cases) {
            await page.evaluate(snapshot => window.vehicleViewer.setState(snapshot), state);
            assert.equal(await page.locator("#target-count").textContent(), String(state.targets.length));
            assert.equal(await page.locator('[data-target^="tpms_"]').count(), state.fitted.includes("tpms") ? 4 : 0);
            for (const id of ["srs", "airbag_driver", "airbag_passenger", "belt_switch"])
                assert.equal(await page.locator(`[data-target="${id}"]`).count(), state.fitted.includes("srs") ? 1 : 0);
            for (const id of ["sas", "yaw"])
                assert.equal(await page.locator(`[data-target="${id}"]`).count(), state.fitted.includes("esc") ? 1 : 0);
            for (const id of ["maf", "o2", "ignition_coils", "intake_leak"])
                assert.equal(await page.locator(`[data-target="${id}"]`).count(), state.fitted.includes("engine_obd") ? 1 : 0);
            assert.match(await page.locator("#connection-summary").textContent(), new RegExp(`${state.ecus.length} CAN`));
            assert.match(await page.locator("#connection-summary").textContent(), new RegExp(`${state.fitted.includes("tpms") ? 4 : 0} radio`));
            for (const target of state.targets) assert.equal(await page.locator(`[data-target="${target.id}"]`).count(), 1, target.id);
        }
        const full = structuredClone(cases.at(-1));
        full.running = true; full.reducedMotion = false;
        await page.evaluate(state => window.vehicleViewer.setState(state), full);
        await page.waitForTimeout(100);
        const movingA = await page.evaluate(() => window.vehicleViewer.getRenderState());
        await page.waitForTimeout(220);
        const movingB = await page.evaluate(() => window.vehicleViewer.getRenderState());
        assert.ok(movingB.frames > movingA.frames, "the moving scene must actually be rendered");
        assert.notDeepEqual(movingA.firstPulse, movingB.firstPulse, "data pulses must move while the simulation runs");
        for (const state of [{ ...full, running: false }, { ...full, reducedMotion: true }]) {
            await page.evaluate(snapshot => window.vehicleViewer.setState(snapshot), state);
            await page.waitForTimeout(100);
            const still = await page.evaluate(() => window.vehicleViewer.getRenderState());
            await page.waitForTimeout(150);
            assert.deepEqual(await page.evaluate(() => window.vehicleViewer.getRenderState()), still,
                "pause/reduced motion must freeze the data flow and stop idle rendering");
        }
        await page.evaluate(state => window.vehicleViewer.setState(state), full);
        await page.waitForTimeout(100);
        await page.evaluate(() => window.vehicleViewer.setDisplayActive(false));
        const hidden = await page.evaluate(() => window.vehicleViewer.getRenderState());
        await page.waitForTimeout(150);
        assert.deepEqual(await page.evaluate(() => window.vehicleViewer.getRenderState()), hidden,
            "a hidden native panel must stop rendering and animating");
        await page.evaluate(() => window.vehicleViewer.setDisplayActive(true));
        await page.locator("#wiring").uncheck();
        assert.equal(await page.locator("#connection-summary").textContent(), "0 wired · 0 CAN · 0 radio");
        await page.locator("#wiring").check();
        await page.locator("#data-flow").uncheck();
        assert.equal(await page.locator("#flow-state").textContent(), "Flow off");
        await page.locator("#data-flow").check();
        full.running = false; full.reducedMotion = true;
        await page.evaluate(state => window.vehicleViewer.setState(state), full);
        if (process.env.ECUSIM_VIEWER_SCREENSHOT_DIR) {
            await page.locator("#target-select").selectOption("wss_fl");
            await page.screenshot({ path: path.join(process.env.ECUSIM_VIEWER_SCREENSHOT_DIR, "vehicle-all-equipment.png") });
            await page.locator("#target-select").selectOption("");
            await page.locator("#exterior").click();
            await page.screenshot({ path: path.join(process.env.ECUSIM_VIEWER_SCREENSHOT_DIR, "vehicle-exterior.png") });
            await page.locator("#diagnostic").click();
        }
        console.log("PASS: all 16 actual C++ equipment settings, fitted geometry/markers, CAN and radio paths, animated flow, pause, reduced motion, hidden panel and wiring/flow switches");
        await page.locator("#target-select").selectOption("ect");
    }
    if (process.env.ECUSIM_VIEWER_CONTROL_FIXTURES) {
        const cases = JSON.parse(await readFile(process.env.ECUSIM_VIEWER_CONTROL_FIXTURES, "utf8"));
        await page.locator("#target-select").selectOption("");
        await page.locator("#exterior").click();
        for (const state of cases) {
            await page.evaluate(snapshot => window.vehicleViewer.setState(snapshot), state);
            await page.waitForTimeout(50);
            const { controls, connections } = await page.evaluate(() => window.vehicleViewer.getRenderState());
            for (const [id, circuit] of Object.entries(state.vehicle.componentCircuits ?? {})) {
                const target = state.targets.find(target => target.id === id);
                if (!target) continue;
                const ecu = state.ecus.find(ecu => ecu.name === target.ecu);
                assert.equal(connections["signal:" + id].active, circuit === "ok" && !["sleep", "nopower"].includes(ecu.state), `${state.name}: ${id} circuit flow`);
            }
            assert.deepEqual(Object.keys(controls.lights).filter(key => controls.lights[key]).sort(), [...state.expectedLights].sort(), state.name);
            assert.deepEqual(controls.doors, state.vehicle.doors.map((open, i) => open ? (i % 2 === 0 ? -1 : 1) * 1.1 : 0), state.name);
            assert.ok(Math.abs(controls.steering[0] - state.vehicle.roadWheelAngle * Math.PI / 180) < 1e-8, state.name);
            assert.equal(controls.belt, state.vehicle.seatBelt);
            assert.deepEqual(controls.bags, state.vehicle.airbagFired);
            assert.ok(Math.abs(controls.fuel - Math.max(.02, state.vehicle.fuel / 55)) < 1e-8);
            assert.ok(Math.abs(controls.throttle - state.vehicle.throttle * .8 * Math.PI / 180) < 1e-8);
            assert.match(await page.locator("#vehicle-telemetry").textContent(), new RegExp(`${state.vehicle.fuel.toFixed(0)} L`));
            assert.match(await page.locator("#vehicle-telemetry").textContent(), new RegExp(`Parking brake ${state.vehicle.parkBrake ? "on" : "off"}`));
            assert.match(await page.locator("#vehicle-telemetry").textContent(), new RegExp(["Dry", "Wet", "Snow", "Ice"][state.vehicle.surface]));
            if (state.name === "component-airbag_driver-open") {
                await page.locator("#diagnostic").click();
                await page.locator("#target-select").selectOption("airbag_driver");
                assert.match(await page.locator("#selected-values").textContent(), /Open circuit/);
                assert.equal(await page.locator("#selected-status").textContent(), "Fault active");
                if (process.env.ECUSIM_VIEWER_SCREENSHOT_DIR)
                    await page.screenshot({ path: path.join(process.env.ECUSIM_VIEWER_SCREENSHOT_DIR, "control-airbag-open.png") });
                await page.locator("#target-select").selectOption("");
                await page.locator("#exterior").click();
            }
            if (process.env.ECUSIM_VIEWER_SCREENSHOT_DIR && ["low", "hazards-on", "brake-and-reverse", "door-0-open", "locked-belt-steering-fuel-snow"].includes(state.name)) {
                await page.locator(`[data-view="${["hazards-on", "brake-and-reverse"].includes(state.name) ? "rear" : "perspective"}"]`).click();
                await page.waitForTimeout(70);
                await page.screenshot({ path: path.join(process.env.ECUSIM_VIEWER_SCREENSHOT_DIR, `control-${state.name}.png`) });
            }
        }
        const driving = cases.find(state => state.name === "driving");
        await page.evaluate(state => window.vehicleViewer.setState({ ...state, running: true, reducedMotion: false }), driving);
        const before = await page.evaluate(() => window.vehicleViewer.getRenderState().controls.rolling);
        await page.waitForTimeout(180);
        assert.notDeepEqual(await page.evaluate(() => window.vehicleViewer.getRenderState().controls.rolling), before, "wheels must roll in exterior mode");
        // At speed the strobing spokes are covered by the rim blur; at walking pace they are not.
        assert.ok((await page.evaluate(() => window.vehicleViewer.getRenderState().controls.rimBlur)).every(value => value > .85), "rim blur at 100 km/h");
        // Direction and rate, measured in real time at 3 km/h (slow enough to never wrap or strobe).
        const slow = structuredClone(driving);
        Object.assign(slow, { running: true, reducedMotion: false, speedFactor: 1 });
        Object.assign(slow.vehicle, { wheels: [3, 3, 3, 3], velocity: 3 });
        await page.evaluate(state => window.vehicleViewer.setState(state), slow);
        await page.waitForTimeout(100);
        const start = await page.evaluate(() => ({ t: performance.now(), r: window.vehicleViewer.getRenderState().controls }));
        await page.waitForTimeout(500);
        const end = await page.evaluate(() => ({ t: performance.now(), r: window.vehicleViewer.getRenderState().controls }));
        assert.deepEqual(end.r.rimBlur, [0, 0, 0, 0], "no rim blur at 3 km/h");
        const expected = 3 / 3.6 / .3294 * (end.t - start.t) / 1000;
        for (let i = 0; i < 4; i++) {
            const delta = Math.atan2(Math.sin(end.r.rolling[i] - start.r.rolling[i]), Math.cos(end.r.rolling[i] - start.r.rolling[i]));
            assert.ok(delta < 0, `wheel ${i} must roll forwards (negative X rotation) when driving forwards`);
            assert.ok(-delta / expected > .6 && -delta / expected < 1.25, `wheel ${i} rate ${(-delta / expected).toFixed(2)}× the simulated speed`);
        }
        await page.evaluate(state => window.vehicleViewer.setState(state), cases[0]);
        for (const view of ["side", "rear", "front", "perspective"]) {
            await page.locator(`[data-view="${view}"]`).click();
            await page.waitForTimeout(80);
            if (process.env.ECUSIM_VIEWER_SCREENSHOT_DIR)
                await page.screenshot({ path: path.join(process.env.ECUSIM_VIEWER_SCREENSHOT_DIR, `vehicle-${view}.png`) });
        }
        await page.locator("#diagnostic").click();
        await page.locator("#target-select").selectOption("ect");
        console.log(`PASS: ${cases.length} real C++ driver-control snapshots, all lamp banks, blink phases, power loss, bulb fault, four doors, steering, restraints, fuel and exterior wheel motion`);
    }
    await page.setViewportSize({ width: 360, height: 740 });
    await page.waitForTimeout(100);
    assert.equal(await page.locator("#selected-name").isVisible(), true);
    assert.ok(await page.locator("canvas").isVisible());
    assert.deepEqual(errors, []);
    assert.deepEqual(externalRequests, [], "the viewer must make no external network requests");
    console.log("PASS: WebGL startup, marker selection, live values, rotation without selection, filters, repair, equipment removal, display modes, camera controls, compact layout, offline loading");
} finally {
    await browser?.close();
    await new Promise(resolve => server.close(resolve));
}
