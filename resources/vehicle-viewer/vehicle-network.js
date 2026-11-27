import * as THREE from "./vendor/three.module.min.js";
import { connectionsFromSnapshot, connectionStatus } from "./vehicle-state.js";

// Schematic harness routing, not an OEM loom or a CAN packet capture. The TPMS paths are radio.
export function createVehicleNetwork() {
    const root = new THREE.Group();
    root.name = "vehicle-signal-network";
    const records = new Map();
    const pulseGeometry = new THREE.SphereGeometry(.026, 8, 6);
    const pulseMaterial = new THREE.MeshBasicMaterial({ color: 0xffffff, depthTest: false, depthWrite: false });
    const pulses = new THREE.InstancedMesh(pulseGeometry, pulseMaterial, 256);
    pulses.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
    pulses.frustumCulled = false; pulses.renderOrder = 12; pulses.count = 0;
    root.add(pulses);
    const transform = new THREE.Matrix4();
    const position = new THREE.Vector3();
    const color = new THREE.Color();
    let topologyKey = "", snapshot = {}, phase = 0;
    let view = { diagnostic: true, wires: true, system: "", selectedId: "", visibleIds: new Set() };

    function curveFor(edge) {
        const [x, y, z] = edge.from;
        const [tx, ty, tz] = edge.to;
        let points;
        if (edge.kind === "radio") {
            points = [edge.from, [x * .8, Math.max(y, ty) + .35, z * .65], [tx, ty + .2, tz], edge.to];
        } else if (edge.kind === "can") {
            points = [edge.from, [x, .53, z + .10], [.10, .53, z + .16], [.10, .53, tz]];
        } else {
            const side = x < 0 ? -1 : 1;
            const lane = .72 + (edge.targetId.split("").reduce((sum, c) => sum + c.charCodeAt(0), 0) % 5) * .016;
            points = [edge.from, [x, Math.max(.53, y - .12), z + .10],
                [side * lane, .56, z + .17], [side * lane, .56, tz + .18], [tx, Math.max(.56, ty - .10), tz + .10], edge.to];
        }
        // Coincident consecutive points destabilize the curve's tangent frame.
        const vectors = points.map(p => new THREE.Vector3(...p)).filter((p, index, all) => index === 0 || p.distanceTo(all[index - 1]) > .015);
        return new THREE.CatmullRomCurve3(vectors, false, "centripetal");
    }

    function createRecord(edge) {
        const curve = curveFor(edge);
        const group = new THREE.Group(); group.name = edge.id;
        let line;
        if (edge.kind === "radio") {
            const material = new THREE.LineDashedMaterial({ color: 0xc393ff, dashSize: .085, gapSize: .055,
                transparent: true, opacity: .55, depthTest: false, depthWrite: false });
            line = new THREE.Line(new THREE.BufferGeometry().setFromPoints(curve.getPoints(60)), material);
            line.computeLineDistances();
        } else {
            const material = new THREE.MeshBasicMaterial({ color: 0x53ddd0, transparent: true, opacity: .52,
                depthTest: false, depthWrite: false });
            line = new THREE.Mesh(new THREE.TubeGeometry(curve, 48, edge.kind === "can" ? .012 : .0075, 5, false), material);
            if (edge.kind === "can") {
                const companion = new THREE.Mesh(line.geometry, material);
                companion.position.x = .026; group.add(companion);
            }
        }
        line.renderOrder = 8; group.add(line); root.add(group);
        return { edge, curve, group, line, length: curve.getLength(), visible: true, active: false, color: 0x53ddd0 };
    }

    function updateVisibility() {
        root.visible = view.diagnostic && view.wires;
        for (const record of records.values()) {
            const edge = record.edge;
            record.visible = root.visible && view.visibleIds.has(edge.targetId) && (!view.system || view.system === edge.system);
            record.group.visible = record.visible;
            const focused = !view.selectedId || edge.targetId === view.selectedId || edge.controllerId === view.selectedId ||
                (view.selectedId === "can_bus" && edge.kind === "can") || (view.selectedId === "abs_e2e" && edge.system === "ABS");
            record.line.material.opacity = focused ? .76 : .17;
        }
        updatePulses();
    }

    function updatePulses() {
        let index = 0;
        for (const record of records.values()) {
            if (!record.visible || !record.active) continue;
            const { edge, curve, length } = record;
            const count = edge.kind === "can" ? 3 : 2;
            const offset = edge.targetId.charCodeAt(0) / 255;
            for (let i = 0; i < count; i++) {
                const t = (phase * .9 / Math.max(.5, length) + i / count + offset) % 1;
                curve.getPointAt(edge.direction === "outbound" ? 1 - t : t, position);
                transform.makeTranslation(position.x, position.y, position.z);
                pulses.setMatrixAt(index, transform);
                pulses.setColorAt(index, color.set(record.color).lerp(new THREE.Color(0xffffff), .45));
                index++;
            }
        }
        pulses.count = index;
        pulses.instanceMatrix.needsUpdate = true;
        if (pulses.instanceColor) pulses.instanceColor.needsUpdate = true;
    }

    function applyState(next) {
        snapshot = next;
        const edges = connectionsFromSnapshot(snapshot);
        const key = edges.map(edge => edge.id).join("|");
        if (key !== topologyKey) {
            for (const record of records.values()) {
                root.remove(record.group);
                record.line.geometry.dispose(); record.line.material.dispose();
            }
            records.clear(); topologyKey = key;
            for (const edge of edges) records.set(edge.id, createRecord(edge));
        }
        for (const edge of edges) {
            const record = records.get(edge.id);
            record.edge = edge;
            const status = connectionStatus(edge, snapshot);
            record.active = status.active; record.color = status.color;
            record.line.material.color.set(status.color);
        }
        updateVisibility();
    }

    return {
        root, records, pulses, applyState,
        setView(next) { view = { ...view, ...next }; updateVisibility(); },
        hasActiveFlow() { return root.visible && [...records.values()].some(record => record.visible && record.active); },
        advance(seconds) { phase += seconds; updatePulses(); },
        summary() {
            const visible = [...records.values()].filter(record => record.visible);
            return { wired: visible.filter(record => record.edge.kind === "signal").length,
                radio: visible.filter(record => record.edge.kind === "radio").length,
                can: visible.filter(record => record.edge.kind === "can").length };
        }
    };
}
