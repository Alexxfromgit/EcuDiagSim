import * as THREE from "./vendor/three.module.min.js";
import sedanAsset from "./assets/sedan-mesh.js";
import { locations, systemColors } from "./vehicle-state.js";
import { createKit, createMaterials, buildCabin, buildDoorCard, buildDrivetrain } from "./vehicle-parts.js";

// Artist-authored sedan surfaces, prepared offline from the attributed CC-BY asset.
const decoded = Uint8Array.from(atob(sedanAsset.binary), c => c.charCodeAt(0)).buffer;
const wheelIds = ["fl", "fr", "rl", "rr"];
const PAINT = 0x4a6987;
// Source material indices (see ATTRIBUTION.md / prepare-vehicle-asset.mjs).
const M = { paint: 0, mesh: 1, trim: 2, glass: 3, chrome: 4, head: 5, brake: 6, signal: 7, gloss: 8,
    tyre: 9, disc: 10, rimLight: 11, rimDark: 12, caliper: 13 };
const lampOff = { head: 0x9aa6b0, tail: 0x4a0710, turn: 0x5c3511, mirror: 0x2a1c12, reverse: 0x1a1f24, stop: 0x3a070d };
// Targets that are logical (network) or represented by an existing assembly rather than a body.
const noBody = new Set(["can_bus", "abs_e2e", "tyres", "intake_leak", "cooling", "oil_pump", "throttle_body", "alternator", "ignition_coils"]);
const controllerIds = ["ecm", "tcm", "abs", "bcm", "ipc", "srs"];

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

// Soft additive halo used to make lit lamps read as light sources without a bloom pass.
function glowTexture() {
    if (typeof document === "undefined") return null;
    const canvas = document.createElement("canvas"); canvas.width = canvas.height = 128;
    const context = canvas.getContext("2d");
    const gradient = context.createRadialGradient(64, 64, 0, 64, 64, 64);
    gradient.addColorStop(0, "rgba(255,255,255,1)"); gradient.addColorStop(.18, "rgba(255,255,255,.55)");
    gradient.addColorStop(.5, "rgba(255,255,255,.12)"); gradient.addColorStop(1, "rgba(255,255,255,0)");
    context.fillStyle = gradient; context.fillRect(0, 0, 128, 128);
    const texture = new THREE.CanvasTexture(canvas); texture.colorSpace = THREE.SRGBColorSpace;
    return texture;
}

// A spinning rim as the eye sees it: spokes averaged into a satin band, the polished lip as a ring.
function rimBlurTexture() {
    if (typeof document === "undefined") return null;
    const canvas = document.createElement("canvas"); canvas.width = canvas.height = 256;
    const context = canvas.getContext("2d");
    const gradient = context.createRadialGradient(128, 128, 0, 128, 128, 128);
    for (const [offset, color] of [[0, "rgba(40,43,46,0)"], [.15, "rgba(40,43,46,0)"], [.17, "rgba(70,74,79,.9)"],
        [.26, "rgba(150,156,162,.75)"], [.55, "rgba(132,138,145,.62)"], [.82, "rgba(150,156,162,.7)"],
        [.86, "rgba(205,209,213,.95)"], [.93, "rgba(190,195,200,.95)"], [.96, "rgba(40,42,45,.95)"], [1, "rgba(25,26,28,0)"]])
        gradient.addColorStop(offset, color);
    context.fillStyle = gradient; context.fillRect(0, 0, 256, 256);
    const texture = new THREE.CanvasTexture(canvas); texture.colorSpace = THREE.SRGBColorSpace;
    return texture;
}

// Group a lamp bank's vertices into separate light units (e.g. a turn signal's front and rear lenses).
function lampUnits(meshes) {
    const cells = new Map(), point = new THREE.Vector3();
    for (const mesh of meshes) {
        mesh.updateWorldMatrix(true, false);
        const positions = mesh.geometry.attributes.position;
        for (let i = 0; i < positions.count; i++) {
            point.fromBufferAttribute(positions, i).applyMatrix4(mesh.matrixWorld);
            const key = [Math.round(point.x / .25), Math.round(point.y / .25), Math.round(point.z / .25)].join(",");
            if (!cells.has(key)) cells.set(key, new THREE.Box3());
            cells.get(key).expandByPoint(point);
        }
    }
    const units = [];
    for (const box of cells.values()) {
        const near = units.find(unit => unit.clone().expandByScalar(.12).intersectsBox(box));
        if (near) near.union(box); else units.push(box.clone());
    }
    return units;
}

export function createVehicle() {
    const root = new THREE.Group(); root.name = "diagnostic-sedan";
    const shell = new THREE.Group(), components = new THREE.Group();
    components.name = "sensor-bodies";
    root.add(shell, components);
    const kit = createKit(), m = createMaterials();
    const parts = new Map(), modules = new Map(), wheels = [], doorPivots = new Map();
    const shellMaterials = new Map(), lights = new Map(), glows = new Map(), tyres = [], wheelSpinners = [], materials = new Map(), linings = [];
    const doorSkins = new Map(wheelIds.map(id => [id, []]));
    const black = m.plastic;
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
    const item = (geometry, material, position, parent = root) => kit.item(geometry, material, position, parent);
    const box = (size, position, material, parent = root) => kit.box(size, position, material, parent);
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
        if (part.door && part.material === M.paint) {
            const positions = geometry.attributes.position.array;
            for (let i = 0; i < positions.length; i += 3) doorSkins.get(part.door).push([positions[i], positions[i + 1], positions[i + 2]]);
        }
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
            const role = part.material === M.head ? part.lampRole : part.material === M.brake ? (part.lampRole === "stop" ? "stop" : "tail") : "turn";
            const lampKey = role === "stop" ? "stop:center" : role + ":" + part.lampSide;
            if (!lights.has(lampKey)) lights.set(lampKey, []);
            lights.get(lampKey).push(mesh);
        }
    }
    function extraLamp(key, size, position, color, parent = shell) {
        const material = new THREE.MeshStandardMaterial({ color: 0x000000, emissive: 0x000000, roughness: .45 });
        const mesh = box(size, position, material, parent); mesh.userData.lampColor = color; lights.set(key, [mesh]); return mesh;
    }
    for (const [side, sign] of [["l", -1], ["r", 1]]) {
        // Reverse lamps sit flush at the ends of the rear light band; repeaters on the mirror caps.
        extraLamp("reverse:" + side, [.11, .02, .008], [sign * .45, .655, 2.396], 0xe7f4ff).rotation.y = sign * .14;
        const mirror = extraLamp("mirror:" + side, [.006, .012, .045], [sign * .936, .972, -.745], 0xffa51f);
        mirror.rotation.y = sign * .4;
        doorPivots.get("f" + side).attach(mirror);
    }
    // Light halos, one per physical lamp unit, attached where the lamp moves (mirror caps with the door).
    const halo = glowTexture();
    for (const [key, meshes] of lights) {
        const sprites = [];
        for (const unit of lampUnits(meshes)) {
            const size = unit.getSize(new THREE.Vector3()), center = unit.getCenter(new THREE.Vector3());
            const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: halo, color: 0x000000, transparent: true,
                opacity: 0, depthWrite: false, blending: THREE.AdditiveBlending, toneMapped: false, fog: false }));
            const outward = center.z < 0 ? -1 : 1;
            center.z += outward * .03;
            sprite.position.copy(center);
            const extent = Math.max(size.x, size.y, size.z * .4);
            sprite.userData.size = Math.min(.5, .12 + extent * 1.2);
            sprite.renderOrder = 6;
            const parent = meshes[0].parent === shell ? shell : meshes[0].parent;
            parent.add(sprite); if (parent !== shell) parent.worldToLocal(sprite.position);
            sprites.push(sprite);
        }
        glows.set(key, sprites);
    }
    const plate = item(new THREE.PlaneGeometry(.44, .096), new THREE.MeshStandardMaterial({ color: 0xffffff, map: plateTexture(), roughness: .45 }), [0, .478, 2.474], shell);
    plate.rotation.x = .1; plate.castShadow = false; linings.push(plate);
    const beams = [];
    for (const x of [-.43, .43]) {
        const beam = new THREE.SpotLight(0xdcefff, 0, 14, .26, .55, 1.2);
        beam.position.set(x, .66, -2.36); beam.target.position.set(x, .02, -8);
        root.add(beam, beam.target); beams.push(beam);
    }
    // Neutral cap covers keep the adapted exterior unbranded.
    for (let i = 0; i < 4; i++) {
        const cap = item(new THREE.CylinderGeometry(.037, .037, .012, 24), m.satin,
            [i % 2 === 0 ? -.115 : .115, 0, 0], wheelSpinners[i]); cap.rotation.z = Math.PI / 2;
    }

    // Motion blur for the rims. About 20 spokes strobe once a frame turns the wheel by more than
    // half a spoke gap (a few km/h), so at speed a blurred rim face fades in over them, as a camera
    // shows a fast wheel. It is radially symmetric and steers with the wheel without spinning.
    const blurTexture = rimBlurTexture();
    const blurs = wheels.map((pivot, i) => {
        const side = i % 2 === 0 ? -1 : 1;
        const disc = item(new THREE.CircleGeometry(.245, 48), new THREE.MeshStandardMaterial({ map: blurTexture,
            color: 0xffffff, metalness: .6, roughness: .38, transparent: true, opacity: 0, depthWrite: false }), [side * .111, 0, 0], pivot);
        disc.rotation.y = side * Math.PI / 2; disc.castShadow = false; disc.receiveShadow = false;
        disc.visible = false; disc.name = "rim-blur:" + wheelIds[i];
        return disc;
    });

    // The artist's shell is open underneath. This shadow-only tray (never drawn to the screen) stops
    // light leaking through to the floor without being able to protrude from the body.
    const tray = box([1.6, .02, 4.3], [0, .24, -.02], new THREE.MeshBasicMaterial({ colorWrite: false, depthWrite: false }), shell);
    tray.receiveShadow = false; linings.push(tray);
    const { cabin, steering, cluster, display } = buildCabin(kit, m, root);
    const { bay, throttle, fuel } = buildDrivetrain(kit, m, root);
    // Inside the body the roof and bonnet already shade everything; ~150 small interior casters
    // would only double their draw calls in the shadow pass.
    for (const group of [cabin, bay]) group.traverse(object => { object.castShadow = false; });
    // Door cards follow the inside of each skin and swing with the door.
    const pins = wheelIds.map(id => {
        const door = doorPivots.get(id), side = id.endsWith("l") ? -1 : 1;
        const card = buildDoorCard(kit, m, doorSkins.get(id), side);
        root.add(card); door.attach(card);
        const sill = card.userData.sill ?? { x: side * .70, y: .935 };
        const pin = box([.012, .03, .012], [sill.x, sill.y, door.position.z + (id.startsWith("f") ? .82 : .62)], m.satin);
        door.attach(pin); pin.userData.y = sill.y;
        return pin;
    });
    // Restraints: shown only when the SRS pack is fitted and the occupant is belted / a bag fired.
    const belt = kit.tube([[-.66, 1.10, .37], [-.50, 1.00, .33], [-.36, .80, .27], [-.19, .62, .24], [-.40, .63, .21], [-.63, .60, .24]], .012, m.belt, cabin);
    const bags = [kit.blob([.24, .22, .14], [-.40, .97, -.37], m.airbag, cabin), kit.blob([.30, .24, .20], [.40, 1.05, -.60], m.airbag, cabin)];
    for (const bag of bags) bag.castShadow = false;

    // Sensor and control-unit bodies at their anchors (diagnostic mode only).
    const sensorShell = new THREE.MeshStandardMaterial({ color: 0x8b9aab, metalness: .5, roughness: .4 });
    for (const [id, anchor] of Object.entries(locations)) {
        if (noBody.has(id)) continue;
        const group = new THREE.Group(); group.name = "component:" + id; group.position.set(...anchor); components.add(group); parts.set(id, group);
        if (controllerIds.includes(id)) {
            const material = new THREE.MeshStandardMaterial({ color: systemColors[id.toUpperCase()] ?? 0x387b87, metalness: .45, roughness: .4 });
            const size = { ecm: [.20, .045, .16], tcm: [.14, .04, .12], abs: [.13, .06, .10], bcm: [.18, .04, .15], ipc: [.28, .08, .05], srs: [.12, .045, .14] }[id];
            box(size, [0, 0, 0], material, group);
            box([size[0] * .7, .022, .03], [0, size[1] / 2 + .011, -size[2] / 2 + .025], black, group);
            if (id === "abs") {
                box([.13, .09, .10], [0, -.075, 0], m.alloy, group);
                for (let i = 0; i < 4; i++) kit.rod([-.045 + i * .03, -.03, .05], [-.045 + i * .03, .01, .10], .005, m.satin, group);
            }
            if (id === "ipc") group.visible = false;
            modules.set(id, { group, material });
            continue;
        }
        const sensorColor = new THREE.MeshStandardMaterial({ color: 0x24303b, roughness: .5 });
        kit.item(new THREE.CylinderGeometry(.013, .013, .035, 16), sensorShell, [0, 0, 0], group);
        kit.item(new THREE.CylinderGeometry(.019, .019, .01, 6), sensorShell, [0, .012, 0], group);
        box([.026, .02, .03], [0, .03, 0], sensorColor, group);
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
        components.visible = enabled;
        m.tank.opacity = enabled ? .45 : 1; m.tank.transparent = enabled; m.tank.depthWrite = !enabled; m.tank.needsUpdate = true;
        fuel.visible = enabled;
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
        for (const sprite of glows.get(key) ?? []) {
            sprite.visible = Boolean(on) && sprite.material.map !== null;
            sprite.material.color.set(color);
            sprite.material.opacity = Math.min(1, .25 + intensity * .18);
            sprite.scale.setScalar(sprite.userData.size * (.8 + Math.min(intensity, 5) * .12));
        }
    }
    function applyState(next) {
        snapshot = next;
        const v = next.vehicle ?? {};
        const fitted = new Set((next.targets ?? []).map(target => target.id));
        for (const [id, group] of parts) group.visible = fitted.has(id) && id !== "ipc";
        const ecus = new Map((next.ecus ?? []).map(ecu => [ecu.name.toLowerCase(), ecu]));
        for (const [id, { group, material }] of modules) {
            group.visible = fitted.has(id) && ecus.has(id) && id !== "ipc";
            material.color.set(["fault", "busoff", "nopower"].includes(ecus.get(id)?.state) ? 0xca8131 : ecus.get(id)?.state === "sleep" ? 0x415260 : systemColors[id.toUpperCase()] ?? 0x387b87);
        }
        for (const [i, side] of ["l", "r"].entries()) {
            const low = Boolean(v.lowBeam && !(side === "l" && v.bulbLowLeftOpen));
            setLamp("low:" + side, low, 0xf5f1de, 2.8);
            setLamp("high:" + side, Boolean(v.highBeam), 0xe2f2ff, 5);
            setLamp("park:" + side, Boolean(v.parkLamps), 0xd6eaff, 1.1);
            setLamp("tail:" + side, Boolean(v.parkLamps || v.braking), 0xff0010, v.braking ? 1.6 : .3);
            setLamp("turn:" + side, side === "l" ? v.turnLeftLamp : v.turnRightLamp, 0xff7500, 2.2);
            setLamp("mirror:" + side, side === "l" ? v.turnLeftLamp : v.turnRightLamp, 0xff7500, 1.5);
            setLamp("reverse:" + side, Boolean(v.reversing), 0xe7f4ff, 3);
            // Each headlamp lights the road ahead: a dipped pool, or a longer, narrower main beam.
            beams[i].intensity = v.highBeam ? 150 : low ? 70 : 0;
            beams[i].angle = v.highBeam ? .26 : .34;
            beams[i].target.position.y = v.highBeam ? -.4 : -1.3;
        }
        setLamp("stop:center", Boolean(v.braking), 0xff0010, 1.6);
        for (let i = 0; i < 4; i++) {
            doorPivots.get(wheelIds[i]).rotation.y = v.doors?.[i] ? (i % 2 === 0 ? -1 : 1) * 1.10 : 0;
            wheels[i].rotation.y = i < 2 ? THREE.MathUtils.degToRad(v.roadWheelAngle ?? 0) : 0;
            pins[i].position.y = pins[i].userData.y - ((v.doorsLocked ?? v.locked) ? .018 : 0) - doorPivots.get(wheelIds[i]).position.y;
        }
        blurs.forEach((disc, i) => {
            const t = THREE.MathUtils.clamp((Math.abs(v.wheels?.[i] ?? 0) - 5) / 25, 0, 1);
            disc.material.opacity = t * t * (3 - 2 * t) * .92;
            disc.visible = disc.material.opacity > .01 && blurTexture !== null;
        });
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
            // Rolling without slip: angle = distance / radius. Driving forward (towards -Z) turns
            // the top of the wheel forward, which is a negative rotation about +X.
            const radians = Math.abs(v.wheels?.[i] ?? 0) / 3.6 * seconds / sedanAsset.wheels[wheelIds[i]][1];
            spinner.rotation.x = (spinner.rotation.x - sign * radians) % (Math.PI * 2);
        });
    }
    diagnostic(true); applyState({});
    return { root, shell, cabin, bay, parts, modules, wheels, blurs, doorPivots, lights, glows, beams, steering, belt, bags, fuel, throttle,
        wheelSpinners, diagnostic, applyState, advance, hasMotion };
}
