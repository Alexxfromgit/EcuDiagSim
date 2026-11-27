import * as THREE from "./vendor/three.module.min.js";
import sedanAsset from "./assets/sedan-mesh.js";
import { locations, systemColors } from "./vehicle-state.js";

// Artist-authored sedan surfaces, prepared offline from the attributed CC-BY asset.
const decoded = Uint8Array.from(atob(sedanAsset.binary), c => c.charCodeAt(0)).buffer;
const wheelIds = ["fl", "fr", "rl", "rr"];
export function createVehicle() {
    const root = new THREE.Group(); root.name = "diagnostic-sedan";
    const shell = new THREE.Group(), components = new THREE.Group(); root.add(shell, components);
    const parts = new Map(), modules = new Map(), wheels = [], doorPivots = new Map();
    const shellMaterials = [], lights = new Map(), tyres = [], wheelSpinners = [];
    const dark = new THREE.MeshStandardMaterial({ color: 0x1c2630, roughness: .65 });
    const metal = new THREE.MeshStandardMaterial({ color: 0x7a8c99, metalness: .7, roughness: .35 });
    const black = new THREE.MeshStandardMaterial({ color: 0x101820, roughness: .7 });
    let snapshot = {}, diagnosticMode = true;

    const geometryFor = part => {
        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute("position", new THREE.BufferAttribute(new Float32Array(decoded, ...part.position).slice(), 3));
        geometry.setAttribute("normal", new THREE.BufferAttribute(new Float32Array(decoded, ...part.normal).slice(), 3));
        geometry.setIndex(new THREE.BufferAttribute(new Uint32Array(decoded, ...part.indices).slice(), 1));
        return geometry;
    };
    function item(geometry, material, position = [0, 0, 0], parent = root) {
        const mesh = new THREE.Mesh(geometry, material); mesh.position.set(...position);
        mesh.castShadow = true; mesh.receiveShadow = true; parent.add(mesh); return mesh;
    }
    const box = (size, position, material = dark, parent = root) => item(new THREE.BoxGeometry(...size), material, position, parent);
    function cushion(size, position, material = dark) {
        const [w, h, d] = size, r = Math.min(w, h, d) * .25;
        const shape = new THREE.Shape();
        shape.moveTo(-w / 2 + r, -h / 2);
        shape.lineTo(w / 2 - r, -h / 2); shape.quadraticCurveTo(w / 2, -h / 2, w / 2, -h / 2 + r);
        shape.lineTo(w / 2, h / 2 - r); shape.quadraticCurveTo(w / 2, h / 2, w / 2 - r, h / 2);
        shape.lineTo(-w / 2 + r, h / 2); shape.quadraticCurveTo(-w / 2, h / 2, -w / 2, h / 2 - r);
        shape.lineTo(-w / 2, -h / 2 + r); shape.quadraticCurveTo(-w / 2, -h / 2, -w / 2 + r, -h / 2);
        const geometry = new THREE.ExtrudeGeometry(shape, { depth: d - 2 * r, bevelEnabled: true, bevelSize: r, bevelThickness: r, bevelSegments: 3, steps: 1 });
        geometry.translate(0, 0, -d / 2 + r);
        return item(geometry, material, position);
    }
    const blob = (size, position, material, parent = root) => {
        const mesh = item(new THREE.SphereGeometry(1, 20, 12), material, position, parent); mesh.scale.set(...size); return mesh;
    };
    function line(points, radius, material, parent = root) {
        const curve = new THREE.CatmullRomCurve3(points.map(p => new THREE.Vector3(...p)));
        return item(new THREE.TubeGeometry(curve, Math.max(12, points.length * 6), radius, 6, false), material, [0, 0, 0], parent);
    }
    for (const [index, id] of wheelIds.entries()) {
        const pivot = new THREE.Group(); pivot.position.set(...sedanAsset.wheels[id]);
        pivot.name = "wheel:" + id; root.add(pivot); wheels[index] = pivot;
        const spinner = new THREE.Group(); pivot.add(spinner); wheelSpinners[index] = spinner;
    }
    for (const id of wheelIds) {
        const pivot = new THREE.Group(); pivot.name = "door:" + id;
        pivot.position.set(id.endsWith("l") ? -.85 : .85, .7, id.startsWith("f") ? -.98 : .20);
        shell.add(pivot); doorPivots.set(id, pivot);
    }
    for (const part of sedanAsset.parts) {
        const source = sedanAsset.materials[part.material];
        const pbr = source.pbrMetallicRoughness ?? {};
        const material = new THREE.MeshPhysicalMaterial({ color: new THREE.Color(...(pbr.baseColorFactor ?? [1, 1, 1]).slice(0, 3)),
            metalness: pbr.metallicFactor ?? 1, roughness: Math.max(.12, pbr.roughnessFactor ?? .5), side: THREE.DoubleSide });
        if (part.material === 0) { material.color.set(0x607f96); material.metalness = .62; material.clearcoat = 1; material.clearcoatRoughness = .18; }
        if ([4, 11, 12].includes(part.material)) { material.metalness = .85; material.roughness = .25; }
        if (part.material === 3) {
            material.color.set(part.lens ? 0xa6c0cb : 0x1f3642); material.transparent = true;
            material.opacity = part.lens ? .12 : .52; material.depthWrite = false; material.roughness = .1;
        }
        material.name = source.name;
        let parent = shell, position = [0, 0, 0];
        const geometry = geometryFor(part);
        if (part.wheel) {
            const index = wheelIds.indexOf(part.wheel);
            parent = part.node.includes("BrakePad") ? wheels[index] : wheelSpinners[index];
            const center = sedanAsset.wheels[part.wheel]; geometry.translate(-center[0], -center[1], -center[2]);
        } else if (part.door) {
            parent = doorPivots.get(part.door);
            const hinge = parent.position; geometry.translate(-hinge.x, -hinge.y, -hinge.z);
        }
        const mesh = item(geometry, material, position, parent);
        mesh.name = [source.name, part.wheel ?? part.door ?? "body", part.lampSide ?? "", part.lampRole ?? ""].join(":");
        if (part.material === 9) tyres.push({ mesh, index: wheelIds.indexOf(part.wheel) });
        if (!part.wheel && ![5, 6, 7].includes(part.material)) shellMaterials.push({ material, opacity: material.opacity, transparent: material.transparent, depthWrite: material.depthWrite, lens: part.lens });
        if ([5, 6, 7].includes(part.material)) {
            const role = part.material === 5 ? part.lampRole : part.material === 6 ? "tail" : "turn";
            const key = role + ":" + part.lampSide;
            if (!lights.has(key)) lights.set(key, []);
            lights.get(key).push(mesh);
            material.color.set(role === "tail" ? 0x501322 : role === "turn" ? 0x58351b : 0x33424c);
            material.emissive.set(0x000000);
        }
    }
    function extraLamp(key, size, position, color) {
        const material = new THREE.MeshStandardMaterial({ color: 0x27323a, emissive: 0x000000, roughness: .22 });
        const mesh = box(size, position, material, shell); mesh.userData.lampColor = color; lights.set(key, [mesh]); return mesh;
    }
    extraLamp("stop:center", [.30, .028, .015], [0, 1.18, 1.69], 0xff243d).rotation.x = -.7;
    for (const [side, x] of [["l", -.58], ["r", .58]]) {
        extraLamp("reverse:" + side, [.075, .045, .018], [x, .72, 2.385], 0xe7f4ff);
        const mirror = extraLamp("mirror:" + side, [.09, .012, .025], [x < 0 ? -.94 : .94, 1.04, -.74], 0xffa51f);
        const door = doorPivots.get("f" + side); door.attach(mirror);
    }
    const beams = [];
    for (const x of [-.43, .43]) {
        const beam = new THREE.SpotLight(0xdcefff, 0, 12, .26, .65, 1.4);
        beam.position.set(x, .7, -2.36); beam.target.position.set(x, .02, -8);
        root.add(beam, beam.target); beams.push(beam);
    }
    // Neutral cap covers keep the adapted exterior unbranded.
    for (let i = 0; i < 4; i++) {
        const cap = item(new THREE.CylinderGeometry(.037, .037, .012, 24), metal,
            [i % 2 === 0 ? -.115 : .115, 0, 0], wheelSpinners[i]); cap.rotation.z = Math.PI / 2;
    }

    // Engine bay and cabin are additive: the exterior uses the artist's continuous body surfaces.
    box([1.28, .08, 3.5], [0, .31, 0], dark);
    blob([.43, .22, .42], [0, .67, -1.50], metal);
    box([.77, .12, .65], [0, .84, -1.47], dark);
    for (let i = -2; i <= 2; i++) box([.025, .015, .49], [i * .13, .91, -1.46], metal);
    box([.40, .23, .55], [.13, .47, -.86], metal);
    box([1.18, .30, .07], [0, .64, -2.08], dark);
    box([.27, .17, .32], [.56, .73, -1.76], dark);
    line([[.56, .81, -1.76], [.61, .86, -1.5], [.38, .82, -1.09]], .045, black);
    box([.82, .19, .60], [0, .47, 1.34], dark);
    const fuel = box([.74, .12, .52], [0, .48, 1.34], new THREE.MeshStandardMaterial({ color: 0x4d9c9c, transparent: true, opacity: .6 }));
    line([[-.25, .38, -1.4], [-.23, .25, -.35], [.2, .25, 1.3], [.45, .30, 2.30]], .03, metal);
    for (const side of [-1, 1]) for (const z of [.02, .92]) {
        cushion([.44, .12, .45], [side * .40, .61, z]);
        const seat = cushion([.43, .49, .13], [side * .40, .91, z + .17]); seat.rotation.x = -.16;
        cushion([.24, .14, .10], [side * .40, 1.20, z + .22]);
    }
    box([1.30, .15, .24], [0, 1.00, -.70], dark);
    const steering = item(new THREE.TorusGeometry(.135, .015, 8, 32), black, [-.40, 1.08, -.44]); steering.rotation.x = -.4;
    const throttle = item(new THREE.CylinderGeometry(.06, .06, .018, 20), metal, [.38, .84, -1.08]);
    const belt = line([[-.60, 1.13, .17], [-.27, .72, -.02], [-.60, .65, .2]], .012,
        new THREE.MeshStandardMaterial({ color: 0x171d24 }));
    const bags = [blob([.24, .20, .15], [-.40, 1.11, -.36], new THREE.MeshStandardMaterial({ color: 0xe7e3d8, roughness: .95 })),
        blob([.28, .22, .19], [.40, 1.11, -.49], new THREE.MeshStandardMaterial({ color: 0xe7e3d8, roughness: .95 }))];
    const pins = wheelIds.map(id => {
        const door = doorPivots.get(id), hinge = door.position;
        const pin = box([.014, .025, .014], [hinge.x * .94, 1.02, hinge.z + .85], metal);
        door.attach(pin); return pin;
    });
    for (const [id, anchor] of Object.entries(locations)) {
        if (["can_bus", "abs_e2e", "tyres", "intake_leak", "cooling", "oil_pump"].includes(id)) continue;
        const group = new THREE.Group(); group.name = "component:" + id; group.position.set(...anchor); components.add(group); parts.set(id, group);
        const controller = ["ecm", "tcm", "abs", "bcm", "ipc", "srs"].includes(id);
        const material = new THREE.MeshStandardMaterial({ color: controller ? 0x387b87 : 0x8b9aab, metalness: .6, roughness: .35 });
        box(controller ? [.22, .10, .20] : [.06, .045, .06], [0, -.025, 0], material, group);
        box([.035, .025, .04], [controller ? .11 : .04, -.03, .02], black, group);
        if (controller) modules.set(id, { group, material });
    }
    function diagnostic(enabled) {
        diagnosticMode = enabled;
        for (const entry of shellMaterials) {
            entry.material.transparent = enabled || entry.transparent;
            entry.material.opacity = enabled ? entry.lens ? .08 : entry.material.name === "Glass" ? .035 : .12 : entry.opacity;
            entry.material.depthWrite = enabled ? false : entry.depthWrite;
            entry.material.needsUpdate = true;
        }
        components.visible = enabled; fuel.visible = enabled;
    }
    function setLamp(key, on, color, intensity = 2) {
        for (const mesh of lights.get(key) ?? []) {
            // Preserve safety lamp hues rather than bleaching red/amber through the studio tone map.
            mesh.material.toneMapped = !/^(tail|stop|turn|mirror):/.test(key);
            mesh.material.emissive.set(on ? color : 0x000000);
            mesh.material.emissiveIntensity = on ? intensity : 0;
            mesh.material.color.set(on ? color : key.startsWith("tail") || key.startsWith("stop") ? 0x501322 : key.startsWith("turn") || key.startsWith("mirror") ? 0x58351b : 0x293540);
            mesh.userData.on = Boolean(on);
        }
    }
    function applyState(next) {
        snapshot = next;
        const v = next.vehicle ?? {};
        const fitted = new Set((next.targets ?? []).map(target => target.id));
        for (const [id, group] of parts) group.visible = fitted.has(id);
        const ecus = new Map((next.ecus ?? []).map(ecu => [ecu.name.toLowerCase(), ecu]));
        for (const [id, { group, material }] of modules) {
            group.visible = fitted.has(id) && ecus.has(id);
            material.color.set(["fault", "busoff", "nopower"].includes(ecus.get(id)?.state) ? 0xca8131 : ecus.get(id)?.state === "sleep" ? 0x415260 : systemColors[id.toUpperCase()] ?? 0x387b87);
        }
        for (const [i, side] of ["l", "r"].entries()) {
            const low = Boolean(v.lowBeam && !(side === "l" && v.bulbLowLeftOpen));
            setLamp("low:" + side, low, 0xf5f1de, 2.8);
            setLamp("high:" + side, Boolean(v.highBeam), 0xe2f2ff, 5);
            setLamp("park:" + side, Boolean(v.parkLamps), 0xd6eaff, 1.1);
            setLamp("tail:" + side, Boolean(v.parkLamps || v.braking), 0xff0010, v.braking ? 1.6 : .3);
            setLamp("turn:" + side, side === "l" ? v.turnLeftLamp : v.turnRightLamp, 0xff7500, 1.8);
            setLamp("mirror:" + side, side === "l" ? v.turnLeftLamp : v.turnRightLamp, 0xff7500, 1.5);
            setLamp("reverse:" + side, Boolean(v.reversing), 0xe7f4ff, 3);
            beams[i].intensity = v.highBeam ? 36 : low ? 16 : 0;
            beams[i].angle = v.highBeam ? .18 : .29;
        }
        setLamp("stop:center", Boolean(v.braking), 0xff0010, 1.6);
        for (let i = 0; i < 4; i++) {
            doorPivots.get(wheelIds[i]).rotation.y = v.doors?.[i] ? (i % 2 === 0 ? -1 : 1) * 1.10 : 0;
            wheels[i].rotation.y = i < 2 ? THREE.MathUtils.degToRad(v.roadWheelAngle ?? 0) : 0;
            pins[i].position.y = ((v.doorsLocked ?? v.locked) ? .995 : 1.02) - .7;
        }
        for (const tyre of tyres) tyre.mesh.scale.y = Math.max(.75, Math.min(1, (v.tyres?.[tyre.index] ?? 2.4) / 2.4));
        steering.rotation.z = -THREE.MathUtils.degToRad(v.steering ?? (v.roadWheelAngle ?? 0) * 15);
        throttle.rotation.z = THREE.MathUtils.degToRad((v.throttle ?? 0) * .8);
        fuel.scale.y = Math.max(.02, Math.min(1, (v.fuel ?? 40) / 55));
        belt.visible = fitted.has("srs") && Boolean(v.seatBelt);
        bags.forEach((bag, i) => { bag.visible = fitted.has("srs") && Boolean(v.airbagFired?.[i]); });
        components.visible = diagnosticMode;
    }
    function hasMotion(state = snapshot) { return state.running === true && state.reducedMotion !== true && (state.vehicle?.wheels ?? []).some(speed => Math.abs(speed) > .05); }
    function advance(seconds) {
        const v = snapshot.vehicle ?? {}, sign = (v.velocity ?? 0) < 0 ? -1 : 1;
        wheelSpinners.forEach((spinner, i) => {
            const radians = Math.abs(v.wheels?.[i] ?? 0) / 3.6 * seconds / sedanAsset.wheels[wheelIds[i]][1];
            spinner.rotation.x = (spinner.rotation.x + sign * radians) % (Math.PI * 2);
        });
    }
    diagnostic(true); applyState({});
    return { root, shell, parts, modules, wheels, doorPivots, lights, beams, steering, belt, bags, fuel, throttle,
        wheelSpinners, diagnostic, applyState, advance, hasMotion };
}
