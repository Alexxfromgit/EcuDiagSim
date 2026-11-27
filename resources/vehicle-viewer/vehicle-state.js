// Metres; +Y up, front at -Z, vehicle left at -X. These are illustrative locations.
export const locations = Object.freeze({
    wss_fl: [-1.12, 0.56, -1.48], wss_fr: [1.12, 0.56, -1.48],
    wss_rl: [-1.12, 0.56, 1.50], wss_rr: [1.12, 0.56, 1.50],
    tpms_fl: [-1.15, 0.28, -1.32], tpms_fr: [1.15, 0.28, -1.32],
    tpms_rl: [-1.15, 0.28, 1.66], tpms_rr: [1.15, 0.28, 1.66],
    ckp: [-0.30, 0.69, -1.50], ect: [0.22, 1.02, -1.55], tps: [0.45, 1.02, -1.12],
    tft: [0.32, 0.58, -0.75], eop: [-0.48, 0.72, -1.24], fls: [0.14, 0.66, 1.43],
    maf: [0.63, 0.92, -1.67], o2: [-0.22, 0.43, -0.43],
    sas: [-0.46, 1.15, -0.32], yaw: [0, 0.57, 0.35],
    alternator: [-0.55, 0.76, -1.80], cooling: [0.22, 0.75, -2.04],
    bulb_low_left: [-0.73, 0.83, -2.27], oil_pump: [-0.28, 0.44, -1.47],
    park_brake_switch: [0.18, 0.97, 0.35], intake_leak: [0.57, 0.93, -1.37],
    ignition_coils: [-0.18, 1.08, -1.35], throttle_body: [0.38, 0.87, -1.05],
    airbag_driver: [-0.46, 1.29, -0.27], airbag_passenger: [0.49, 1.18, -0.43],
    belt_switch: [-0.24, 0.82, 0.31], tyres: [-1.13, 0.52, 1.78],
    ecm: [-0.74, 1.12, -0.93], tcm: [0.69, 0.75, -0.67], abs: [0.73, 1.10, -1.92],
    bcm: [0.71, 0.99, 0.08], ipc: [-0.42, 1.30, -0.57], srs: [0, 0.67, 0.16],
    abs_e2e: [0.65, 0.61, -0.15], can_bus: [0.10, 0.65, 0.78]
});

export function targetsFromSnapshot(snapshot) {
    const sensors = new Map((snapshot.sensors ?? []).map(sensor => [sensor.id, sensor]));
    const controllers = new Map((snapshot.ecus ?? []).map(ecu => [ecu.name.toLowerCase(), ecu]));
    const faults = new Set((snapshot.faults ?? []).map(fault => fault.targetId));
    return (snapshot.targets ?? []).map(target => {
        const controller = controllers.get(target.id);
        const sensor = sensors.get(target.id);
        return {
            ...target, controller, sensor, position: locations[target.id],
            label: controller?.name ?? sensor?.shortName ?? target.label ?? target.id.toUpperCase(),
            faulted: faults.has(target.id) || sensor?.faulted === true ||
                ["fault", "busoff", "nopower"].includes(controller?.state),
            state: controller?.state ?? (faults.has(target.id) || sensor?.faulted ? "fault" : "ok")
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
