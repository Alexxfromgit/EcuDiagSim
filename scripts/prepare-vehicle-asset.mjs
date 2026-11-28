// Rebuild the browser's geometry-only asset from the attributed source GLB. No runtime decoder.
import { readFile, writeFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import * as THREE from "../resources/vehicle-viewer/vendor/three.module.min.js";
import { MeshoptDecoder } from "../resources/vehicle-viewer/vendor/meshopt_decoder.module.js";

const source = new URL("../resources/vehicle-viewer/assets/sedan-source.glb", import.meta.url);
const bytes = await readFile(source);
const jsonLength = bytes.readUInt32LE(12);
const gltf = JSON.parse(bytes.subarray(20, 20 + jsonLength).toString());
const binStart = 20 + jsonLength + 8;
const binary = bytes.subarray(binStart);
await MeshoptDecoder.ready;
const views = gltf.bufferViews.map(view => {
    const compression = view.extensions?.EXT_meshopt_compression;
    if (!compression) return binary.subarray(view.byteOffset ?? 0, (view.byteOffset ?? 0) + view.byteLength);
    const result = new Uint8Array(compression.count * compression.byteStride);
    MeshoptDecoder.decodeGltfBuffer(result, compression.count, compression.byteStride,
        binary.subarray(compression.byteOffset, compression.byteOffset + compression.byteLength), compression.mode, compression.filter);
    return result;
});
const components = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4 };
const formats = { 5120: [1, "getInt8", 127], 5121: [1, "getUint8", 255],
    5122: [2, "getInt16", 32767], 5123: [2, "getUint16", 65535], 5125: [4, "getUint32", 4294967295], 5126: [4, "getFloat32", 1] };
function accessor(index) {
    const a = gltf.accessors[index], view = views[a.bufferView];
    const [size, method, normalizer] = formats[a.componentType];
    const n = components[a.type], stride = gltf.bufferViews[a.bufferView].byteStride ?? n * size;
    const data = new DataView(view.buffer, view.byteOffset, view.byteLength);
    const result = [];
    for (let i = 0; i < a.count; i++) for (let c = 0; c < n; c++) {
        let value = data[method]((a.byteOffset ?? 0) + i * stride + c * size, true);
        if (a.normalized) value = Math.max(-1, value / normalizer);
        result.push(value);
    }
    return result;
}
const worlds = new Map();
function visit(index, parent = new THREE.Matrix4()) {
    const node = gltf.nodes[index];
    const local = node.matrix ? new THREE.Matrix4().fromArray(node.matrix) : new THREE.Matrix4().compose(
        new THREE.Vector3(...(node.translation ?? [0, 0, 0])), new THREE.Quaternion(...(node.rotation ?? [0, 0, 0, 1])),
        new THREE.Vector3(...(node.scale ?? [1, 1, 1])));
    const world = parent.clone().multiply(local); worlds.set(index, world);
    for (const child of node.children ?? []) visit(child, world);
}
for (const node of gltf.scenes[gltf.scene ?? 0].nodes) visit(node);
const parts = [], bounds = new THREE.Box3(), p = new THREE.Vector3(), normal = new THREE.Vector3();
for (const [nodeIndex, world] of worlds) {
    const node = gltf.nodes[nodeIndex]; if (node.mesh === undefined) continue;
    const normalMatrix = new THREE.Matrix3().getNormalMatrix(world);
    for (const primitive of gltf.meshes[node.mesh].primitives) {
        const position = accessor(primitive.attributes.POSITION), normals = accessor(primitive.attributes.NORMAL);
        for (let i = 0; i < position.length; i += 3) {
            p.fromArray(position, i).applyMatrix4(world); position.splice(i, 3, -p.x, p.y, -p.z); bounds.expandByPoint(new THREE.Vector3(-p.x, p.y, -p.z));
            normal.fromArray(normals, i).applyMatrix3(normalMatrix).normalize(); normals.splice(i, 3, -normal.x, normal.y, -normal.z);
        }
        const indices = primitive.indices === undefined ? Array.from({ length: position.length / 3 }, (_, i) => i) : accessor(primitive.indices);
        // Keep connected surface islands separate: this preserves the artist's panel gaps and lamp lenses.
        const parents = Array.from({ length: position.length / 3 }, (_, i) => i);
        const find = i => { while (parents[i] !== i) { parents[i] = parents[parents[i]]; i = parents[i]; } return i; };
        const join = (a, b) => { parents[find(a)] = find(b); };
        for (let i = 0; i < indices.length; i += 3) { join(indices[i], indices[i + 1]); join(indices[i], indices[i + 2]); }
        const islands = new Map();
        for (let i = 0; i < indices.length; i += 3) {
            const key = find(indices[i]); if (!islands.has(key)) islands.set(key, []);
            islands.get(key).push(...indices.slice(i, i + 3));
        }
        for (const indices of islands.values()) {
            const compact = [], compactNormals = [], compactIndices = [], remap = new Map();
            for (const i of indices) {
                if (!remap.has(i)) { remap.set(i, compact.length / 3); compact.push(...position.slice(i * 3, i * 3 + 3)); compactNormals.push(...normals.slice(i * 3, i * 3 + 3)); }
                compactIndices.push(remap.get(i));
            }
            parts.push({ node: node.name, material: primitive.material, position: compact, normal: compactNormals, indices: compactIndices });
        }
    }
}
const size = bounds.getSize(new THREE.Vector3()), center = bounds.getCenter(new THREE.Vector3());
const scale = 4.96 / size.z;
const wheelNodes = { WheelFrontL: "fl", WheelFrontR: "fr", WheelRearL: "rl", WheelRearR: "rr" };
const wheels = {};
for (const [index, world] of worlds) if (wheelNodes[gltf.nodes[index].name]) {
    p.setFromMatrixPosition(world);
    wheels[wheelNodes[gltf.nodes[index].name]] = [(-p.x - center.x) * scale, (p.y - bounds.min.y) * scale, (-p.z - center.z) * scale];
}
const chunks = [], manifest = [];
let offset = 0;
const append = array => { const buffer = Buffer.from(array.buffer, array.byteOffset, array.byteLength); chunks.push(buffer); const result = [offset, array.length]; offset += buffer.length; return result; };
const grouped = new Map();

// Proportions. The artist's body is 1.33 m high; a uniform vertical stretch would make the wheel
// arches oval and the flanks slab-sided. Instead only the greenhouse is lifted, with a smooth
// (C1) ramp so reflections stay continuous. Everything below the beltline keeps its exact shape.
const nativeRoof = size.y * scale, lift = .08, liftStart = .80;
const ramp = y => { const t = Math.min(1, Math.max(0, (y - liftStart) / (nativeRoof - liftStart))); return [t * t * (3 - 2 * t), 6 * t * (1 - t) / (nativeRoof - liftStart)]; };
const fitHeight = y => y + lift * ramp(y)[0];
const fitSlope = y => 1 + lift * ramp(y)[1];

// Door shut lines traced from the grooves the artist modelled, in the native side profile:
// [height y, longitudinal z] pairs, z interpolated by height. They keep the fender vent, the
// B-pillar trim and the rear wheel arch on the body, and cut each panel along its real seam.
const shutLines = {
    frontEdge: [[.30, -.95], [.365, -.96], [.48, -.975], [.656, -.99], [.806, -1.0], [.85, -.985], [.90, -.95], [.96, -.89], [1.0, -.85], [1.5, -.85]],
    frontRear: [[.30, .06], [.612, .06], [.70, .065], [.788, .08], [.851, .10], [.912, .125], [1.141, .17], [1.5, .21]],
    rearFront: [[.30, .06], [.612, .06], [.70, .065], [.788, .08], [.851, .10], [.912, .125], [.92, .15], [1.141, .205], [1.5, .25]],
    rearEdge: [[.30, .765], [.35, .78], [.44, .82], [.52, .86], [.60, .90], [.65, .94], [.70, 1.0], [.77, 1.08], [.829, 1.14],
        [.884, 1.20], [.912, 1.215], [.934, 1.23], [.991, 1.29], [1.044, 1.305], [1.079, 1.30], [1.193, 1.21], [1.5, 1.21]]
};
// Door paint ends at the beltline; frames, glass and mirrors above it are attached as islands.
const doorBottom = .335, doorTop = 1.27, beltline = .97;
const lineZ = (line, y) => {
    if (y <= line[0][0]) return line[0][1];
    for (let i = 1; i < line.length; i++) if (y <= line[i][0]) {
        const [y0, z0] = line[i - 1], [y1, z1] = line[i];
        return z0 + (y - y0) / (y1 - y0) * (z1 - z0);
    }
    return line.at(-1)[1];
};
// Signed distance-like field: negative in front of (or below) the boundary.
const fields = {
    bottom: v => v.point[1] - doorBottom,
    frontEdge: v => v.point[2] - lineZ(shutLines.frontEdge, v.point[1]),
    frontRear: v => v.point[2] - lineZ(shutLines.frontRear, v.point[1]),
    rearFront: v => v.point[2] - lineZ(shutLines.rearFront, v.point[1]),
    rearEdge: v => v.point[2] - lineZ(shutLines.rearEdge, v.point[1])
};
// Split a polygon by a scalar field. Interpolated normals preserve the artist's shading and
// produce closed, clean panel edges rather than triangle-stepped ones.
function split(vertices, field) {
    const below = [], above = [];
    for (let i = 0; i < vertices.length; i++) {
        const a = vertices[i], b = vertices[(i + 1) % vertices.length];
        const fa = field(a), fb = field(b);
        if (fa <= 0) below.push(a); else above.push(a);
        if ((fa <= 0) !== (fb <= 0)) {
            const t = fa / (fa - fb);
            const vertex = { point: a.point.map((v, c) => v + t * (b.point[c] - v)), normal: a.normal.map((v, c) => v + t * (b.normal[c] - v)) };
            below.push(vertex); above.push(vertex);
        }
    }
    return [below, above].map(polygon => polygon.length >= 3 ? polygon : null);
}
// Returns [polygon, region] pieces, region "f", "r" (front/rear door on this side) or "".
function regions(polygon) {
    const result = [];
    const [low, high] = split(polygon, fields.bottom);
    if (low) result.push([low, ""]);
    if (!high) return result;
    const [ahead, behindFront] = split(high, fields.frontEdge);
    if (ahead) result.push([ahead, ""]);
    if (!behindFront) return result;
    const [front, afterFront] = split(behindFront, fields.frontRear);
    if (front) result.push([front, "f"]);
    if (!afterFront) return result;
    const [pillar, afterPillar] = split(afterFront, fields.rearFront);
    if (pillar) result.push([pillar, ""]);
    if (!afterPillar) return result;
    const [rear, behind] = split(afterPillar, fields.rearEdge);
    if (rear) result.push([rear, "r"]);
    if (behind) result.push([behind, ""]);
    return result;
}
const regionAt = (x, y, z) => {
    const v = { point: [x, y, z] };
    if (Math.abs(x) < .55 || fields.bottom(v) <= 0 || y > doorTop || fields.frontEdge(v) <= 0) return "";
    if (fields.frontRear(v) <= 0) return "f";
    if (fields.rearFront(v) > 0 && fields.rearEdge(v) <= 0) return "r";
    return "";
};

for (const part of parts) {
    const box = new THREE.Box3();
    for (let i = 0; i < part.position.length; i += 3) {
        part.position[i] = (part.position[i] - center.x) * scale;
        part.position[i + 1] = (part.position[i + 1] - bounds.min.y) * scale;
        part.position[i + 2] = (part.position[i + 2] - center.z) * scale;
        box.expandByPoint(p.fromArray(part.position, i));
    }
    const mid = box.getCenter(new THREE.Vector3()), extent = box.getSize(new THREE.Vector3());
    const wheel = Object.keys(wheels).find(id => part.node.startsWith(Object.keys(wheelNodes).find(name => wheelNodes[name] === id)));
    // Remove the decorative hood badge and wheel-cap lettering; attribution stays in the asset metadata.
    if (!wheel && [2, 4, 8].includes(part.material) && Math.abs(mid.x) < .10 && mid.z < -1.9 && mid.y > .85 && extent.x < .16) continue;
    const lamp = [5, 6, 7].includes(part.material);
    // Small separate islands (mirrors, handles, window frames, side glass) move as one piece.
    const whole = !wheel && !lamp && extent.z <= 1.45 && extent.y <= .7 && part.indices.length / 3 < 3000;
    const islandDoor = whole ? regionAt(mid.x, mid.y, mid.z) : "";
    for (let i = 0; i < part.indices.length; i += 3) {
        let triangle = part.indices.slice(i, i + 3);
        const centroid = [0, 0, 0], averageNormal = [0, 0, 0];
        for (const index of triangle) for (let c = 0; c < 3; c++) {
            centroid[c] += part.position[index * 3 + c] / 3;
            averageNormal[c] += part.normal[index * 3 + c] / 3;
        }
        // A handful of source triangles wind against their normals; they would vanish when single-sided.
        const [a, b, c] = triangle.map(index => p.fromArray(part.position, index * 3).clone());
        if (b.clone().sub(a).cross(c.clone().sub(a)).dot(new THREE.Vector3(...averageNormal)) < 0) triangle = [triangle[0], triangle[2], triangle[1]];
        const side = centroid[0] < 0 ? "l" : "r";
        const polygon = triangle.map(index => ({ point: part.position.slice(index * 3, index * 3 + 3), normal: part.normal.slice(index * 3, index * 3 + 3) }));
        let pieces = [[polygon, islandDoor]];
        if (!wheel && !lamp && !whole && Math.abs(centroid[0]) > .45) {
            // Split everything near the flanks so neighbouring triangles share the new edge vertices.
            // Outward-facing skin, plus the upward-facing window sill and the trim strips along it.
            const outward = Math.abs(averageNormal[0]) > .45 || [2, 4, 8].includes(part.material) || centroid[1] > .82 && Math.abs(centroid[0]) > .68;
            const doorSkin = Math.abs(centroid[0]) > .57 && outward && centroid[1] < beltline;
            pieces = regions(polygon).map(([piece, region]) => [piece, doorSkin ? region : ""]);
        }
        const lampSide = lamp ? side : undefined;
        const lampRole = part.material === 5 ? (mid.y > .675 && extent.y < .04 ? "park" : Math.abs(mid.x) > .48 ? "high" : "low") : undefined;
        const lens = part.material === 3 && (mid.z < -2 || mid.z > 2);
        for (const [piece, region] of pieces) {
            const door = region ? region + side : undefined;
            const key = [part.material, wheel ?? "body", door ?? "fixed", lampSide ?? "", lampRole ?? "", lens ? "lens" : ""].join(":");
            if (!grouped.has(key)) grouped.set(key, { node: wheel ? part.node : "Body", material: part.material, wheel, door, lampSide, lampRole, lens,
                positions: [], normals: [], indices: [], remap: new Map(), box: new THREE.Box3() });
            const group = grouped.get(key);
            for (let corner = 1; corner < piece.length - 1; corner++) for (const vertex of [piece[0], piece[corner], piece[corner + 1]]) {
                const point = [...vertex.point], n = [...vertex.normal];
                if (!wheel) { n[1] /= fitSlope(point[1]); point[1] = fitHeight(point[1]); }
                normal.fromArray(n).normalize();
                const vertexKey = [...point, ...normal.toArray()].map(v => Math.round(v * 1000000)).join(",");
                if (!group.remap.has(vertexKey)) {
                    group.remap.set(vertexKey, group.positions.length / 3); group.positions.push(...point); group.normals.push(...normal.toArray());
                    group.box.expandByPoint(p.fromArray(point));
                }
                group.indices.push(group.remap.get(vertexKey));
            }
        }
    }
}
// Hinges sit on the leading edge of each door's outer skin (not its mirror), at mid-door height.
const doors = {};
for (const group of grouped.values()) if (group.door && group.material === 0) {
    let x = 0;
    for (let i = 0; i < group.positions.length; i += 3)
        if (group.positions[i + 2] < group.box.min.z + .06 && group.positions[i + 1] < .9) x = Math.max(x, Math.abs(group.positions[i]));
    doors[group.door] = [group.door.endsWith("l") ? -x : x, .62, group.box.min.z];
}
for (const group of grouped.values()) manifest.push({ node: group.node, material: group.material, wheel: group.wheel,
    door: group.door, lampSide: group.lampSide, bounds: [...group.box.min.toArray(), ...group.box.max.toArray()],
    lampRole: group.lampRole, lens: group.lens,
    position: append(new Float32Array(group.positions)), normal: append(new Float32Array(group.normals)), indices: append(new Uint32Array(group.indices)) });
const height = fitHeight(nativeRoof);
const payload = { author: gltf.asset.extras, sourceSha256: createHash("sha256").update(bytes).digest("hex"),
    dimensions: [size.x * scale, height, 4.96], wheels, doors, materials: gltf.materials,
    parts: manifest, binary: Buffer.concat(chunks).toString("base64") };
await writeFile(new URL("../resources/vehicle-viewer/assets/sedan-mesh.js", import.meta.url),
    "// Gourami - Luxury Saloon by Fishboe/ministephen, CC BY 4.0. See ATTRIBUTION.md. Generated; do not edit.\nexport default " + JSON.stringify(payload) + ";\n");
console.log({ dimensions: payload.dimensions, wheels, doors, parts: manifest.length, bytes: offset });
if (process.argv.includes("--verbose")) for (let i = 0; i < payload.materials.length; i++) {
    const matches = manifest.filter(part => part.material === i && !part.wheel);
    console.log(payload.materials[i].name, matches.map(part => ({ door: part.door, side: part.lampSide, bounds: part.bounds.map(v => +v.toFixed(2)) })));
}
