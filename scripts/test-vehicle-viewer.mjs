#!/usr/bin/env node
// Optional real-browser smoke test. See docs/vehicle-3d.md for the development-only dependency.
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
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
    await page.locator("#diagnostic").click();
    await page.locator('[data-view="perspective"]').click();
    await page.locator("#target-select").selectOption("ect");
    await page.locator("#focus").click();
    await page.locator("#reset").click();
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
