import * as THREE from "./vendor/three.module.min.js";

// Procedural cabin, engine bay and drivetrain for the adapted sedan (metres; +Y up, front at -Z,
// driver on the left at -X). The artist's model is an exterior shell only, so everything inside it
// is built here, sized against the measured body: hood underside ≈ 0.80–0.90 m, windscreen base
// z ≈ -1.10, firewall z ≈ -1.05, roof ≈ 1.40, door skins |x| ≈ 0.73–0.87, sills y ≈ 0.30.
// These are generic, illustrative assemblies, not an OEM layout.

export function createKit() {
    function item(geometry, material, position = [0, 0, 0], parent) {
        const mesh = new THREE.Mesh(geometry, material); mesh.position.set(...position);
        mesh.castShadow = true; mesh.receiveShadow = true; parent?.add(mesh); return mesh;
    }
    const box = (size, position, material, parent) => item(new THREE.BoxGeometry(...size), material, position, parent);
    // A box with rounded edges (upholstery, covers, housings).
    function rounded(size, position, material, parent, roundness = .25) {
        const [w, h, d] = size, r = Math.min(w, h, d) * roundness;
        const shape = new THREE.Shape();
        shape.moveTo(-w / 2 + r, -h / 2);
        shape.lineTo(w / 2 - r, -h / 2); shape.quadraticCurveTo(w / 2, -h / 2, w / 2, -h / 2 + r);
        shape.lineTo(w / 2, h / 2 - r); shape.quadraticCurveTo(w / 2, h / 2, w / 2 - r, h / 2);
        shape.lineTo(-w / 2 + r, h / 2); shape.quadraticCurveTo(-w / 2, h / 2, -w / 2, h / 2 - r);
        shape.lineTo(-w / 2, -h / 2 + r); shape.quadraticCurveTo(-w / 2, -h / 2, -w / 2 + r, -h / 2);
        const geometry = new THREE.ExtrudeGeometry(shape, { depth: Math.max(.001, d - 2 * r), bevelEnabled: true,
            bevelSize: r, bevelThickness: r, bevelSegments: 3, steps: 1, curveSegments: 4 });
        geometry.translate(0, 0, -(d - 2 * r) / 2);
        return item(geometry, material, position, parent);
    }
    // A side profile (z, y) extruded across `width`, centred on `x`.
    function profile(points, width, material, parent, x = 0, bevel = .012) {
        const shape = new THREE.Shape(points.map(([z, y]) => new THREE.Vector2(z, y)));
        const geometry = new THREE.ExtrudeGeometry(shape, { depth: width - 2 * bevel, bevelEnabled: bevel > 0,
            bevelSize: bevel, bevelThickness: bevel, bevelSegments: 2, curveSegments: 8 });
        geometry.rotateY(-Math.PI / 2); geometry.translate(x + width / 2 - bevel, 0, 0);
        return item(geometry, material, [0, 0, 0], parent);
    }
    function tube(points, radius, material, parent, { closed = false, radial = 8, segments } = {}) {
        const curve = new THREE.CatmullRomCurve3(points.map(p => new THREE.Vector3(...p)), closed, "centripetal");
        return item(new THREE.TubeGeometry(curve, segments ?? Math.max(16, points.length * 10), radius, radial, closed), material, [0, 0, 0], parent);
    }
    // Cylinder between two points.
    function rod(from, to, radius, material, parent, radiusEnd = radius, segments = 18) {
        const a = new THREE.Vector3(...from), b = new THREE.Vector3(...to);
        const mesh = item(new THREE.CylinderGeometry(radiusEnd, radius, a.distanceTo(b), segments), material,
            a.clone().add(b).multiplyScalar(.5).toArray(), parent);
        mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), b.clone().sub(a).normalize());
        return mesh;
    }
    function blob(size, position, material, parent) {
        const mesh = item(new THREE.SphereGeometry(1, 24, 14), material, position, parent); mesh.scale.set(...size); return mesh;
    }
    return { item, box, rounded, profile, tube, rod, blob };
}

const standard = options => new THREE.MeshStandardMaterial(options);
export function createMaterials() {
    return {
        carpet: standard({ color: 0x17191c, roughness: .98 }),
        dash: standard({ color: 0x16181b, roughness: .82 }),
        dashUpper: standard({ color: 0x1e2024, roughness: .7 }),
        plastic: standard({ color: 0x111316, roughness: .62 }),
        bolster: standard({ color: 0x262220, roughness: .6 }),
        insert: standard({ color: 0x7a4a33, roughness: .55 }), // cognac leather
        doorTrim: standard({ color: 0x1c1e21, roughness: .78, side: THREE.DoubleSide }),
        satin: standard({ color: 0xa7adb3, metalness: .9, roughness: .28 }),
        screen: new THREE.MeshPhysicalMaterial({ color: 0x050709, roughness: .08, clearcoat: 1, emissive: 0x000000 }),
        rubber: standard({ color: 0x0f1012, roughness: .85 }),
        alloy: standard({ color: 0x8d949b, metalness: .65, roughness: .42 }),
        castIron: standard({ color: 0x55595e, metalness: .55, roughness: .6 }),
        engineCover: standard({ color: 0x1b1d21, roughness: .45, metalness: .2 }),
        exhaust: standard({ color: 0x4f4c49, metalness: .7, roughness: .5 }),
        radiator: standard({ color: 0x2e3337, metalness: .5, roughness: .62 }),
        reservoir: standard({ color: 0xaeb4b9, roughness: .55 }),
        coolantCap: standard({ color: 0x2a74c9, roughness: .5 }),
        battery: standard({ color: 0x1c1f23, roughness: .55 }),
        terminalRed: standard({ color: 0xb4232b, roughness: .5 }),
        tank: standard({ color: 0x242a30, roughness: .7, transparent: true, opacity: .45, depthWrite: false }),
        fuel: standard({ color: 0x4d9c9c, transparent: true, opacity: .65, roughness: .3 }),
        spring: standard({ color: 0x2f6f93, metalness: .4, roughness: .45 }),
        airbag: standard({ color: 0xe7e3d8, roughness: .95 }),
        belt: standard({ color: 0x1a1f26, roughness: .8 })
    };
}

// Front seat: pedestal, cushion with bolsters, reclined backrest and a headrest on posts.
function seat(kit, m, parent, x, z) {
    const group = new THREE.Group(); group.position.set(x, 0, z); parent.add(group);
    kit.box([.36, .14, .40], [0, .39, .02], m.plastic, group);
    kit.rounded([.50, .09, .52], [0, .505, 0], m.bolster, group, .3);
    kit.rounded([.30, .05, .44], [0, .565, -.01], m.insert, group, .45);
    for (const side of [-1, 1]) kit.rounded([.10, .08, .48], [side * .195, .575, 0], m.bolster, group, .45);
    const back = new THREE.Group(); back.position.set(0, .54, .25); back.rotation.x = .22; group.add(back);
    kit.rounded([.50, .58, .07], [0, .30, .045], m.bolster, back, .3);
    kit.rounded([.30, .50, .05], [0, .30, -.01], m.insert, back, .45);
    for (const side of [-1, 1]) kit.rounded([.10, .54, .10], [side * .20, .30, 0], m.bolster, back, .45);
    for (const side of [-1, 1]) kit.rod([side * .06, .58, .02], [side * .06, .66, .02], .007, m.satin, back);
    kit.rounded([.26, .15, .09], [0, .72, .02], m.bolster, back, .45);
    return group;
}

export function buildCabin(kit, m, parent) {
    const cabin = new THREE.Group(); cabin.name = "cabin"; parent.add(cabin);
    // Floor, toe board, transmission tunnel and the raised rear seat pan over the fuel tank.
    kit.box([1.46, .02, 1.42], [0, .30, -.15], m.carpet, cabin);
    kit.profile([[-.86, .30], [-1.04, .70], [-1.06, .70], [-.90, .30]], 1.42, m.carpet, cabin, 0, .004);
    kit.profile([[-1.02, .30], [-1.02, .58], [-.55, .53], [.55, .48], [.60, .30]], .34, m.carpet, cabin, 0, .02);
    kit.box([1.30, .02, .74], [0, .43, .92], m.carpet, cabin);
    kit.box([1.30, .14, .02], [0, .37, .55], m.carpet, cabin);
    // Front seats; the driver sits on the left.
    for (const x of [-.40, .40]) seat(kit, m, cabin, x, 0);
    // Rear bench: two contoured outer places and a firmer centre.
    kit.rounded([1.20, .10, .50], [0, .49, .95], m.bolster, cabin, .3);
    for (const x of [-.34, .34]) kit.rounded([.38, .05, .42], [x, .55, .94], m.insert, cabin, .45);
    const rear = new THREE.Group(); rear.position.set(0, .52, 1.18); rear.rotation.x = .30; cabin.add(rear);
    kit.rounded([1.20, .54, .08], [0, .27, .04], m.bolster, rear, .3);
    for (const x of [-.34, .34]) {
        kit.rounded([.38, .46, .05], [x, .26, -.01], m.insert, rear, .45);
        kit.rounded([.24, .12, .08], [x, .60, .03], m.bolster, rear, .45);
    }
    kit.box([1.30, .02, .40], [0, .955, 1.52], m.dash, cabin); // parcel shelf
    // Dashboard: lower moulding, upper pad, binnacle, centre stack and trim inlay.
    kit.profile([[-1.0, .60], [-1.0, .86], [-.82, .90], [-.62, .925], [-.54, .90], [-.53, .82], [-.60, .70], [-.70, .60]], 1.48, m.dash, cabin);
    kit.profile([[-1.02, .86], [-.95, .935], [-.78, .965], [-.62, .965], [-.56, .935], [-.55, .90], [-.82, .90]], 1.46, m.dashUpper, cabin);
    kit.box([1.36, .014, .012], [0, .875, -.535], m.satin, cabin);
    kit.rounded([.40, .09, .16], [-.40, .975, -.70], m.dashUpper, cabin, .4);
    kit.profile([[-.86, .40], [-.86, .86], [-.62, .86], [-.58, .74], [-.50, .56], [-.38, .52], [-.38, .40]], .32, m.dash, cabin);
    for (const x of [-.10, .10, -.62, .62]) {
        const vent = kit.box([x * x > .1 ? .12 : .09, .045, .02], [x, .835, -.545], m.plastic, cabin);
        for (let i = -1; i <= 1; i++) kit.box([vent.geometry.parameters.width * .9, .004, .006], [x, .835 + i * .013, -.534], m.satin, cabin);
    }
    const cluster = kit.box([.32, .11, .012], [-.40, .96, -.635], m.screen, cabin); cluster.rotation.x = -.30;
    const display = kit.box([.30, .16, .014], [0, .985, -.60], m.screen, cabin); display.rotation.x = -.22;
    kit.box([.30, .014, .03], [0, .905, -.61], m.plastic, cabin);
    // Centre console: armrest, selector and the electric parking brake switch.
    kit.profile([[-.52, .52], [-.40, .62], [.18, .62], [.22, .58], [.22, .46], [-.52, .46]], .22, m.dash, cabin);
    kit.rounded([.20, .06, .30], [0, .64, .05], m.bolster, cabin, .4);
    kit.rounded([.05, .07, .07], [0, .66, -.38], m.satin, cabin, .4);
    kit.rounded([.035, .012, .05], [.07, .625, -.30], m.plastic, cabin, .3);
    // Steering wheel (rim, three spokes, airbag hub) and column.
    const steering = new THREE.Group(); steering.name = "steering"; steering.position.set(-.40, .93, -.47); steering.rotation.x = -.35; cabin.add(steering);
    kit.item(new THREE.TorusGeometry(.175, .017, 12, 48), m.plastic, [0, 0, 0], steering);
    kit.item(new THREE.CylinderGeometry(.062, .066, .05, 28), m.plastic, [0, 0, -.015], steering).rotation.x = Math.PI / 2;
    for (const angle of [0, Math.PI, -Math.PI / 2]) {
        const spoke = kit.box([.13, .022, .012], [Math.cos(angle) * .11, Math.sin(angle) * .11, -.01], m.plastic, steering);
        spoke.rotation.z = angle;
    }
    kit.box([.06, .012, .004], [0, -.018, .012], m.satin, steering);
    kit.rod([-.40, .93, -.48], [-.40, .84, -.72], .03, m.plastic, cabin, .045);
    // Pedals for an automatic: brake and accelerator.
    kit.rod([-.44, .62, -.86], [-.44, .45, -.80], .008, m.satin, cabin);
    kit.rounded([.10, .02, .07], [-.44, .43, -.79], m.rubber, cabin, .4).rotation.x = -.9;
    kit.rounded([.05, .02, .14], [-.29, .40, -.83], m.rubber, cabin, .4).rotation.x = -1.0;
    // Interior mirror.
    kit.rod([0, 1.30, -.50], [0, 1.25, -.52], .006, m.plastic, cabin);
    kit.rounded([.22, .06, .02], [0, 1.23, -.52], m.plastic, cabin, .4);
    return { cabin, steering, cluster, display };
}

// Door trim conforming to the inside of each door skin, so an open door shows a finished card.
export function buildDoorCard(kit, m, skin, side) {
    const group = new THREE.Group();
    const rows = [];
    for (let y = .40; y <= .925; y += .035) {
        const near = skin.filter(p => Math.abs(p[1] - y) < .02);
        if (near.length < 4) continue;
        const zs = near.map(p => p[2]);
        const x = Math.min(...near.map(p => Math.abs(p[0]))) - .03;
        rows.push({ y, x, z0: Math.min(...zs) + .03, z1: Math.max(...zs) - .03 });
    }
    if (rows.length < 3) return group;
    const columns = 12, positions = [], indices = [];
    for (const row of rows) for (let c = 0; c <= columns; c++)
        positions.push(side * row.x, row.y, row.z0 + (row.z1 - row.z0) * c / columns);
    for (let r = 0; r < rows.length - 1; r++) for (let c = 0; c < columns; c++) {
        const a = r * (columns + 1) + c, b = a + columns + 1;
        indices.push(a, b, a + 1, a + 1, b, b + 1);
    }
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
    geometry.setIndex(indices); geometry.computeVertexNormals();
    kit.item(geometry, m.doorTrim, [0, 0, 0], group);
    const at = y => rows.reduce((best, row) => Math.abs(row.y - y) < Math.abs(best.y - y) ? row : best);
    const arm = at(.64), top = rows.at(-1), low = at(.48);
    const length = (arm.z1 - arm.z0) * .62, mid = arm.z0 + (arm.z1 - arm.z0) * .55;
    kit.rounded([.06, .04, length], [side * (arm.x - .03), .64, mid], m.bolster, group, .4);
    kit.rounded([.012, .12, length * .8], [side * (at(.76).x - .006), .76, mid], m.insert, group, .4);
    kit.rounded([.02, .025, .09], [side * (at(.72).x - .012), .72, arm.z0 + (arm.z1 - arm.z0) * .3], m.satin, group, .4);
    kit.item(new THREE.CylinderGeometry(.055, .055, .008, 28), m.plastic,
        [side * (low.x - .004), .48, low.z0 + .16], group).rotation.z = Math.PI / 2;
    kit.box([.05, .02, top.z1 - top.z0], [side * (top.x - .015), top.y + .008, (top.z0 + top.z1) / 2], m.dashUpper, group);
    group.userData.sill = { x: side * (top.x - .02), y: top.y + .018 };
    return group;
}

// A helical coil spring around a damper, for the suspension corners.
function strut(kit, m, parent, x, z, bottom, top) {
    kit.rod([x, bottom, z], [x, top, z], .02, m.castIron, parent);
    const points = [];
    for (let i = 0; i <= 90; i++) {
        const t = i / 90, angle = t * Math.PI * 2 * 5.5;
        points.push([x + Math.cos(angle) * .052, bottom + .05 + t * (top - bottom - .12), z + Math.sin(angle) * .052]);
    }
    kit.tube(points, .009, m.spring, parent, { segments: 240, radial: 6 });
    kit.item(new THREE.CylinderGeometry(.075, .075, .03, 24), m.castIron, [x, top, z], parent);
}

// Inline-six engine (intake on the right, exhaust on the left), automatic gearbox, propshaft and
// rear axle, fuel tank, exhaust, radiator and the engine-bay ancillaries.
export function buildDrivetrain(kit, m, parent) {
    const bay = new THREE.Group(); bay.name = "engine-bay"; parent.add(bay);
    const z0 = -1.62;
    kit.rounded([.30, .10, .62], [0, .30, z0], m.alloy, bay, .2); // sump
    kit.rounded([.34, .27, .66], [0, .48, z0], m.alloy, bay, .12); // block
    kit.rounded([.32, .10, .66], [0, .665, z0], m.alloy, bay, .2); // head
    kit.rounded([.29, .07, .60], [0, .745, z0], m.engineCover, bay, .35); // cover
    for (let i = 0; i < 6; i++) kit.rounded([.06, .025, .05], [0, .79, z0 - .25 + i * .1], m.rubber, bay, .35); // coil packs
    // Intake plenum and runners on the right; the throttle body at its rear, fed from the airbox.
    kit.rounded([.10, .12, .60], [.21, .68, z0], m.engineCover, bay, .4);
    for (let i = 0; i < 6; i++) kit.rod([.19, .66, z0 - .25 + i * .1], [.14, .62, z0 - .25 + i * .1], .018, m.engineCover, bay);
    kit.rounded([.24, .14, .30], [.44, .715, -1.86], m.engineCover, bay, .3); // airbox
    kit.tube([[.42, .73, -1.70], [.40, .745, -1.60], [.36, .745, -1.50], [.31, .735, -1.42]], .04, m.rubber, bay);
    const throttleBody = new THREE.Group(); throttleBody.position.set(.27, .735, -1.42); bay.add(throttleBody);
    kit.item(new THREE.CylinderGeometry(.045, .045, .07, 24, 1, true), m.alloy, [0, 0, 0], throttleBody).rotation.z = Math.PI / 2;
    // The butterfly: a disc across the bore whose shaft lies along Z, opened by rotation.z.
    const plateGeometry = new THREE.CylinderGeometry(.04, .04, .004, 24); plateGeometry.rotateZ(Math.PI / 2);
    const throttle = kit.item(plateGeometry, m.satin, [0, 0, 0], throttleBody);
    // Exhaust manifold on the left into the downpipe, catalyst, centre pipe, rear silencer and tips.
    for (let i = 0; i < 3; i++) kit.tube([[-.16, .56, z0 - .22 + i * .2], [-.24, .52, z0 - .18 + i * .2], [-.27, .44, -1.36]], .022, m.exhaust, bay);
    kit.tube([[-.27, .44, -1.36], [-.25, .32, -1.20], [-.20, .23, -1.0], [-.16, .21, -.90]], .03, m.exhaust, bay);
    kit.rod([-.15, .21, -.90], [-.15, .21, -.55], .055, m.exhaust, bay);
    kit.tube([[-.15, .21, -.55], [-.18, .19, -.1], [-.25, .17, .5], [-.25, .17, 1.1], [-.25, .22, 1.55], [-.25, .25, 1.85]], .028, m.exhaust, bay);
    kit.rounded([1.10, .13, .30], [0, .26, 2.0], m.exhaust, bay, .45);
    for (const x of [-.45, .45]) kit.rod([x, .25, 2.12], [x, .25, 2.36], .03, m.exhaust, bay);
    // Front of the engine: pulleys, belt, alternator and water pump; radiator and fan behind the grille.
    const pulleys = [[0, .40, .07], [0, .56, .05], [.24, .47, .035], [-.12, .62, .03]];
    for (const [x, y, r] of pulleys) kit.item(new THREE.CylinderGeometry(r, r, .03, 28), m.castIron, [x, y, -1.97], bay).rotation.x = Math.PI / 2;
    kit.tube([[-.07, .40, -1.985], [-.15, .62, -1.985], [-.06, .60, -1.985], [.24, .51, -1.985], [.27, .46, -1.985], [.07, .40, -1.985], [0, .33, -1.985]], .006, m.rubber, bay, { closed: true, segments: 120, radial: 4 });
    kit.rod([.24, .47, -1.96], [.24, .47, -1.80], .07, m.alloy, bay);
    kit.box([.86, .40, .05], [0, .51, -2.21], m.radiator, bay);
    kit.rounded([.68, .38, .06], [0, .51, -2.15], m.plastic, bay, .2);
    kit.item(new THREE.CylinderGeometry(.16, .16, .02, 7), m.rubber, [0, .51, -2.115], bay).rotation.x = Math.PI / 2;
    kit.tube([[.30, .66, -2.17], [.22, .68, -2.06], [.06, .70, -1.99]], .022, m.rubber, bay);
    kit.tube([[-.30, .36, -2.17], [-.18, .36, -2.04], [-.04, .52, -1.99]], .022, m.rubber, bay);
    kit.rounded([.12, .14, .16], [-.42, .72, -1.94], m.reservoir, bay, .3);
    kit.item(new THREE.CylinderGeometry(.025, .025, .02, 18), m.coolantCap, [-.42, .80, -1.94], bay);
    // Brake servo and master cylinder at the driver's bulkhead.
    kit.item(new THREE.CylinderGeometry(.11, .11, .09, 32), m.plastic, [-.42, .70, -1.15], bay).rotation.x = Math.PI / 2;
    kit.rod([-.42, .72, -1.20], [-.42, .72, -1.36], .028, m.alloy, bay);
    kit.rounded([.08, .05, .07], [-.42, .77, -1.28], m.reservoir, bay, .3);
    // Suspension corners.
    for (const x of [-.54, .54]) strut(kit, m, bay, x, -1.60, .40, .84);
    for (const x of [-.60, .60]) strut(kit, m, bay, x, 1.24, .34, .66);
    // Gearbox: bellhousing, case and sump; propshaft, differential and half-shafts.
    kit.rod([0, .42, -1.29], [0, .41, -1.04], .19, m.alloy, bay, .15, 28);
    kit.rod([0, .41, -1.04], [0, .40, -.38], .14, m.alloy, bay, .09, 28);
    kit.rounded([.22, .05, .38], [0, .265, -.76], m.alloy, bay, .3);
    kit.rod([0, .37, -.38], [0, .33, 1.12], .035, m.castIron, bay);
    kit.rounded([.28, .20, .24], [0, .33, 1.24], m.castIron, bay, .4);
    for (const side of [-1, 1]) kit.rod([side * .13, .33, 1.24], [side * .64, .33, 1.24], .025, m.castIron, bay);
    // Fuel tank under the rear seat pan, with its filler neck; the fuel fill is shown inside it.
    kit.rounded([.92, .14, .46], [0, .29, .82], m.tank, bay, .3).castShadow = false;
    const fuelGeometry = new THREE.BoxGeometry(.84, .11, .40); fuelGeometry.translate(0, .055, 0);
    const fuel = kit.item(fuelGeometry, m.fuel, [0, .235, .82], bay); fuel.castShadow = false;
    kit.tube([[.36, .33, .95], [.55, .46, 1.30], [.72, .66, 1.55], [.79, .77, 1.66]], .025, m.rubber, bay);
    // Battery in the boot, on the right.
    kit.rounded([.18, .18, .30], [.52, .40, 2.02], m.battery, bay, .1);
    kit.item(new THREE.CylinderGeometry(.014, .014, .03, 12), m.terminalRed, [.52, .50, 1.92], bay);
    return { bay, throttle, fuel };
}
