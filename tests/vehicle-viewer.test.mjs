import test from "node:test";
import assert from "node:assert/strict";
import { targetsFromSnapshot, visibleTargets, connectionsFromSnapshot, connectionStatus,
    shouldAnimateFlow, locations, SelectionGesture } from "../resources/vehicle-viewer/vehicle-state.js";
import { createVehicleNetwork } from "../resources/vehicle-viewer/vehicle-network.js";
import { createVehicle } from "../resources/vehicle-viewer/vehicle-model.js";
import * as THREE from "../resources/vehicle-viewer/vendor/three.module.min.js";

const pointer = (x, y, id = 1) => ({ clientX: x, clientY: y, pointerId: id, button: 0, isPrimary: true });

test("rotation never selects a component, including a drag returning to its origin", () => {
    const gesture = new SelectionGesture();
    gesture.start(pointer(10, 10));
    gesture.move(pointer(80, 40));
    assert.equal(gesture.finish(pointer(10, 10)), false);
    gesture.start(pointer(10, 10));
    assert.equal(gesture.finish(pointer(12, 11)), true);
});

test("cancelled, right-button, and multi-pointer gestures never select", () => {
    const gesture = new SelectionGesture();
    gesture.start(pointer(10, 10));
    gesture.cancel();
    assert.equal(gesture.finish(pointer(10, 10)), false);
    gesture.start({ ...pointer(10, 10), button: 2 });
    assert.equal(gesture.finish(pointer(10, 10)), false);
    gesture.start(pointer(10, 10));
    gesture.start({ ...pointer(20, 20, 2), isPrimary: false });
    assert.equal(gesture.finish(pointer(10, 10)), false);
});

test("live state uses simulation IDs and never invents unfitted equipment", () => {
    const snapshot = {
        targets: [
            { id: "wss_fl", name: "Wheel FL", ecu: "ABS", kind: "Sensors" },
            { id: "ect", name: "Coolant", ecu: "ECM", kind: "Sensors" },
            { id: "abs", name: "ABS controller", ecu: "ABS", kind: "Control units" }
        ],
        sensors: [{ id: "wss_fl", trueValue: 50, value: 0, faulted: true }, { id: "ect", value: 85 }],
        ecus: [{ name: "ABS", diagIndex: 2, state: "fault" }],
        faults: [{ targetId: "wss_fl", type: "Open circuit" }]
    };
    const targets = targetsFromSnapshot(snapshot);
    assert.equal(targets.length, 3);
    assert.equal(targets.some(target => target.id === "srs" || target.id.startsWith("tpms")), false);
    assert.deepEqual(targets[0].position, [-1.12, .56, -1.48]);
    assert.equal(targets[0].sensor.trueValue, 50);
    assert.equal(targets[0].sensor.value, 0);
    assert.equal(targets[2].controller.diagIndex, 2);
    assert.deepEqual(visibleTargets(targets, "ABS", true).map(target => target.id), ["wss_fl", "abs"]);
    const repaired = targetsFromSnapshot({ ...snapshot, sensors: [{ id: "wss_fl", faulted: false }],
        faults: [], ecus: [{ name: "ABS", diagIndex: 2, state: "ok" }] });
    assert.equal(visibleTargets(repaired, "ABS", true).length, 0);
    assert.equal(targets[0].faulted, true, "a repair must not mutate an older snapshot");
});

test("new logical targets remain selectable without inventing a physical location", () => {
    const targets = targetsFromSnapshot({ targets: [{ id: "future_network_fault", kind: "Network" }] });
    assert.equal(targets.length, 1);
    assert.equal(targets[0].position, undefined);
    assert.equal(targets[0].label, "FUTURE_NETWORK_FAULT");
});

function equippedSnapshot() {
    const ecus = ["ECM", "TCM", "ABS", "BCM", "IPC", "SRS"].map(name => ({ name, state: "ok" }));
    const sensors = [{ id: "ect", ecu: "ECM", circuit: "OK" }, { id: "wss_fl", ecu: "ABS", circuit: "OK" },
        { id: "sas", ecu: "ABS", circuit: "OK" }, { id: "yaw", ecu: "ABS", circuit: "OK" },
        { id: "maf", ecu: "ECM", circuit: "OK" }, { id: "o2", ecu: "ECM", circuit: "OK" }];
    const targets = [...ecus.map(ecu => ({ id: ecu.name.toLowerCase(), ecu: ecu.name, kind: "Control units" })),
        ...sensors.map(sensor => ({ id: sensor.id, ecu: sensor.ecu, kind: "Sensors" })),
        ...["fl", "fr", "rl", "rr"].map(wheel => ({ id: "tpms_" + wheel, ecu: "BCM", kind: "Components" })),
        { id: "airbag_driver", ecu: "SRS", kind: "Components" }, { id: "airbag_passenger", ecu: "SRS", kind: "Components" },
        { id: "belt_switch", ecu: "SRS", kind: "Components" }, { id: "oil_pump", ecu: "ECM", kind: "Components" },
        { id: "bulb_low_left", ecu: "BCM", kind: "Components" }, { id: "throttle_body", ecu: "ECM", kind: "Components" }];
    return { ecus, sensors, targets, faults: [], running: true, reducedMotion: false };
}

test("signal topology connects sensors to their fitted controller and represents TPMS as radio", () => {
    const snapshot = equippedSnapshot();
    const links = connectionsFromSnapshot(snapshot);
    assert.equal(links.find(link => link.targetId === "wss_fl").controllerId, "abs");
    assert.equal(links.find(link => link.targetId === "ect").controllerId, "ecm");
    assert.equal(links.find(link => link.targetId === "ect").direction, "inbound");
    assert.equal(links.find(link => link.targetId === "bulb_low_left").direction, "outbound");
    assert.equal(links.find(link => link.targetId === "throttle_body").direction, "outbound");
    assert.equal(links.find(link => link.targetId === "airbag_driver").controllerId, "srs");
    assert.equal(links.filter(link => link.kind === "radio").length, 4);
    assert.equal(links.some(link => link.targetId === "tpms_fl" && link.kind === "signal"), false);
    assert.equal(links.some(link => link.targetId === "oil_pump"), false, "mechanical pump failure must not acquire an invented lead");
    for (const link of links) assert.ok(locations[link.targetId] && locations[link.controllerId]);
});

test("fault semantics stop broken circuits and CAN bus-off, but preserve a controller's local sampling", () => {
    const snapshot = equippedSnapshot();
    snapshot.sensors[0].faulted = true;
    snapshot.sensors[0].circuit = "Open circuit";
    snapshot.ecus.find(ecu => ecu.name === "ABS").state = "busoff";
    let links = connectionsFromSnapshot(snapshot);
    assert.equal(connectionStatus(links.find(link => link.id === "signal:ect"), snapshot).active, false);
    assert.equal(connectionStatus(links.find(link => link.id === "signal:wss_fl"), snapshot).active, true);
    assert.equal(connectionStatus(links.find(link => link.id === "can:abs"), snapshot).active, false);
    snapshot.sensors[0].circuit = "OK";
    links = connectionsFromSnapshot(snapshot);
    assert.equal(connectionStatus(links.find(link => link.id === "signal:ect"), snapshot).active, true, "an offset signal still reaches its ECU");
    snapshot.faults = [{ targetId: "can_bus" }];
    assert.ok(links.filter(link => link.kind === "can").every(link => !connectionStatus(link, snapshot).active));
    snapshot.ecus.find(ecu => ecu.name === "ECM").state = "sleep";
    links = connectionsFromSnapshot(snapshot);
    assert.equal(connectionStatus(links.find(link => link.id === "signal:ect"), snapshot).active, false);
});

test("removing equipment disposes its paths, hides physical parts and clears their data pulses", () => {
    const snapshot = equippedSnapshot();
    const network = createVehicleNetwork();
    const vehicle = createVehicle();
    network.setView({ visibleIds: new Set(snapshot.targets.map(target => target.id)) });
    network.applyState(snapshot); vehicle.applyState(snapshot);
    assert.equal(vehicle.parts.get("airbag_driver").visible, true);
    assert.equal(vehicle.parts.get("maf").visible, true);
    assert.equal(network.summary().radio, 4);
    const first = network.pulses.instanceMatrix.array.slice(0, 16);
    network.advance(.25);
    assert.notDeepEqual(network.pulses.instanceMatrix.array.slice(0, 16), first);
    const removed = new Set(["srs", "airbag_driver", "airbag_passenger", "belt_switch", "maf", "o2", "sas", "yaw",
        "tpms_fl", "tpms_fr", "tpms_rl", "tpms_rr"]);
    const base = { ...snapshot, ecus: snapshot.ecus.filter(ecu => ecu.name !== "SRS"),
        sensors: snapshot.sensors.filter(sensor => !removed.has(sensor.id)), targets: snapshot.targets.filter(target => !removed.has(target.id)) };
    network.applyState(base); vehicle.applyState(base);
    assert.equal(vehicle.parts.get("airbag_driver").visible, false);
    assert.equal(vehicle.parts.get("maf").visible, false);
    assert.equal(vehicle.parts.get("yaw").visible, false);
    assert.equal(network.summary().radio, 0);
    assert.ok([...network.records.values()].every(record => !removed.has(record.edge.targetId)));
    network.setView({ wires: false });
    assert.equal(network.pulses.count, 0);
    assert.equal(network.hasActiveFlow(), false);
    const bounds = new THREE.Box3().setFromObject(vehicle.shell).getSize(new THREE.Vector3());
    assert.ok(bounds.z > 4.8 && bounds.y < 1.3, "the body must retain low, full-size sedan proportions");
    vehicle.root.traverse(object => {
        if (object.isMesh) assert.ok(object.geometry.attributes.position.array.every(Number.isFinite), "geometry must remain finite");
    });
});

test("data flow respects simulation pause, reduced motion, visibility and the viewer switch", () => {
    assert.equal(shouldAnimateFlow({ running: true }), true);
    assert.equal(shouldAnimateFlow({ running: false }), false);
    assert.equal(shouldAnimateFlow({ running: true, reducedMotion: true }), false);
    assert.equal(shouldAnimateFlow({ running: true }, false), false);
    assert.equal(shouldAnimateFlow({ running: true }, true, false), false);
});
