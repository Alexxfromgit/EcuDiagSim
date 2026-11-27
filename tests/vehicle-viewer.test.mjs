import test from "node:test";
import assert from "node:assert/strict";
import { targetsFromSnapshot, visibleTargets, SelectionGesture } from "../resources/vehicle-viewer/vehicle-state.js";

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
