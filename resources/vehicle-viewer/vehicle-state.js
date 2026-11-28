// Metres; +Y up, front at -Z, vehicle left at -X. These are illustrative locations.
export const locations = Object.freeze({
    wss_fl: [-.90, .36, -1.60], wss_fr: [.90, .36, -1.60],
    wss_rl: [-.90, .36, 1.24], wss_rr: [.90, .36, 1.24],
    tpms_fl: [-.88, .23, -1.46], tpms_fr: [.88, .23, -1.46],
    tpms_rl: [-.88, .23, 1.38], tpms_rr: [.88, .23, 1.38],
    ckp: [-0.30, 0.65, -1.50], ect: [0.22, 0.95, -1.55], tps: [0.45, 0.95, -1.12],
    tft: [0.32, 0.55, -0.75], eop: [-0.48, 0.67, -1.24], fls: [0.14, 0.62, 1.43],
    maf: [0.63, 0.85, -1.67], o2: [-0.22, 0.42, -0.43],
    sas: [-0.43, 0.97, -0.45], yaw: [0, 0.54, 0.35],
    alternator: [-0.55, 0.71, -1.80], cooling: [0.22, 0.70, -2.04],
    bulb_low_left: [-0.73, 0.77, -2.27], oil_pump: [-0.28, 0.43, -1.47],
    park_brake_switch: [0.18, 0.90, 0.35], intake_leak: [0.57, 0.86, -1.37],
    ignition_coils: [-0.18, 0.90, -1.35], throttle_body: [0.38, 0.81, -1.05],
    airbag_driver: [-0.43, 1.09, -0.45], airbag_passenger: [0.49, 1.07, -0.66],
    belt_switch: [-0.24, 0.65, 0.31], tyres: [-1.13, 0.50, 1.78],
    ecm: [-0.68, 0.84, -0.95], tcm: [0.56, 0.61, -0.85], abs: [0.70, 0.81, -1.85],
    bcm: [0.68, 0.83, -0.15], ipc: [-0.44, 1.04, -0.65], srs: [0, 0.58, 0.25],
    abs_e2e: [0.65, 0.58, -0.15], can_bus: [0.10, 0.61, 0.78]
});

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
            from: controller.position, to: [.10, .51, 1.20], controller: controller.controller });
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
    return (snapshot.targets ?? []).map(target => {
        const controller = controllers.get(target.id);
        const sensor = sensors.get(target.id);
        const componentCircuit = snapshot.vehicle?.componentCircuits?.[target.id];
        const circuitFault = componentCircuit && componentCircuit !== "ok";
        return {
            ...target, controller, sensor, componentCircuit, position: locations[target.id],
            label: controller?.name ?? sensor?.shortName ?? target.label ?? target.id.toUpperCase(),
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
