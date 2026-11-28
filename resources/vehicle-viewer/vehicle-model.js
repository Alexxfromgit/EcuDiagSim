import * as THREE from "./vendor/three.module.min.js";
import sedanAsset from "./assets/sedan-mesh.js";
import { locations, systemColors } from "./vehicle-state.js";

// Artist-authored sedan surfaces, prepared offline from the attributed CC-BY asset.
const decoded = Uint8Array.from(atob(sedanAsset.binary), c => c.charCodeAt(0)).buffer;
const wheelIds = ["fl", "fr", "rl", "rr"];
const PAINT = 0x4a6987;
// Source material indices (see ATTRIBUTION.md / prepare-vehicle-asset.mjs).
const M = { paint: 0, mesh: 1, trim: 2, glass: 3, chrome: 4, head: 5, brake: 6, signal: 7, gloss: 8,
    tyre: 9, disc: 10, rimLight: 11, rimDark: 12, caliper: 13 };
const lampOff = { head: 0x9aa6b0, tail: 0x4a0710, turn: 0x5c3511, mirror: 0x2a1c12, reverse: 0x1a1f24, stop: 0x3a070d };

function surface(index, lens) {
    const physical = (options) => new THREE.MeshPhysicalMaterial(options);
    switch (index) {
    case M.paint: return physical({ color: PAINT, metalness: .4, roughness: .36, clearcoat: 1, clearcoatRoughness: .04 });
    case M.mesh: return physical({ color: 0x0c0e10, metalness: .3, roughness: .62 });
    case M.trim: return physical({ color: 0x0b0d0f, roughness: .7 });
    case M.glass: return lens
        ? physical({ color: 0xe8eef2, roughness: .02, transparent: true, opacity: .16, depthWrite: false, side: THREE.DoubleSide })
        : physical({ color: 0x0b1117, roughness: .04, transparent: true, opacity: .55, depthWrite: false, side: THREE.DoubleSide, envMapIntensity: 1.6 });
    case M.chrome: return physical({ color: 0xc3c9cf, metalness: 1, roughness: .2 });
    case M.gloss: return physical({ color: 0x040506, roughness: .1, clearcoat: 1, clearcoatRoughness: .04 });
    case M.tyre: return physical({ color: 0x141517, roughness: .88 });
    case M.disc: return physical({ color: 0x777b80, metalness: .9, roughness: .42 });
    case M.rimLight: return physical({ color: 0xd2d6da, metalness: 1, roughness: .2 });
    case M.rimDark: return physical({ color: 0x9aa0a7, metalness: 1, roughness: .26 });
    case M.caliper: return physical({ color: 0x9e1219, roughness: .38, clearcoat: .6 });
    default: return physical({ color: 0x808080, roughness: .5 });
    }
}

// A neutral, unregistered plate for the rear recess. (No canvas when the model runs headless in tests.)
function plateTexture() {
    if (typeof document === "undefined") return null;
    const canvas = document.createElement("canvas"); canvas.width = 512; canvas.height = 112;
    const context = canvas.getContext("2d");
    context.fillStyle = "#eef1f3"; context.fillRect(0, 0, 512, 112);
    context.strokeStyle = "#20262c"; context.lineWidth = 5; context.strokeRect(6, 6, 500, 100);
    context.fillStyle = "#1d2329"; context.font = "600 70px ui-monospace, Menlo, Consolas, monospace";
    context.textAlign = "center"; context.textBaseline = "middle"; context.fillText("ECU·SIM", 256, 60);
    const texture = new THREE.CanvasTexture(canvas); texture.colorSpace = THREE.SRGBColorSpace; texture.anisotropy = 4;
    return texture;
}

export function createVehicle() {
    const root = new THREE.Group(); root.name = "diagnostic-sedan";
    const shell = new THREE.Group(), components = new THREE.Group();
    root.add(shell, components);
    const parts = new Map(), modules = new Map(), wheels = [], doorPivots = new Map();
    const shellMaterials = new Map(), lights = new Map(), tyres = [], wheelSpinners = [], materials = new Map(), linings = [];
    const dark = new THREE.MeshStandardMaterial({ color: 0x1c2630, roughness: .65 });
    const metal = new THREE.MeshStandardMaterial({ color: 0x7a8c99, metalness: .7, roughness: .35 });
    const black = new THREE.MeshStandardMaterial({ color: 0x101820, roughness: .7 });
    // Cabin: soft-touch charcoal, darker leather, satin aluminium accents.
    const cabin = new THREE.MeshStandardMaterial({ color: 0x111316, roughness: .95 });
    const leather = new THREE.MeshStandardMaterial({ color: 0x24211f, roughness: .68 });
    const satin = new THREE.MeshStandardMaterial({ color: 0x9da4aa, metalness: .9, roughness: .3 });
    // Inner faces of the painted shell: what an open door, a shut line or the windows reveal.
    // These, the underbody tray and the plate belong to the solid exterior only (`linings`).
    const lining = new THREE.MeshStandardMaterial({ color: 0x15171a, roughness: .9, side: THREE.BackSide });
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
    function cushion(size, position, material = dark, parent = root) {
        const [w, h, d] = size, r = Math.min(w, h, d) * .25;
        const shape = new THREE.Shape();
        shape.moveTo(-w / 2 + r, -h / 2);
        shape.lineTo(w / 2 - r, -h / 2); shape.quadraticCurveTo(w / 2, -h / 2, w / 2, -h / 2 + r);
        shape.lineTo(w / 2, h / 2 - r); shape.quadraticCurveTo(w / 2, h / 2, w / 2 - r, h / 2);
        shape.lineTo(-w / 2 + r, h / 2); shape.quadraticCurveTo(-w / 2, h / 2, -w / 2, h / 2 - r);
        shape.lineTo(-w / 2, -h / 2 + r); shape.quadraticCurveTo(-w / 2, -h / 2, -w / 2 + r, -h / 2);
        const geometry = new THREE.ExtrudeGeometry(shape, { depth: d - 2 * r, bevelEnabled: true, bevelSize: r, bevelThickness: r, bevelSegments: 3, steps: 1 });
        geometry.translate(0, 0, -d / 2 + r);
        return item(geometry, material, position, parent);
    }
    // A side profile (z, y) extruded across the car's width, centred on x = 0.
    function profile(points, width, material, parent = root) {
        const shape = new THREE.Shape(points.map(([z, y]) => new THREE.Vector2(z, y)));
        const geometry = new THREE.ExtrudeGeometry(shape, { depth: width, bevelEnabled: true, bevelSize: .015, bevelThickness: .015, bevelSegments: 2, curveSegments: 8 });
        geometry.rotateY(-Math.PI / 2); geometry.translate(width / 2, 0, 0);
        return item(geometry, material, [0, 0, 0], parent);
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
    // Hinges are on each door's leading edge, from the prepared shut lines.
    for (const id of wheelIds) {
        const pivot = new THREE.Group(); pivot.name = "door:" + id;
        pivot.position.set(...sedanAsset.doors[id]);
        shell.add(pivot); doorPivots.set(id, pivot);
    }
    for (const part of sedanAsset.parts) {
        const source = sedanAsset.materials[part.material];
        const lamp = [M.head, M.brake, M.signal].includes(part.material);
        const key = part.material + (part.lens ? ":lens" : "");
        // Lamps switch individually, so each bank owns its material; other surfaces share theirs.
        let material = lamp ? new THREE.MeshPhysicalMaterial({ color: 0x000000, roughness: .12, clearcoat: 1, clearcoatRoughness: .05 }) : materials.get(key);
        if (!material) {
            material = surface(part.material, part.lens);
            // The shell has no underbody, so its outer faces must cast the shadow (offset by normal bias).
            if (part.material !== M.glass) material.shadowSide = THREE.FrontSide;
            materials.set(key, material);
        }
        material.name = source.name;
        let parent = shell;
        const geometry = geometryFor(part);
        if (part.wheel) {
            const index = wheelIds.indexOf(part.wheel);
            parent = part.node.includes("BrakePad") ? wheels[index] : wheelSpinners[index];
            const center = sedanAsset.wheels[part.wheel]; geometry.translate(-center[0], -center[1], -center[2]);
        } else if (part.door) {
            parent = doorPivots.get(part.door);
            const hinge = parent.position; geometry.translate(-hinge.x, -hinge.y, -hinge.z);
        }
        const mesh = item(geometry, material, [0, 0, 0], parent);
        mesh.name = [source.name, part.wheel ?? part.door ?? "body", part.lampSide ?? "", part.lampRole ?? ""].join(":");
        if (part.material === M.glass) mesh.castShadow = false;
        if (part.material === M.paint && !part.wheel) {
            const inner = item(geometry, lining, [0, 0, 0], parent); inner.castShadow = false; linings.push(inner);
        }
        if (part.material === M.tyre) tyres.push({ mesh, index: wheelIds.indexOf(part.wheel) });
        if (!part.wheel && !lamp && !shellMaterials.has(material))
            shellMaterials.set(material, { opacity: material.opacity, transparent: material.transparent, depthWrite: material.depthWrite, side: material.side, lens: part.lens });
        if (lamp) {
            const role = part.material === M.head ? part.lampRole : part.material === M.brake ? "tail" : "turn";
            const lampKey = role + ":" + part.lampSide;
            if (!lights.has(lampKey)) lights.set(lampKey, []);
            lights.get(lampKey).push(mesh);
        }
    }
    function extraLamp(key, size, position, color, parent = shell) {
        const material = new THREE.MeshStandardMaterial({ color: 0x000000, emissive: 0x000000, roughness: .45 });
        const mesh = box(size, position, material, parent); mesh.userData.lampColor = color; lights.set(key, [mesh]); return mesh;
    }
    // Centre high-mounted stop lamp, on the parcel shelf under the rear screen.
    extraLamp("stop:center", [.34, .022, .03], [0, .975, 1.66], 0xff243d).rotation.x = -.5;
    for (const [side, sign] of [["l", -1], ["r", 1]]) {
        // Reverse lamps sit flush at the ends of the rear light band; repeaters on the mirror caps.
        extraLamp("reverse:" + side, [.11, .02, .008], [sign * .45, .655, 2.396], 0xe7f4ff).rotation.y = sign * .14;
        const mirror = extraLamp("mirror:" + side, [.006, .012, .045], [sign * .936, .972, -.745], 0xffa51f);
        mirror.rotation.y = sign * .4;
        const door = doorPivots.get("f" + side); door.attach(mirror);
    }
    const plate = item(new THREE.PlaneGeometry(.44, .096), new THREE.MeshStandardMaterial({ color: 0xffffff, map: plateTexture(), roughness: .45 }), [0, .478, 2.474], shell);
    plate.rotation.x = .1; plate.castShadow = false; linings.push(plate);
    const beams = [];
    for (const x of [-.43, .43]) {
        const beam = new THREE.SpotLight(0xdcefff, 0, 12, .26, .65, 1.4);
        beam.position.set(x, .66, -2.36); beam.target.position.set(x, .02, -8);
        root.add(beam, beam.target); beams.push(beam);
    }
    // Neutral cap covers keep the adapted exterior unbranded.
    for (let i = 0; i < 4; i++) {
        const cap = item(new THREE.CylinderGeometry(.037, .037, .012, 24), satin,
            [i % 2 === 0 ? -.115 : .115, 0, 0], wheelSpinners[i]); cap.rotation.z = Math.PI / 2;
    }

    // The artist's shell is open underneath. This shadow-only tray (never drawn to the screen) stops
    // light leaking through to the floor without being able to protrude from the body.
    const tray = box([1.6, .02, 4.3], [0, .24, -.02], new THREE.MeshBasicMaterial({ colorWrite: false, depthWrite: false }), shell);
    tray.receiveShadow = false; linings.push(tray);
    // Engine bay and cabin are additive: the exterior uses the artist's continuous body surfaces.
    box([1.28, .08, 3.5], [0, .31, 0], dark);
    blob([.43, .19, .42], [0, .63, -1.50], metal);
    box([.77, .11, .65], [0, .78, -1.47], dark);
    for (let i = -2; i <= 2; i++) box([.025, .015, .49], [i * .13, .84, -1.46], metal);
    box([.40, .20, .55], [.13, .45, -.86], metal);
    box([1.18, .26, .07], [0, .60, -2.08], dark);
    box([.27, .15, .32], [.56, .68, -1.76], dark);
    line([[.56, .75, -1.76], [.61, .80, -1.5], [.38, .76, -1.09]], .045, black);
    box([.82, .17, .60], [0, .45, 1.34], dark);
    const fuel = box([.74, .11, .52], [0, .46, 1.34], new THREE.MeshStandardMaterial({ color: 0x4d9c9c, transparent: true, opacity: .6 }));
    line([[-.25, .37, -1.4], [-.23, .26, -.35], [.2, .26, 1.3], [.45, .30, 2.30]], .03, metal);
    // Cabin. Seats: cushion, reclined back and headrest; front pair and rear outer places.
    for (const side of [-1, 1]) for (const z of [.02, .92]) {
        cushion([.46, .12, .48], [side * .40, .58, z], leather);
        const back = cushion([.45, .47, .13], [side * .40, .84, z + .19], leather); back.rotation.x = -.18;
        cushion([.26, .13, .09], [side * .40, 1.14, z + .24], leather);
    }
    // Dashboard: a moulded profile across the cabin, with a satin trim inlay and two displays.
    profile([[-.98, .62], [-.98, .90], [-.80, .95], [-.60, .965], [-.52, .955], [-.49, .90], [-.50, .80], [-.58, .70], [-.70, .62]], 1.44, cabin);
    box([1.30, .014, .012], [0, .895, -.487], satin);
    const screen = new THREE.MeshPhysicalMaterial({ color: 0x050709, roughness: .08, clearcoat: 1, emissive: 0x000000 });
    const cluster = box([.30, .10, .012], [-.40, .975, -.53], screen); cluster.rotation.x = -.35;
    const display = box([.30, .17, .012], [0, .96, -.50], screen); display.rotation.x = -.25;
    cushion([.22, .16, .78], [0, .48, -.12], cabin);
    box([1.30, .02, .40], [0, .955, 1.50], cabin); // parcel shelf
    const steering = item(new THREE.TorusGeometry(.17, .016, 10, 40), black, [-.40, 1.01, -.42]); steering.rotation.x = -.35;
    item(new THREE.CylinderGeometry(.05, .05, .05, 20), black, [-.40, 1.01, -.43]).rotation.x = Math.PI / 2 - .35;
    item(new THREE.CylinderGeometry(.025, .03, .22, 12), black, [-.40, .97, -.53]).rotation.x = Math.PI / 2 - .35;
    const throttle = item(new THREE.CylinderGeometry(.06, .06, .018, 20), metal, [.38, .78, -1.08]);
    const belt = line([[-.60, 1.07, .17], [-.27, .67, -.02], [-.60, .61, .2]], .012,
        new THREE.MeshStandardMaterial({ color: 0x171d24 }));
    const bags = [blob([.24, .20, .15], [-.40, 1.05, -.36], new THREE.MeshStandardMaterial({ color: 0xe7e3d8, roughness: .95 })),
        blob([.28, .22, .19], [.40, 1.05, -.49], new THREE.MeshStandardMaterial({ color: 0xe7e3d8, roughness: .95 }))];
    const pinHeight = { locked: .925, unlocked: .95 };
    const pins = wheelIds.map(id => {
        const door = doorPivots.get(id), hinge = door.position;
        const pin = box([.014, .025, .014], [hinge.x * .93, pinHeight.unlocked, hinge.z + .85], satin);
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
        for (const [material, base] of shellMaterials) {
            material.transparent = enabled || base.transparent;
            material.opacity = enabled ? base.lens ? .08 : material.name === "Glass" ? .035 : .12 : base.opacity;
            material.depthWrite = enabled ? false : base.depthWrite;
            // The ghosted shell shows both of its walls; the solid exterior culls its inner faces.
            material.side = enabled ? THREE.DoubleSide : base.side;
            material.needsUpdate = true;
        }
        for (const mesh of linings) mesh.visible = !enabled;
        components.visible = enabled; fuel.visible = enabled;
    }
    function setLamp(key, on, color, intensity = 2) {
        const role = key.split(":")[0];
        for (const mesh of lights.get(key) ?? []) {
            // Preserve safety lamp hues rather than bleaching red/amber through the studio tone map.
            mesh.material.toneMapped = !/^(tail|stop|turn|mirror):/.test(key);
            mesh.material.emissive.set(on ? color : 0x000000);
            mesh.material.emissiveIntensity = on ? intensity : 0;
            mesh.material.color.set(on ? color : lampOff[role] ?? lampOff.head);
            mesh.material.metalness = on || role !== "low" && role !== "high" && role !== "park" ? 0 : .6;
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
            pins[i].position.y = ((v.doorsLocked ?? v.locked) ? pinHeight.locked : pinHeight.unlocked) - doorPivots.get(wheelIds[i]).position.y;
        }
        for (const tyre of tyres) tyre.mesh.scale.y = Math.max(.75, Math.min(1, (v.tyres?.[tyre.index] ?? 2.4) / 2.4));
        steering.rotation.z = -THREE.MathUtils.degToRad(v.steering ?? (v.roadWheelAngle ?? 0) * 15);
        throttle.rotation.z = THREE.MathUtils.degToRad((v.throttle ?? 0) * .8);
        fuel.scale.y = Math.max(.02, Math.min(1, (v.fuel ?? 40) / 55));
        belt.visible = fitted.has("srs") && Boolean(v.seatBelt);
        bags.forEach((bag, i) => { bag.visible = fitted.has("srs") && Boolean(v.airbagFired?.[i]); });
        // The displays wake with the ignition.
        for (const panel of [cluster, display]) panel.material.emissive.set(v.ignition ? 0x0d2a36 : 0x000000);
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
