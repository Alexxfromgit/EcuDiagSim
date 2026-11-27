import * as THREE from "./vendor/three.module.min.js";

// Original generic sedan and schematic assemblies; no downloaded vehicle geometry or textures.
export function createVehicle() {
    const root = new THREE.Group();
    const shell = new THREE.Group();
    root.add(shell);
    const paint = new THREE.MeshPhysicalMaterial({ color: 0x286580, metalness: 0.65, roughness: 0.24, clearcoat: 1 });
    const glass = new THREE.MeshPhysicalMaterial({ color: 0x173747, metalness: 0.35, roughness: 0.16, transparent: true, opacity: 0.82 });
    const metal = new THREE.MeshStandardMaterial({ color: 0x677c89, metalness: 0.7, roughness: 0.35 });
    const dark = new THREE.MeshStandardMaterial({ color: 0x17242e, roughness: 0.7, metalness: 0.25 });
    const rubber = new THREE.MeshStandardMaterial({ color: 0x111720, roughness: 0.88 });
    const trim = new THREE.MeshStandardMaterial({ color: 0x15252f, metalness: 0.6, roughness: 0.3 });
    const edges = [];

    function box(parent, size, position, material, bevel = 0) {
        const geometry = new THREE.BoxGeometry(...size);
        const mesh = new THREE.Mesh(geometry, material);
        mesh.position.set(...position);
        mesh.castShadow = true;
        parent.add(mesh);
        if (bevel) mesh.rotation.y = bevel;
        return mesh;
    }
    function tube(points, radius, material, parent = root) {
        const curve = new THREE.CatmullRomCurve3(points.map(p => new THREE.Vector3(...p)));
        const mesh = new THREE.Mesh(new THREE.TubeGeometry(curve, points.length * 8, radius, 6, false), material);
        parent.add(mesh);
        return mesh;
    }
    function profile(sections, material) {
        // Each cross section: z, half-width, sill height, shoulder height, top width, top height.
        const vertices = [];
        for (const [z, w, bottom, shoulder, topWidth, top] of sections) {
            for (const [x, y] of [[-w * .92, bottom], [-w, shoulder], [-topWidth, top],
                [topWidth, top], [w, shoulder], [w * .92, bottom]]) vertices.push(x, y, z);
        }
        const indices = [];
        for (let s = 0; s < sections.length - 1; s++) {
            for (let p = 0; p < 6; p++) {
                const a = s * 6 + p, b = s * 6 + (p + 1) % 6;
                indices.push(a, a + 6, b, b, a + 6, b + 6);
            }
        }
        for (let i = 1; i < 5; i++) {
            indices.push(0, i, i + 1);
            const end = (sections.length - 1) * 6;
            indices.push(end, end + i + 1, end + i);
        }
        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute("position", new THREE.Float32BufferAttribute(vertices, 3));
        geometry.setIndex(indices);
        geometry.computeVertexNormals();
        const mesh = new THREE.Mesh(geometry, material);
        mesh.castShadow = true;
        shell.add(mesh);
        const line = new THREE.LineSegments(new THREE.EdgesGeometry(geometry, 24),
            new THREE.LineBasicMaterial({ color: 0x60d4e3, transparent: true, opacity: 0.34 }));
        shell.add(line);
        edges.push(line);
        return mesh;
    }
    profile([
        [-2.39, .77, .43, .73, .65, .79], [-2.14, .94, .43, .86, .77, .97],
        [-1.64, .96, .47, .94, .78, 1.02], [-.85, .94, .46, .98, .76, 1.04],
        [.88, .95, .46, .96, .77, 1.02], [1.66, .96, .48, .91, .79, .97],
        [2.17, .90, .46, .83, .76, .92], [2.39, .77, .48, .76, .65, .82]
    ], paint);
    profile([
        [-.94, .77, 1.01, 1.05, .70, 1.09], [-.35, .78, 1.02, 1.15, .65, 1.72],
        [.64, .78, 1.01, 1.14, .65, 1.75], [1.34, .77, 1.00, 1.04, .69, 1.07]
    ], glass);
    // Roof and pillars retain a recognizable silhouette in exterior mode.
    box(shell, [1.34, .055, 1.04], [0, 1.73, .17], paint);
    for (const side of [-1, 1]) {
        tube([[side * .76, 1.04, -.90], [side * .65, 1.73, -.34]], .027, paint, shell);
        tube([[side * .76, 1.03, .25], [side * .65, 1.75, .23]], .032, paint, shell);
        tube([[side * .76, 1.02, 1.29], [side * .65, 1.74, .66]], .042, paint, shell);
        tube([[side * .95, .98, -.86], [side * .95, .96, .92]], .012, metal, shell);
        for (const z of [-.14, .79]) box(shell, [.025, .025, .15], [side * .956, .94, z], metal);
        box(shell, [.24, .10, .15], [side * .94, 1.13, -.67], paint);
    }
    box(shell, [.86, .19, .035], [0, .63, -2.395], trim);
    for (let i = -3; i <= 3; i++) box(shell, [.025, .14, .045], [i * .105, .64, -2.42], metal);

    // Mechanical layout is intentionally schematic, matching the simulator rather than an OEM.
    box(root, [1.42, .11, 3.75], [0, .40, .06], dark);
    box(root, [.80, .42, .69], [0, .75, -1.43], metal);
    box(root, [.87, .10, .76], [0, 1.0, -1.43], trim);
    for (let i = -3; i <= 3; i++) box(root, [.035, .035, .67], [i * .11, 1.07, -1.43], metal);
    box(root, [.47, .27, .63], [.23, .59, -.80], metal);
    box(root, [1.27, .44, .10], [0, .69, -2.02], dark);
    for (let i = -5; i <= 5; i++) box(root, [.016, .39, .035], [i * .10, .69, -2.085], metal);
    box(root, [.82, .23, .63], [0, .56, 1.39], dark);
    tube([[-.25, .48, -1.2], [-.23, .33, -.35], [.2, .33, 1.3], [.45, .35, 2.35]], .038, metal);
    for (const side of [-1, 1]) {
        for (const z of [.13, .97]) {
            box(root, [.47, .17, .49], [side * .43, .75, z], dark);
            const seat = box(root, [.47, .57, .13], [side * .43, 1.0, z + .18], dark);
            seat.rotation.x = -.15;
        }
    }
    box(root, [1.36, .18, .27], [0, 1.09, -.52], dark);
    const steering = new THREE.Mesh(new THREE.TorusGeometry(.16, .018, 8, 24), trim);
    steering.position.set(-.43, 1.21, -.30); steering.rotation.x = -.4; root.add(steering);

    const wheels = [];
    for (const x of [-1.0, 1.0]) {
        for (const z of [-1.48, 1.50]) {
            const wheel = new THREE.Group();
            wheel.position.set(x, .45, z);
            root.add(wheel);
            const tyre = new THREE.Mesh(new THREE.CylinderGeometry(.43, .43, .25, 40), rubber);
            tyre.rotation.z = Math.PI / 2; tyre.castShadow = true; wheel.add(tyre);
            const disc = new THREE.Mesh(new THREE.CylinderGeometry(.295, .295, .265, 32), trim);
            disc.rotation.z = Math.PI / 2; wheel.add(disc);
            const ring = new THREE.Mesh(new THREE.TorusGeometry(.287, .018, 8, 32), metal);
            ring.rotation.y = Math.PI / 2; ring.position.x = Math.sign(x) * .14; wheel.add(ring);
            for (let i = 0; i < 10; i++) {
                const a = i / 10 * Math.PI * 2;
                const spoke = box(wheel, [.025, .036, .255],
                    [Math.sign(x) * .145, Math.sin(a) * .145, Math.cos(a) * .145], metal);
                spoke.rotation.x = -a;
            }
            wheels.push(wheel);
        }
    }
    for (const z of [-1.48, 1.50]) {
        box(root, [1.80, .08, .12], [0, .46, z], metal);
        for (const x of [-.77, .77]) tube([[x, .51, z], [x * .86, .90, z]], .055, metal);
    }
    const modules = new Map();
    for (const [id, position, size] of [
        ["ecm", [-.74, 1.02, -.93], [.27, .14, .30]], ["tcm", [.69, .65, -.67], [.26, .13, .28]],
        ["abs", [.73, 1.00, -1.92], [.25, .16, .29]], ["bcm", [.71, .87, .08], [.24, .16, .31]],
        ["ipc", [-.42, 1.20, -.57], [.42, .15, .14]], ["srs", [0, .57, .16], [.23, .14, .23]]
    ]) {
        const material = new THREE.MeshStandardMaterial({ color: 0x297b87, metalness: .55, roughness: .3, emissive: 0x123a42 });
        modules.set(id, box(root, size, position, material));
    }
    const busMaterial = new THREE.MeshBasicMaterial({ color: 0x31b9be, transparent: true, opacity: .6 });
    const bus = new THREE.Group(); root.add(bus);
    tube([[.1, .62, -1.95], [.1, .62, -.5], [.1, .62, 1.45]], .012, busMaterial, bus);
    for (const module of modules.values()) tube([module.position.toArray(), [.1, .62, module.position.z]], .008, busMaterial, bus);

    const lamps = { front: [], rear: [] };
    for (const side of [-1, 1]) {
        const frontMaterial = new THREE.MeshStandardMaterial({ color: 0xc9e9f2, emissive: 0x405e68 });
        const rearMaterial = new THREE.MeshStandardMaterial({ color: 0x8b1f32, emissive: 0x2c0510 });
        lamps.front.push(box(shell, [.43, .105, .08], [side * .64, .81, -2.30], frontMaterial));
        lamps.rear.push(box(shell, [.43, .095, .065], [side * .64, .83, 2.29], rearMaterial));
    }
    function diagnostic(enabled) {
        paint.transparent = enabled; paint.opacity = enabled ? .10 : 1; paint.depthWrite = !enabled;
        glass.opacity = enabled ? .035 : .82; glass.depthWrite = !enabled;
        paint.needsUpdate = true; glass.needsUpdate = true;
        for (const edge of edges) edge.visible = enabled;
        bus.visible = enabled;
    }
    function applyState(snapshot) {
        const fitted = new Map((snapshot.ecus ?? []).map(ecu => [ecu.name.toLowerCase(), ecu]));
        for (const [id, mesh] of modules) {
            mesh.visible = fitted.has(id);
            const state = fitted.get(id)?.state;
            mesh.material.color.set(["fault", "busoff", "nopower"].includes(state) ? 0xca8131 : state === "sleep" ? 0x415260 : 0x297b87);
        }
        const v = snapshot.vehicle ?? {};
        lamps.front.forEach((lamp, i) => lamp.material.emissive.set(
            (v.lowBeam || v.highBeam) && !(i === 0 && v.bulbLowLeftOpen && !v.highBeam) ? 0xc8edff : 0x20313a));
        lamps.rear.forEach(lamp => lamp.material.emissive.set(v.braking ? 0xff182b : v.parkLamps ? 0x8b1020 : 0x21040a));
    }
    diagnostic(true);
    return { root, diagnostic, applyState, wheels };
}
