import * as THREE from "./vendor/three.module.min.js";
import { locations, systemColors } from "./vehicle-state.js";

// Original contemporary four-door sedan: 4.96 m long, 1.9 m wide, 1.46 m high.
// All geometry is authored here; no branded vehicle, purchased asset or external texture.
export function createVehicle() {
    const root = new THREE.Group(); root.name = "modern-sedan";
    const shell = new THREE.Group(); root.add(shell);
    const outlines = new THREE.Group(); root.add(outlines);
    const components = new THREE.Group(); root.add(components);
    const paint = new THREE.MeshPhysicalMaterial({ color: 0x65899f, metalness: .78, roughness: .23, clearcoat: 1, clearcoatRoughness: .18 });
    const glass = new THREE.MeshPhysicalMaterial({ color: 0x17313e, metalness: .24, roughness: .12, clearcoat: 1,
        transparent: true, opacity: .9, side: THREE.DoubleSide });
    const chrome = new THREE.MeshStandardMaterial({ color: 0xa9bac5, metalness: .92, roughness: .2 });
    const alloy = new THREE.MeshStandardMaterial({ color: 0x8c9ca9, metalness: .8, roughness: .32 });
    const dark = new THREE.MeshStandardMaterial({ color: 0x17232d, roughness: .58, metalness: .12 });
    const leather = new THREE.MeshStandardMaterial({ color: 0x37404a, roughness: .86 });
    const rubber = new THREE.MeshStandardMaterial({ color: 0x13171d, roughness: .88 });
    const outerTrim = new THREE.MeshStandardMaterial({ color: 0x091119, roughness: .36, metalness: .35 });
    const outlineMaterial = new THREE.MeshBasicMaterial({ color: 0x69c5da, transparent: true, opacity: .23, depthWrite: false });
    const parts = new Map(), modules = new Map(), wheels = [];
    const wheelZ = [-1.48, 1.46];
    let diagnosticMode = true;

    function mesh(geometry, material, position = [0, 0, 0], parent = root) {
        const item = new THREE.Mesh(geometry, material);
        item.position.set(...position); item.castShadow = true; item.receiveShadow = true;
        parent.add(item); return item;
    }
    function box(size, position, material = dark, parent = root) {
        return mesh(new THREE.BoxGeometry(...size), material, position, parent);
    }
    function roundedBox(size, position, material = dark, parent = root, radius = .035) {
        const [w, h, d] = size;
        const r = Math.min(radius, w / 3, h / 3, d / 3);
        const shape = new THREE.Shape();
        shape.moveTo(-w / 2 + r, -h / 2);
        shape.lineTo(w / 2 - r, -h / 2); shape.quadraticCurveTo(w / 2, -h / 2, w / 2, -h / 2 + r);
        shape.lineTo(w / 2, h / 2 - r); shape.quadraticCurveTo(w / 2, h / 2, w / 2 - r, h / 2);
        shape.lineTo(-w / 2 + r, h / 2); shape.quadraticCurveTo(-w / 2, h / 2, -w / 2, h / 2 - r);
        shape.lineTo(-w / 2, -h / 2 + r); shape.quadraticCurveTo(-w / 2, -h / 2, -w / 2 + r, -h / 2);
        const geometry = new THREE.ExtrudeGeometry(shape, { depth: d - 2 * r, bevelEnabled: true,
            bevelThickness: r, bevelSize: r * .5, bevelSegments: 3, steps: 1, curveSegments: 5 });
        geometry.translate(0, 0, -d / 2 + r);
        return mesh(geometry, material, position, parent);
    }
    function tube(points, radius, material = alloy, parent = root) {
        const curve = new THREE.CatmullRomCurve3(points.map(point => new THREE.Vector3(...point)), false, "centripetal");
        return mesh(new THREE.TubeGeometry(curve, Math.max(16, points.length * 8), radius, 6, false), material, [0, 0, 0], parent);
    }
    function sphere(size, position, material, parent = root) {
        const item = mesh(new THREE.SphereGeometry(1, 20, 12), material, position, parent);
        item.scale.set(...size); return item;
    }
    function cylinder(radius, length, position, material, parent = root) {
        const item = mesh(new THREE.CylinderGeometry(radius, radius, length, 32), material, position, parent);
        item.rotation.z = Math.PI / 2; return item;
    }
    function reverseWinding(geometry) {
        if (geometry.index) {
            const indices = geometry.index.array;
            for (let i = 0; i < indices.length; i += 3) [indices[i + 1], indices[i + 2]] = [indices[i + 2], indices[i + 1]];
        } else {
            for (const attribute of Object.values(geometry.attributes)) {
                const { array, itemSize } = attribute;
                for (let i = 0; i < attribute.count; i += 3)
                    for (let c = 0; c < itemSize; c++) [array[(i + 1) * itemSize + c], array[(i + 2) * itemSize + c]] =
                        [array[(i + 2) * itemSize + c], array[(i + 1) * itemSize + c]];
            }
        }
    }
    function sideGeometry(shape, side, depth = .035) {
        const original = new THREE.ExtrudeGeometry(shape, { depth, bevelEnabled: true, bevelSize: .015,
            bevelThickness: .015, bevelSegments: 3, steps: 1, curveSegments: 24 });
        // Tessellate before bending the sheet, then share vertices for continuous paint normals.
        // Bending only the sparse triangulation of an extruded silhouette creates large facets.
        let positions = Array.from(original.attributes.position.array);
        original.dispose();
        for (let pass = 0; pass < 2; pass++) {
            const divided = [];
            for (let i = 0; i < positions.length; i += 9) {
                const a = positions.slice(i, i + 3), b = positions.slice(i + 3, i + 6), c = positions.slice(i + 6, i + 9);
                const ab = a.map((v, j) => (v + b[j]) / 2), bc = b.map((v, j) => (v + c[j]) / 2), ac = a.map((v, j) => (v + c[j]) / 2);
                divided.push(...a, ...ab, ...ac, ...ab, ...b, ...bc, ...ac, ...bc, ...c, ...ab, ...bc, ...ac);
            }
            positions = divided;
        }
        const unique = [], indices = [], lookup = new Map();
        for (let i = 0; i < positions.length; i += 3) {
            const z = positions[i], y = positions[i + 1], depth = positions[i + 2];
            const taper = 1 - .15 * Math.pow(Math.min(1, Math.abs(z) / 2.48), 5);
            const width = (.925 + .018 * Math.cos((y - .7) * 5)) * taper;
            const point = [side * (width + depth), y, z];
            const key = point.map(value => Math.round(value * 100000)).join(",");
            if (!lookup.has(key)) { lookup.set(key, unique.length / 3); unique.push(...point); }
            indices.push(lookup.get(key));
        }
        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute("position", new THREE.Float32BufferAttribute(unique, 3));
        geometry.setIndex(indices);
        if (side === 1) reverseWinding(geometry);
        geometry.computeVertexNormals(); return geometry;
    }
    function topPanel(stations, material = paint, parent = shell) {
        const curves = [0, 1, 2].map(column => new THREE.CatmullRomCurve3(stations.map(s => new THREE.Vector3(s[0], s[column + 1], 0))));
        const vertices = [], indices = [];
        const lengthSteps = 40, widthSteps = 24;
        for (let i = 0; i <= lengthSteps; i++) {
            const width = curves[0].getPoint(i / lengthSteps);
            const center = curves[1].getPoint(i / lengthSteps);
            const edge = curves[2].getPoint(i / lengthSteps);
            for (let j = 0; j <= widthSteps; j++) {
                const u = j / widthSteps * 2 - 1;
                vertices.push(u * width.y, center.y + (edge.y - center.y) * Math.pow(Math.abs(u), 2.7), width.x);
            }
        }
        for (let i = 0; i < lengthSteps; i++) for (let j = 0; j < widthSteps; j++) {
            const a = i * (widthSteps + 1) + j;
            indices.push(a, a + widthSteps + 1, a + 1, a + 1, a + widthSteps + 1, a + widthSteps + 2);
        }
        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute("position", new THREE.Float32BufferAttribute(vertices, 3));
        geometry.setIndex(indices); geometry.computeVertexNormals();
        return mesh(geometry, material, [0, 0, 0], parent);
    }
    function glassSide(points, side) {
        const shape = new THREE.Shape(points.map(([z, y]) => new THREE.Vector2(z, y)));
        const geometry = new THREE.ShapeGeometry(shape, 24);
        const vertices = geometry.attributes.position;
        const contour = [];
        for (let i = 0; i < vertices.count; i++) {
            const z = vertices.getX(i), y = vertices.getY(i);
            vertices.setXYZ(i, side * (.93 - (y - .96) * .48), y, z);
        }
        geometry.computeVertexNormals();
        mesh(geometry, glass, [0, 0, 0], shell);
        for (const [z, y] of [...points, points[0]]) contour.push([side * (.932 - (y - .96) * .48), y, z]);
        tube(contour, .018, outerTrim, shell);
        tube(contour.map(p => [p[0] * 1.009, p[1], p[2]]), .005, chrome, shell);
        tube(contour, .005, outlineMaterial, outlines);
    }

    // Separate side panels contain actual wheel openings instead of intersecting solid tyres.
    const sideShape = new THREE.Shape();
    sideShape.moveTo(-2.48, .35); sideShape.lineTo(-2.45, .68);
    sideShape.bezierCurveTo(-2.35, .87, -1.95, .94, -1.05, .98);
    sideShape.bezierCurveTo(-.5, 1.0, .85, .99, 1.53, .95);
    sideShape.bezierCurveTo(2.1, .93, 2.4, .85, 2.47, .72);
    sideShape.lineTo(2.48, .35);
    for (const z of [...wheelZ].reverse()) {
        sideShape.lineTo(z + .415, .35);
        sideShape.absarc(z, .37, .415, -.048, Math.PI + .048, false);
    }
    sideShape.lineTo(-2.48, .35); sideShape.closePath();
    for (const side of [-1, 1]) mesh(sideGeometry(sideShape, side), paint, [0, 0, 0], shell);
    topPanel([[-2.46, .79, .76, .71], [-2.22, .89, .91, .85], [-1.67, .94, .98, .93], [-1.08, .93, 1.0, .97]]);
    topPanel([[1.25, .91, 1.0, .96], [1.75, .93, .97, .92], [2.22, .88, .9, .84], [2.46, .79, .79, .73]]);
    topPanel([[-.55, .68, 1.39, 1.35], [-.33, .72, 1.45, 1.40], [.43, .72, 1.46, 1.40], [.72, .67, 1.39, 1.34]]);
    topPanel([[-1.10, .82, 1.01, .97], [-.82, .77, 1.23, 1.16], [-.53, .68, 1.39, 1.35]], glass);
    topPanel([[.69, .67, 1.39, 1.34], [1.0, .76, 1.23, 1.17], [1.35, .84, 1.0, .97]], glass);
    for (const side of [-1, 1]) {
        glassSide([[-1.01, 1.0], [-.49, 1.36], [-.27, 1.39], [.13, 1.39], [.13, 1.0]], side);
        glassSide([[.23, 1.0], [.23, 1.39], [.62, 1.37], [1.21, 1.0]], side);
        tube([[side * .84, .99, -1.13], [side * .69, 1.36, -.54], [side * .67, 1.39, -.45]], .033, paint, shell);
        tube([[side * .69, 1.36, .65], [side * .79, 1.17, 1.02], [side * .88, .99, 1.38]], .047, paint, shell);
        tube([[side * .92, 1.0, .18], [side * .72, 1.40, .18]], .027, outerTrim, shell);
        tube([[side * .93, .96, -1.08], [side * .95, .93, .25], [side * .94, .91, 1.39]], .008, chrome, shell);
        tube([[side * .93, .96, -1.08], [side * .95, .93, .25], [side * .94, .91, 1.39]], .006, outlineMaterial, outlines);
        for (const z of [-1.01, .19, 1.14]) tube([[side * .999, .96, z], [side * 1.001, .70, z - .03], [side * .985, .40, z - .10]], .004, outerTrim, shell);
        tube([[side * .95, .42, -1.0], [side * .96, .39, .0], [side * .95, .4, 1.01]], .025, outerTrim, shell);
        for (const z of [-.21, .80]) roundedBox([.028, .025, .145], [side * 1.003, .91, z], chrome, shell, .01);
        for (const z of wheelZ) {
            const points = [];
            for (let i = 0; i <= 32; i++) {
                const angle = i / 32 * Math.PI;
                points.push([side * .974, .37 + Math.sin(angle) * .421, z + Math.cos(angle) * .421]);
            }
            tube(points, .014, paint, shell);
            tube(points, .005, outlineMaterial, outlines);
        }
        tube([[side * .85, 1.03, -.9], [side * 1.01, 1.03, -.93]], .018, outerTrim, shell);
        sphere([.15, .058, .10], [side * 1.015, 1.055, -.91], paint, shell);
        tube([[side * .99, 1.052, -1.0], [side * 1.13, 1.052, -.94]], .008,
            new THREE.MeshBasicMaterial({ color: 0xbfeaff }), shell);
        tube([[side * .34, .93, -2.19], [side * .42, .99, -1.7], [side * .56, 1.005, -1.12]], .006, paint, shell);
        tube([[side * .79, .75, -2.45], [side * .94, .94, -1.58], [side * .93, .98, -1.07]], .005, outlineMaterial, outlines);
        tube([[side * .85, .79, 2.45], [side * .94, .93, 1.75], [side * .88, .98, 1.35]], .005, outlineMaterial, outlines);
    }

    // Sculpted bumper fascia, slim light housings, LED signatures and recessed air intakes.
    roundedBox([1.65, .31, .15], [0, .54, -2.39], paint, shell, .08);
    roundedBox([1.65, .29, .15], [0, .53, 2.39], paint, shell, .08);
    roundedBox([.83, .21, .025], [0, .64, -2.477], outerTrim, shell, .055);
    roundedBox([1.22, .10, .035], [0, .395, -2.485], outerTrim, shell, .04);
    for (const y of [.60, .665]) box([.71, .009, .02], [0, y, -2.498], chrome, shell);
    for (let i = -5; i <= 5; i++) box([.01, .15, .015], [i * .063, .64, -2.495], dark, shell);
    const lamps = { front: [], rear: [] };
    for (const side of [-1, 1]) {
        roundedBox([.33, .12, .04], [side * .63, .78, -2.40], outerTrim, shell, .035);
        roundedBox([.23, .12, .028], [side * .69, .47, -2.46], outerTrim, shell, .025);
        const front = new THREE.MeshStandardMaterial({ color: 0xc1dce7, emissive: 0x294452, roughness: .15 });
        lamps.front.push(tube([[side * .47, .79, -2.435], [side * .62, .80, -2.442], [side * .78, .83, -2.40]], .013, front, shell));
        const projector = sphere([.044, .028, .013], [side * .66, .755, -2.444], front, shell);
        projector.userData.headlamp = true;
        const rear = new THREE.MeshStandardMaterial({ color: 0x8c1830, emissive: 0x350816, roughness: .2 });
        lamps.rear.push(tube([[side * .37, .83, 2.445], [side * .68, .84, 2.438], [side * .87, .87, 2.32]], .020, rear, shell));
        roundedBox([.25, .055, .08], [side * .61, .37, 2.45], chrome, shell, .015);
    }
    roundedBox([1.27, .07, .05], [0, .34, 2.42], outerTrim, shell, .02);
    roundedBox([.31, .085, .012], [0, .61, 2.483], new THREE.MeshStandardMaterial({ color: 0xcbd7df }), shell, .01);

    // Underbody, driveline and a lower, more realistic cabin/engine silhouette.
    roundedBox([1.4, .08, 3.65], [0, .34, -.03], dark);
    roundedBox([.72, .30, .68], [0, .67, -1.46], alloy, root, .06);
    roundedBox([.79, .12, .70], [0, .88, -1.44], dark, root, .05);
    for (let i = -2; i <= 2; i++) roundedBox([.028, .016, .54], [i * .14, .95, -1.44], alloy, root, .004);
    roundedBox([.40, .22, .57], [.18, .47, -.83], alloy);
    roundedBox([1.21, .31, .075], [0, .64, -2.04], dark);
    for (let i = -8; i <= 8; i++) box([.009, .28, .02], [i * .063, .64, -2.08], alloy);
    roundedBox([.85, .18, .60], [0, .47, 1.38], dark);
    roundedBox([.25, .18, .30], [.58, .74, -1.76], dark);
    tube([[.58, .81, -1.76], [.61, .87, -1.53], [.55, .85, -1.30], [.38, .83, -1.08]], .045, rubber);
    tube([[-.25, .40, -1.2], [-.23, .27, -.35], [.2, .27, 1.3], [.45, .31, 2.35]], .031, alloy);
    roundedBox([.55, .12, .32], [.25, .28, 1.84], alloy);
    for (const side of [-1, 1]) for (const z of [.05, .89]) {
        roundedBox([.47, .13, .48], [side * .43, .62, z], leather, root, .055);
        const seat = roundedBox([.45, .45, .13], [side * .43, .86, z + .18], leather, root, .05);
        seat.rotation.x = -.15;
        roundedBox([.24, .13, .10], [side * .43, 1.14, z + .22], leather, root, .04);
    }
    roundedBox([1.37, .15, .26], [0, .98, -.69], dark);
    roundedBox([.17, .045, .82], [0, .63, -.05], dark);
    const steering = mesh(new THREE.TorusGeometry(.145, .016, 8, 32), outerTrim, [-.43, 1.075, -.45]);
    steering.rotation.x = -.4;
    for (const x of [-.90, .90]) for (const z of wheelZ) {
        const wheel = new THREE.Group(); wheel.position.set(x, .37, z); root.add(wheel); wheels.push(wheel);
        cylinder(.363, .24, [0, 0, 0], rubber, wheel);
        for (const face of [-.115, .115]) {
            const ring = mesh(new THREE.TorusGeometry(.326, .035, 12, 48), rubber, [face, 0, 0], wheel);
            ring.rotation.y = Math.PI / 2;
        }
        cylinder(.274, .246, [0, 0, 0], dark, wheel);
        cylinder(.231, .022, [Math.sign(x) * .095, 0, 0], alloy, wheel);
        const ring = mesh(new THREE.TorusGeometry(.267, .013, 8, 48), chrome, [Math.sign(x) * .132, 0, 0], wheel);
        ring.rotation.y = Math.PI / 2;
        for (let i = 0; i < 5; i++) for (const split of [-.07, .07]) {
            const a = i / 5 * Math.PI * 2 + split;
            const spoke = box([.025, .022, .235], [Math.sign(x) * .137, Math.sin(a) * .14, Math.cos(a) * .14], alloy, wheel);
            spoke.rotation.x = -a;
        }
        cylinder(.048, .028, [Math.sign(x) * .143, 0, 0], chrome, wheel);
        roundedBox([.045, .13, .075], [Math.sign(x) * .105, .015, -.20],
            new THREE.MeshStandardMaterial({ color: 0x9b3843, metalness: .3, roughness: .5 }), wheel, .014);
    }
    for (const z of wheelZ) {
        box([1.8, .065, .10], [0, .37, z], alloy);
        for (const x of [-.78, .78]) tube([[x, .41, z], [x * .91, .78, z]], .037, alloy);
    }

    // These actual component bodies follow fitted targets, including all four optional packs.
    for (const [id, anchor] of Object.entries(locations)) {
        if (["can_bus", "abs_e2e", "tyres", "intake_leak", "cooling", "oil_pump"].includes(id)) continue;
        const group = new THREE.Group(); group.name = "component:" + id; group.position.set(...anchor);
        components.add(group); parts.set(id, group);
        const controller = ["ecm", "tcm", "abs", "bcm", "ipc", "srs"].includes(id);
        const material = new THREE.MeshStandardMaterial({ color: controller ? 0x387b87 : 0x8b9aab,
            metalness: .55, roughness: .35, emissive: controller ? 0x0b252e : 0 });
        if (controller) {
            roundedBox(id === "ipc" ? [.31, .10, .08] : [.22, .11, .22], [0, -.035, 0], material, group, .018);
            for (let i = -2; i <= 2; i++) box([.008, .008, .17], [i * .032, .027, 0], alloy, group);
            roundedBox([.08, .04, .055], [.09, -.04, .13], outerTrim, group, .008);
            modules.set(id, { group, material });
        } else if (id.startsWith("airbag_")) {
            roundedBox([.13, .085, .08], [0, -.02, 0], dark, group, .025);
            roundedBox([.03, .025, .04], [.08, -.035, 0], new THREE.MeshStandardMaterial({ color: 0xe1bd46 }), group, .007);
        } else {
            roundedBox(id.startsWith("tpms_") ? [.065, .045, .035] : [.065, .045, .065], [0, -.015, 0], material, group, .01);
            roundedBox([.035, .025, .028], [.04, -.02, .02], outerTrim, group, .006);
            if (id.startsWith("tpms_")) tube([[0, 0, 0], [0, .07, 0]], .009, chrome, group);
        }
    }

    function diagnostic(enabled) {
        diagnosticMode = enabled;
        for (const [material, opacity] of [[paint, .13], [glass, .045], [outerTrim, .15], [chrome, .25]]) {
            material.transparent = enabled || material === glass;
            material.opacity = enabled ? opacity : material === glass ? .90 : 1;
            material.depthWrite = !enabled; material.needsUpdate = true;
        }
        outlines.visible = enabled; components.visible = enabled;
    }
    function applyState(snapshot) {
        const fittedTargets = new Set((snapshot.targets ?? []).map(target => target.id));
        const faults = new Set((snapshot.faults ?? []).map(fault => fault.targetId));
        for (const [id, group] of parts) group.visible = fittedTargets.has(id);
        const fittedEcus = new Map((snapshot.ecus ?? []).map(ecu => [ecu.name.toLowerCase(), ecu]));
        for (const [id, { group, material }] of modules) {
            group.visible = fittedTargets.has(id) && fittedEcus.has(id);
            const state = fittedEcus.get(id)?.state;
            material.color.set(faults.has(id) || ["fault", "busoff", "nopower"].includes(state) ? 0xca8131 :
                state === "sleep" ? 0x415260 : systemColors[id.toUpperCase()] ?? 0x387b87);
        }
        const v = snapshot.vehicle ?? {};
        lamps.front.forEach((lamp, i) => lamp.material.emissive.set(
            (v.lowBeam || v.highBeam) && !(i === 0 && v.bulbLowLeftOpen && !v.highBeam) ? 0xc8edff : 0x20313a));
        lamps.rear.forEach(lamp => lamp.material.emissive.set(v.braking ? 0xff182b : v.parkLamps ? 0x8b1020 : 0x21040a));
        components.visible = diagnosticMode;
    }
    diagnostic(true);
    applyState({});
    return { root, shell, parts, modules, diagnostic, applyState, wheels };
}
