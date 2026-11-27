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
// Cut across triangles at the door jambs instead of moving triangles by their centroid.
// Interpolated normals preserve the artist's shading and leave straight, closed panel edges.
function clip(vertices, z, less) {
    const output = [];
    for (let i = 0; i < vertices.length; i++) {
        const a = vertices[i], b = vertices[(i + 1) % vertices.length];
        const insideA = less ? a.point[2] <= z : a.point[2] >= z;
        const insideB = less ? b.point[2] <= z : b.point[2] >= z;
        if (insideA) output.push(a);
        if (insideA !== insideB) {
            const t = (z - a.point[2]) / (b.point[2] - a.point[2]);
            output.push({ point: a.point.map((v, c) => v + t * (b.point[c] - v)),
                normal: a.normal.map((v, c) => v + t * (b.normal[c] - v)) });
        }
    }
    return output;
}
for (const part of parts) {
    const box = new THREE.Box3();
    for (let i = 0; i < part.position.length; i += 3) {
        part.position[i] = (part.position[i] - center.x) * scale;
        part.position[i + 1] = (part.position[i + 1] - bounds.min.y) * scale;
        part.position[i + 2] = (part.position[i + 2] - center.z) * scale;
        box.expandByPoint(p.fromArray(part.position, i));
    }
    const mid = box.getCenter(new THREE.Vector3());
    const wheel = Object.keys(wheels).find(id => part.node.startsWith(Object.keys(wheelNodes).find(name => wheelNodes[name] === id)));
    // Remove the decorative hood badge and wheel-cap lettering; attribution stays in the asset metadata.
    if (!wheel && [2, 4, 8].includes(part.material) && Math.abs(mid.x) < .10 && mid.z < -1.9 && mid.y > .85 && box.getSize(p).x < .16) continue;
    for (let i = 0; i < part.indices.length; i += 3) {
        const triangle = part.indices.slice(i, i + 3);
        const centroid = [0, 0, 0], averageNormal = [0, 0, 0];
        for (const index of triangle) for (let c = 0; c < 3; c++) {
            centroid[c] += part.position[index * 3 + c] / 3;
            averageNormal[c] += part.normal[index * 3 + c] / 3;
        }
        const side = centroid[0] < 0 ? "l" : "r";
        let polygons = [triangle.map(index => ({ point: part.position.slice(index * 3, index * 3 + 3), normal: part.normal.slice(index * 3, index * 3 + 3) }))];
        const doorSurface = !wheel && Math.abs(centroid[0]) > .57 && Math.abs(averageNormal[0]) > .45 && centroid[1] > .36 && centroid[1] < 1.27;
        if (doorSurface) for (const cut of [-.98, .14, .20, 1.21])
            polygons = polygons.flatMap(vertices => [clip(vertices, cut, true), clip(vertices, cut, false)]).filter(vertices => vertices.length >= 3);
        const lampSide = [5, 6, 7].includes(part.material) ? side : undefined;
        const lampRole = part.material === 5 ? (mid.y > .675 && box.getSize(p).y < .04 ? "park" : Math.abs(mid.x) > .48 ? "high" : "low") : undefined;
        const lens = part.material === 3 && (mid.z < -2 || mid.z > 2);
        for (const polygon of polygons) {
            const z = polygon.reduce((sum, vertex) => sum + vertex.point[2], 0) / polygon.length;
            const door = doorSurface ? z > -.98 && z < .14 ? "f" + side : z > .20 && z < 1.21 ? "r" + side : undefined : undefined;
            const key = [part.material, wheel ?? "body", door ?? "fixed", lampSide ?? "", lampRole ?? "", lens ? "lens" : ""].join(":");
            if (!grouped.has(key)) grouped.set(key, { node: wheel ? part.node : "Body", material: part.material, wheel, door, lampSide, lampRole, lens,
                positions: [], normals: [], indices: [], remap: new Map(), box: new THREE.Box3() });
            const group = grouped.get(key);
            for (let corner = 1; corner < polygon.length - 1; corner++) for (const vertex of [polygon[0], polygon[corner], polygon[corner + 1]]) {
                const point = [...vertex.point], n = [...vertex.normal];
                if (!wheel) { point[1] = .329 + (point[1] - .329) * ((1.46 - .329) / (size.y * scale - .329)); n[1] /= ((1.46 - .329) / (size.y * scale - .329)); }
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
for (const group of grouped.values()) manifest.push({ node: group.node, material: group.material, wheel: group.wheel,
    door: group.door, lampSide: group.lampSide, bounds: [...group.box.min.toArray(), ...group.box.max.toArray()],
    lampRole: group.lampRole, lens: group.lens,
    position: append(new Float32Array(group.positions)), normal: append(new Float32Array(group.normals)), indices: append(new Uint32Array(group.indices)) });
const payload = { author: gltf.asset.extras, sourceSha256: createHash("sha256").update(bytes).digest("hex"),
    dimensions: [size.x * scale, 1.46, 4.96], wheels, materials: gltf.materials,
    parts: manifest, binary: Buffer.concat(chunks).toString("base64") };
await writeFile(new URL("../resources/vehicle-viewer/assets/sedan-mesh.js", import.meta.url),
    "// Gourami - Luxury Saloon by Fishboe/ministephen, CC BY 4.0. See ATTRIBUTION.md. Generated; do not edit.\nexport default " + JSON.stringify(payload) + ";\n");
console.log({ dimensions: payload.dimensions, wheels, parts: manifest.length, bytes: offset });
for (let i = 0; i < payload.materials.length; i++) {
    const matches = manifest.filter(part => part.material === i && !part.wheel);
    console.log(payload.materials[i].name, matches.length, matches.map(part => ({door:part.door,side:part.lampSide,bounds:part.bounds.map(v => +v.toFixed(2))})));
}
