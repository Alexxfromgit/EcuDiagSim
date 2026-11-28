// Metres; +Y up, front at -Z, vehicle left (driver) at -X. Illustrative positions on the generic
// assemblies in vehicle-parts.js: sensors sit on the part they measure, controllers where such
// modules are commonly fitted. Not an OEM layout.
export const locations = Object.freeze({
    // Wheel speed sensors on the knuckles, inboard of the discs; TPMS valves at the rims.
    wss_fl: [-.58, .40, -1.50], wss_fr: [.58, .40, -1.50],
    wss_rl: [-.58, .40, 1.34], wss_rr: [.58, .40, 1.34],
    tpms_fl: [-.79, .13, -1.60], tpms_fr: [.79, .13, -1.60],
    tpms_rl: [-.79, .13, 1.24], tpms_rr: [.79, .13, 1.24],
    // Engine: crank sensor at the flywheel, coolant sensor in the thermostat housing, throttle
    // position on the throttle body, oil pressure switch on the block, MAF in the intake duct,
    // pre-catalyst oxygen sensor in the downpipe; gearbox fluid sensor in the gearbox sump.
    ckp: [-.18, .32, -1.27], ect: [.06, .72, -1.99], tps: [.29, .785, -1.42],
    tft: [.13, .27, -.76], eop: [-.19, .42, -1.55], fls: [.20, .37, .80],
    maf: [.39, .79, -1.62], o2: [-.27, .32, -1.12],
    sas: [-.40, .86, -.51], yaw: [0, .50, .08],
    alternator: [.24, .55, -1.88], cooling: [0, .58, -2.0],
    bulb_low_left: [-.43, .66, -2.30], oil_pump: [0, .29, -1.85],
    park_brake_switch: [.07, .64, -.30], intake_leak: [.36, .79, -1.50],
    ignition_coils: [0, .82, -1.62], throttle_body: [.26, .79, -1.42],
    airbag_driver: [-.40, .94, -.44], airbag_passenger: [.40, .97, -.70],
    belt_switch: [-.17, .64, .22], tyres: [.87, .50, -1.60],
    // ECM in the cowl e-box, ABS modulator by the brake servo, TCM and BCM behind the kick panels,
    // the cluster in its binnacle, the airbag module under the centre console.
    ecm: [.50, .79, -1.10], tcm: [.58, .50, -.86], abs: [-.46, .70, -1.46],
    bcm: [-.60, .52, -.84], ipc: [-.40, 1.00, -.66], srs: [0, .44, -.30],
    abs_e2e: [-.46, .80, -1.46], can_bus: [0, .55, -.55]
});

// Where every controller's CAN branch meets the backbone: behind the centre of the dashboard.
export const canHub = Object.freeze([0, .80, -.86]);

// Harness backbone (nodes and the looms between them). Every circuit is routed from its component
// to the nearest node, along the looms, and from the nearest node to its controller, so wires run
// in bundles along the engine bay rails, through the bulkhead, behind the dash, down the kick
// panels and along the sills and tunnel, as a real loom does.
export const harness = Object.freeze({
    nodes: {
        ef: [0, .80, -2.03], el1: [-.24, .78, -1.95], el2: [-.30, .78, -1.55], el3: [-.30, .80, -1.20],
        er1: [.30, .81, -1.95], er2: [.31, .82, -1.58], er3: [.38, .83, -1.20], eb: [0, .83, -1.20],
        lampL: [-.50, .74, -2.12], lampR: [.50, .74, -2.12], wfl: [-.48, .72, -1.76], wfr: [.48, .72, -1.76],
        eLow: [-.20, .40, -1.30], fwL: [-.32, .74, -1.04], fwR: [.36, .76, -1.04],
        dL: [-.36, .78, -.86], dC: [0, .80, -.86], dR: [.36, .78, -.86], col: [-.40, .84, -.72],
        kickL: [-.62, .50, -.86], kickR: [.62, .50, -.86],
        sL1: [-.70, .34, -.70], sL2: [-.70, .34, .45], sL3: [-.64, .38, 1.42],
        sR1: [.70, .34, -.70], sR2: [.70, .34, .45], sR3: [.64, .38, 1.42],
        t1: [0, .52, -.62], t2: [0, .50, .15], t3: [0, .45, .70], r1: [0, .40, 1.45]
    },
    links: [["ef", "el1"], ["ef", "er1"], ["el1", "el2"], ["el2", "el3"], ["er1", "er2"], ["er2", "er3"],
        ["el3", "eb"], ["er3", "eb"], ["el3", "fwL"], ["er3", "fwR"], ["el3", "eLow"], ["lampL", "el1"], ["lampR", "er1"],
        ["wfl", "el2"], ["wfr", "er2"], ["fwL", "dL"], ["fwR", "dR"], ["dL", "dC"], ["dC", "dR"], ["dL", "col"],
        ["dL", "kickL"], ["dR", "kickR"], ["kickL", "sL1"], ["kickR", "sR1"], ["sL1", "sL2"], ["sL2", "sL3"],
        ["sR1", "sR2"], ["sR2", "sR3"], ["dC", "t1"], ["t1", "t2"], ["t2", "t3"], ["t3", "r1"], ["r1", "sL3"], ["r1", "sR3"]]
});

// Shortest loom route between two points: [component, nodes..., controller].
export function routeHarness(from, to) {
    const { nodes, links } = harness;
    const ids = Object.keys(nodes);
    const distance = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
    const nearest = point => ids.reduce((best, id) => distance(nodes[id], point) < distance(nodes[best], point) ? id : best);
    const start = nearest(from), goal = nearest(to);
    const cost = new Map(ids.map(id => [id, Infinity])), previous = new Map(), open = new Set(ids);
    cost.set(start, 0);
    while (open.size) {
        const current = [...open].reduce((best, id) => cost.get(id) < cost.get(best) ? id : best);
        open.delete(current);
        if (current === goal || cost.get(current) === Infinity) break;
        for (const [a, b] of links) {
            const next = a === current ? b : b === current ? a : null;
            if (!next || !open.has(next)) continue;
            const candidate = cost.get(current) + distance(nodes[current], nodes[next]);
            if (candidate < cost.get(next)) { cost.set(next, candidate); previous.set(next, current); }
        }
    }
    const path = [goal];
    while (path[0] !== start && previous.has(path[0])) path.unshift(previous.get(path[0]));
    return path;
}

export const systemColors = Object.freeze({ ECM: 0x53ddd0, TCM: 0x71aaff, ABS: 0xf6c268,
    BCM: 0xb19aff, IPC: 0x83c7ea, SRS: 0xf191af });

// Physical signal/monitoring circuits only. Mechanical failures are selectable but do not
// acquire fictitious electrical leads. Direct TPMS communicates by radio, not wheel wiring.
const electricalComponents = new Set(["alternator", "bulb_low_left", "park_brake_switch",
    "ignition_coils", "throttle_body", "airbag_driver", "airbag_passenger", "belt_switch"]);
const commandedComponents = new Set(["bulb_low_left", "ignition_coils", "throttle_body"]);

export function connectionsFromSnapshot(snapshot) {
    const targets = targetsFromSnapshot(snapshot);
    const controllers = new Map(targets.filter(target => target.controller && target.position)
        .map(target => [target.controller.name, target]));
    const connections = [];
    for (const target of targets) {
        const controller = controllers.get(target.ecu);
        if (!controller || !target.position || target.controller) continue;
        const radio = target.id.startsWith("tpms_");
        if (!target.sensor && !radio && !electricalComponents.has(target.id)) continue;
        connections.push({ id: `signal:${target.id}`, targetId: target.id, controllerId: controller.id,
            system: target.ecu, kind: radio ? "radio" : "signal", from: target.position,
            direction: commandedComponents.has(target.id) ? "outbound" : "inbound",
            to: controller.position, target, controller: controller.controller });
    }
    for (const controller of controllers.values()) {
        connections.push({ id: `can:${controller.id}`, targetId: controller.id,
            controllerId: controller.id, system: controller.ecu, kind: "can",
            from: controller.position, to: canHub, controller: controller.controller });
    }
    return connections;
}

export function connectionStatus(connection, snapshot) {
    const ecu = connection.controller;
    const faults = snapshot.faults ?? [];
    const busShort = faults.some(fault => fault.targetId === "can_bus");
    const ecuPowered = ecu && !["sleep", "nopower"].includes(ecu.state);
    const circuit = snapshot.vehicle?.componentCircuits?.[connection.targetId] ?? connection.target?.sensor?.circuit ?? "";
    const missing = /open|short|missing/i.test(circuit) || (connection.kind === "radio" && connection.target?.faulted);
    const blocked = !ecuPowered || (connection.kind === "can" && (busShort || ecu.state === "busoff")) || missing;
    return { active: Boolean(ecuPowered && !blocked),
        faulted: Boolean(missing || connection.target?.faulted || ecu?.state === "nopower" ||
            (connection.kind === "can" && (busShort || ecu?.state === "busoff"))),
        color: !ecuPowered ? 0x536475 : blocked || connection.target?.faulted ? 0xffae49 :
            connection.kind === "can" ? 0x46d4ec : connection.kind === "radio" ? 0xc393ff : systemColors[connection.system] ?? 0x53ddd0 };
}

export function shouldAnimateFlow(snapshot, visible = true, enabled = true) {
    return visible && enabled && snapshot.running === true && snapshot.reducedMotion !== true;
}

export function targetsFromSnapshot(snapshot) {
    const sensors = new Map((snapshot.sensors ?? []).map(sensor => [sensor.id, sensor]));
    const controllers = new Map((snapshot.ecus ?? []).map(ecu => [ecu.name.toLowerCase(), ecu]));
    const faults = new Set((snapshot.faults ?? []).map(fault => fault.targetId));
    // Short diagram labels can repeat (both airbag squibs are "AB"); a shared one uses the full name.
    const labelCount = new Map();
    for (const target of snapshot.targets ?? []) if (target.label) labelCount.set(target.label, (labelCount.get(target.label) ?? 0) + 1);
    return (snapshot.targets ?? []).map(target => {
        const controller = controllers.get(target.id);
        const sensor = sensors.get(target.id);
        const componentCircuit = snapshot.vehicle?.componentCircuits?.[target.id];
        const circuitFault = componentCircuit && componentCircuit !== "ok";
        return {
            ...target, controller, sensor, componentCircuit, position: locations[target.id],
            label: controller?.name ?? sensor?.shortName ??
                (labelCount.get(target.label) > 1 ? target.name : target.label) ?? target.id.toUpperCase(),
            faulted: faults.has(target.id) || sensor?.faulted === true || Boolean(circuitFault) ||
                ["fault", "busoff", "nopower"].includes(controller?.state),
            state: controller?.state ?? (faults.has(target.id) || sensor?.faulted || circuitFault ? "fault" : "ok")
        };
    });
}

export function visibleTargets(targets, system, faultsOnly) {
    return targets.filter(target => (!system || target.ecu === system) && (!faultsOnly || target.faulted));
}

// A drag that returns to its starting point is still a drag. A second pointer cancels picking.
export class SelectionGesture {
    start(event) {
        if (this.pointer || event.button !== 0 || event.isPrimary === false) {
            this.cancel();
            return;
        }
        this.pointer = { id: event.pointerId, x: event.clientX, y: event.clientY, dragged: false };
    }
    move(event) {
        const p = this.pointer;
        if (p && p.id === event.pointerId && Math.hypot(event.clientX - p.x, event.clientY - p.y) > 6)
            p.dragged = true;
    }
    finish(event) {
        this.move(event);
        const selected = this.pointer?.id === event.pointerId && !this.pointer.dragged;
        this.cancel();
        return selected;
    }
    cancel() { this.pointer = null; }
}
