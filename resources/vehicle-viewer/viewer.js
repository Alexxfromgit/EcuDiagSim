import * as THREE from "./vendor/three.module.min.js";
import { OrbitControls } from "./vendor/OrbitControls.js";
import { createVehicle } from "./vehicle-model.js";
import { createVehicleNetwork } from "./vehicle-network.js";
import { targetsFromSnapshot, visibleTargets, shouldAnimateFlow, systemColors, SelectionGesture } from "./vehicle-state.js";

const byId = id => document.getElementById(id);
let bridge;
let viewer;

function fail(message) {
    viewer?.setDisplayActive(false);
    byId("viewer-error").textContent = message;
    byId("viewer-error").hidden = false;
    bridge?.viewerFailed(message);
}

class VehicleViewer {
    constructor() {
        this.stage = byId("stage");
        this.scene = new THREE.Scene();
        this.scene.background = new THREE.Color(0x0c1823);
        this.scene.fog = new THREE.FogExp2(0x0c1823, .04);
        this.camera = new THREE.PerspectiveCamera(34, 1, .05, 80);
        this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true, powerPreference: "low-power" });
        this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 1.75));
        this.renderer.setClearColor(0x000000, 0);
        this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
        this.renderer.toneMappingExposure = 1.25;
        this.renderer.shadowMap.enabled = true;
        this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
        this.stage.prepend(this.renderer.domElement);
        this.renderer.domElement.addEventListener("webglcontextlost", event => {
            event.preventDefault();
            fail("3D graphics became unavailable. Reload the view or use the diagram.");
        });
        this.controls = new OrbitControls(this.camera, this.stage);
        this.controls.enableDamping = false;
        this.controls.enablePan = false;
        this.controls.minDistance = 1.1;
        this.controls.maxDistance = 16;
        this.controls.maxPolarAngle = Math.PI * .49;
        this.controls.addEventListener("change", () => this.invalidate());
        this.controls.addEventListener("start", () => { this.interacting = true; });
        this.controls.addEventListener("end", () => { this.interacting = false; this.invalidate(); });
        this.scene.add(new THREE.HemisphereLight(0xc8eaff, 0x263645, 1.6));
        const key = new THREE.DirectionalLight(0xe4f4ff, 3);
        key.position.set(-3, 7, -4); key.castShadow = true;
        key.shadow.mapSize.set(2048, 2048);
        Object.assign(key.shadow.camera, { left: -4, right: 4, top: 4, bottom: -4, near: .5, far: 20 });
        key.shadow.bias = -.0002;
        key.shadow.radius = 2;
        this.scene.add(key);
        const rim = new THREE.DirectionalLight(0x99c6db, 1.6);
        rim.position.set(4, 2, 3); this.scene.add(rim);
        this.addEnvironment();
        this.vehicle = createVehicle();
        this.scene.add(this.vehicle.root);
        this.network = createVehicleNetwork();
        this.scene.add(this.network.root);
        const floor = new THREE.Mesh(new THREE.PlaneGeometry(200, 200),
            new THREE.MeshStandardMaterial({ color: 0x020406, roughness: .8, metalness: 0, envMapIntensity: 0 }));
        floor.rotation.x = -Math.PI / 2; floor.position.y = .012; floor.receiveShadow = true;
        this.scene.add(floor);
        this.floor = floor;
        const grid = new THREE.GridHelper(12, 24, 0x234e61, 0x163143);
        grid.material.transparent = true; grid.material.opacity = .28;
        this.scene.add(grid);
        this.grid = grid;
        this.raycaster = new THREE.Raycaster();
        this.markers = new Map();
        this.targets = [];
        this.snapshot = {};
        this.selectedId = "";
        this.system = "";
        this.faultsOnly = false;
        this.diagnosticMode = true;
        this.displayActive = true;
        this.wiresEnabled = true;
        this.flowEnabled = true;
        this.gesture = new SelectionGesture();
        this.listen();
        this.resizeObserver = new ResizeObserver(() => this.resize());
        this.resizeObserver.observe(this.stage);
        this.cameraPreset("perspective");
    }

    addEnvironment() {
        const canvas = document.createElement("canvas");
        canvas.width = 512; canvas.height = 256;
        const context = canvas.getContext("2d");
        const gradient = context.createLinearGradient(0, 0, 0, 256);
        gradient.addColorStop(0, "#a9d6ee"); gradient.addColorStop(.5, "#263e51"); gradient.addColorStop(1, "#0c1721");
        context.fillStyle = gradient; context.fillRect(0, 0, 512, 256);
        context.fillStyle = "#e0f6ff"; context.fillRect(45, 45, 65, 130); context.fillRect(320, 55, 110, 70);
        const texture = new THREE.CanvasTexture(canvas);
        texture.mapping = THREE.EquirectangularReflectionMapping;
        texture.colorSpace = THREE.SRGBColorSpace;
        const generator = new THREE.PMREMGenerator(this.renderer);
        this.environment = generator.fromEquirectangular(texture);
        this.scene.environment = this.environment.texture;
        texture.dispose(); generator.dispose();
    }

    listen() {
        this.stage.addEventListener("pointerdown", event => {
            if (event.target.closest(".view-presets")) return;
            this.pointerTarget = event.target.closest("[data-target]")?.dataset.target;
            this.gesture.start(event);
        });
        this.stage.addEventListener("pointermove", event => this.gesture.move(event));
        this.stage.addEventListener("pointercancel", () => this.gesture.cancel());
        this.stage.addEventListener("lostpointercapture", () => this.gesture.cancel());
        this.stage.addEventListener("pointerup", event => {
            if (!this.gesture.finish(event)) return;
            if (this.pointerTarget) return this.select(this.pointerTarget);
            const label = event.target.closest("[data-target]");
            if (label) return this.select(label.dataset.target);
            const bounds = this.stage.getBoundingClientRect();
            const pointer = new THREE.Vector2((event.clientX - bounds.left) / bounds.width * 2 - 1,
                -(event.clientY - bounds.top) / bounds.height * 2 + 1);
            this.raycaster.setFromCamera(pointer, this.camera);
            const hits = this.raycaster.intersectObjects([...this.markers.values()]
                .filter(marker => marker.visible).map(marker => marker.proxy));
            if (hits.length) this.select(hits[0].object.userData.targetId);
        }, true);
        for (const element of this.stage.querySelectorAll(".view-presets")) {
            element.addEventListener("pointerdown", event => event.stopPropagation());
            element.addEventListener("pointerup", event => event.stopPropagation());
        }
        document.querySelectorAll("[data-view]").forEach(button =>
            button.addEventListener("click", () => this.cameraPreset(button.dataset.view)));
        byId("reset").addEventListener("click", () => this.cameraPreset("perspective"));
        byId("focus").addEventListener("click", () => {
            const target = this.targets.find(item => item.id === this.selectedId);
            if (!target?.position) return;
            const position = new THREE.Vector3(...target.position);
            const direction = this.camera.position.clone().sub(this.controls.target).normalize();
            this.controls.target.copy(position);
            this.camera.position.copy(position).addScaledVector(direction, 2.8);
            this.controls.update(); this.invalidate();
        });
        for (const mode of ["diagnostic", "exterior"]) byId(mode).addEventListener("click", () => {
            this.diagnosticMode = mode === "diagnostic";
            this.vehicle.diagnostic(this.diagnosticMode);
            this.grid.visible = this.diagnosticMode;
            byId("diagnostic").setAttribute("aria-pressed", String(this.diagnosticMode));
            byId("exterior").setAttribute("aria-pressed", String(!this.diagnosticMode));
            byId("scene-caption").textContent = this.diagnosticMode ? "Sensor circuits · live CAN activity" : "Modern executive sedan";
            this.filter();
            this.syncNetwork();
            this.invalidate();
        });
        byId("wiring").addEventListener("change", event => { this.wiresEnabled = event.target.checked; this.syncNetwork(); this.invalidate(); });
        byId("data-flow").addEventListener("change", event => { this.flowEnabled = event.target.checked; this.syncNetwork(); this.invalidate(); });
        byId("system").addEventListener("change", event => { this.system = event.target.value; this.filter(); });
        byId("faults-only").addEventListener("change", event => { this.faultsOnly = event.target.checked; this.filter(); });
        byId("target-select").addEventListener("change", event => this.select(event.target.value));
        byId("open-controls").addEventListener("click", () => bridge?.openTarget(this.selectedId));
        byId("diagnose").addEventListener("click", () => {
            const target = this.targets.find(item => item.id === this.selectedId);
            const ecu = target?.controller ?? this.snapshot.ecus?.find(item => item.name === target?.ecu);
            if (ecu?.diagIndex >= 0) bridge?.openEcu(ecu.diagIndex);
        });
        document.addEventListener("visibilitychange", () => this.setDisplayActive(this.displayActive));
    }

    cameraPreset(preset) {
        this.controls.target.set(0, .64, 0);
        const positions = { perspective: [-5.4, 2.9, -5.8], top: [0, 9, .001], front: [0, 1.7, -7.6], rear: [4.9, 2.4, 6.3], side: [-8, 1.8, 0] };
        this.camera.position.set(...(positions[preset] ?? positions.perspective));
        this.controls.update(); this.invalidate();
    }

    resize() {
        const { width, height } = this.stage.getBoundingClientRect();
        if (width < 1 || height < 1) return;
        this.renderer.setSize(width, height, false);
        this.camera.aspect = width / height;
        // Fit the entire car in the narrow overview as well as the expanded workspace.
        this.camera.fov = width < 550 ? 48 : 34;
        this.camera.updateProjectionMatrix(); this.invalidate();
    }

    setState(snapshot) {
        this.snapshot = snapshot;
        this.targets = targetsFromSnapshot(snapshot);
        this.vehicle.applyState(snapshot);
        this.floor.material.roughness = snapshot.vehicle?.surface === 1 ? .25 : .8;
        this.floor.material.color.set(snapshot.vehicle?.surface === 2 ? 0x52616d : snapshot.vehicle?.surface === 3 ? 0x14222d : 0x020406);
        const v = snapshot.vehicle ?? {};
        byId("vehicle-telemetry").textContent = `${Number(v.speed ?? 0).toFixed(0)} km/h · ${Number(v.rpm ?? 0).toFixed(0)} rpm · ${["P", "R", "N", "D"][v.selector ?? 0]} · Fuel ${Number(v.fuel ?? 0).toFixed(0)} L · ${v.parkBrake ? "Parking brake on" : "Parking brake off"} · ${["Dry", "Wet", "Snow", "Ice"][v.surface ?? 0]}`;
        this.network.applyState(snapshot);
        const equipmentNames = { engine_obd: "Engine OBD", tpms: "TPMS", srs: "SRS", esc: "ESC" };
        const fitted = snapshot.fitted ?? [];
        const equipmentKey = fitted.join("|");
        if (equipmentKey !== this.equipmentKey) {
            this.equipmentKey = equipmentKey;
            byId("equipment-strip").replaceChildren(...["Base vehicle", ...fitted.map(id => equipmentNames[id] ?? id)].map(name => {
                const chip = document.createElement("span"); chip.textContent = name; return chip;
            }));
        }
        const systems = (snapshot.ecus ?? []).map(ecu => ecu.name);
        const systemKey = systems.join("|");
        if (this.systemKey !== systemKey) {
            this.systemKey = systemKey;
            if (!systems.includes(this.system)) this.system = "";
            byId("system").replaceChildren(new Option("All systems", ""), ...systems.map(name => new Option(name, name)));
            byId("system").value = this.system;
        }
        byId("live-state").textContent = snapshot.running ? "LIVE" : "PAUSED";
        for (const target of this.targets) {
            if (!target.position) continue;
            if (!this.markers.has(target.id)) this.addMarker(target);
            const marker = this.markers.get(target.id);
            const color = target.faulted ? 0xffb347 : target.state === "sleep" ? 0x6d7f92 : systemColors[target.ecu] ?? 0x61d5c7;
            marker.mesh.material.color.set(color);
            marker.mesh.material.emissive.set(color).multiplyScalar(.4);
            marker.button.className = ["marker", target.controller ? "ecu" : "", target.faulted ? "faulted" : "",
                target.state === "sleep" ? "asleep" : "", target.state === "stored" ? "stored" : "",
                target.id === this.selectedId ? "selected" : ""].join(" ");
        }
        const currentIds = new Set(this.targets.map(target => target.id));
        for (const [id, marker] of this.markers) {
            if (currentIds.has(id)) continue;
            this.scene.remove(marker.mesh, marker.proxy);
            marker.mesh.geometry.dispose(); marker.mesh.material.dispose();
            marker.proxy.geometry.dispose(); marker.proxy.material.dispose();
            marker.button.remove(); this.markers.delete(id);
        }
        if (!currentIds.has(this.selectedId)) this.selectedId = "";
        this.filter(); this.renderInspector(); this.invalidate();
    }

    addMarker(target) {
        const mesh = new THREE.Mesh(new THREE.SphereGeometry(.045, 12, 8),
            new THREE.MeshStandardMaterial({ color: 0x61d5c7, emissive: 0x24584d, roughness: .3 }));
        mesh.position.set(...target.position); this.scene.add(mesh);
        const proxy = new THREE.Mesh(new THREE.SphereGeometry(.14, 8, 6),
            new THREE.MeshBasicMaterial({ colorWrite: false, depthWrite: false }));
        proxy.position.copy(mesh.position); proxy.userData.targetId = target.id; this.scene.add(proxy);
        const button = document.createElement("button");
        button.className = "marker"; button.dataset.target = target.id;
        button.title = target.name; button.setAttribute("aria-label", target.name);
        const dot = document.createElement("span"); dot.className = "marker-dot";
        const label = document.createElement("span"); label.className = "marker-label"; label.textContent = target.label;
        button.append(dot, label);
        button.addEventListener("click", event => { if (event.detail === 0) this.select(target.id); });
        byId("labels").append(button);
        this.markers.set(target.id, { mesh, proxy, button, visible: true });
    }

    filter() {
        const filtered = visibleTargets(this.targets, this.system, this.faultsOnly);
        const ids = new Set(filtered.map(target => target.id));
        for (const [id, marker] of this.markers) {
            marker.visible = ids.has(id) && (this.diagnosticMode || id === this.selectedId);
            marker.mesh.visible = marker.visible; marker.proxy.visible = marker.visible;
            marker.button.hidden = !marker.visible;
        }
        // A selected component that disappears from the current filter must not remain actionable.
        if (this.selectedId && !ids.has(this.selectedId)) this.selectedId = "";
        for (const [id, marker] of this.markers) marker.button.classList.toggle("selected", id === this.selectedId);
        const listKey = filtered.map(target => target.id + ":" + target.faulted).join("|");
        if (listKey !== this.listKey) {
            this.listKey = listKey;
            const groups = new Map();
            for (const target of filtered) {
                if (!groups.has(target.kind)) {
                    const group = document.createElement("optgroup"); group.label = target.kind;
                    groups.set(target.kind, group);
                }
                groups.get(target.kind).append(new Option((target.faulted ? "● " : "") + target.name, target.id));
            }
            byId("target-select").replaceChildren(new Option(filtered.length ? "Choose a component…" : "No matching components", ""), ...groups.values());
        }
        byId("target-select").value = this.selectedId;
        byId("target-count").textContent = String(filtered.length);
        this.renderInspector(); this.invalidate();
        this.syncNetwork();
    }

    select(id) {
        if (id && !visibleTargets(this.targets, this.system, this.faultsOnly).some(target => target.id === id)) return;
        this.selectedId = id;
        byId("target-select").value = id;
        for (const [markerId, marker] of this.markers) marker.button.classList.toggle("selected", markerId === id);
        if (!this.diagnosticMode) this.filter();
        this.renderInspector(); this.invalidate();
        this.syncNetwork();
    }

    renderInspector() {
        const target = this.targets.find(item => item.id === this.selectedId);
        byId("empty-selection").hidden = !!target;
        byId("selection").hidden = !target;
        if (!target) return;
        byId("selected-kind").textContent = target.kind;
        byId("selected-name").textContent = target.name;
        byId("selected-description").textContent = target.controller?.description ??
            (target.kind === "Network" ? "Logical network target" : target.position ? "Illustrative position on the concept vehicle" : "Available through the component list");
        byId("selected-status").textContent = target.faulted ? "Fault active" : target.controller?.state ?? "Healthy";
        byId("selected-status").classList.toggle("faulted", target.faulted);
        const rows = [["Controller", target.ecu || "Vehicle network"]];
        const connection = [...this.network.records.values()].find(record => record.edge.targetId === target.id);
        if (connection) rows.push(["Connection", connection.edge.kind === "radio" ? "TPMS radio" :
            connection.edge.kind === "can" ? "CAN bus" : "Wired circuit"]);
        const sensor = target.sensor;
        const number = (value, unit) => Number.isFinite(value) ? `${value.toFixed(2)} ${unit ?? ""}`.trim() : "—";
        if (sensor) rows.push(["True value", number(sensor.trueValue, sensor.unit)], ["ECU measures", number(sensor.value, sensor.unit)],
            ["Signal", sensor.digital ? "Digital" : number(sensor.voltage, "V")], ["Circuit", sensor.circuit]);
        const circuitLabel = { ok: "OK", open: "Open circuit", short_gnd: "Short to ground" }[target.componentCircuit];
        if (circuitLabel) rows.push(["Circuit", circuitLabel]);
        if (target.controller) rows.push(["Stored DTCs", String(target.controller.dtcCount)], ["Confirmed DTCs", String(target.controller.confirmed)]);
        byId("selected-values").replaceChildren(...rows.map(([name, value]) => {
            const row = document.createElement("div"); row.className = "value-row";
            const term = document.createElement("dt"); term.textContent = name;
            const description = document.createElement("dd"); description.textContent = value;
            row.append(term, description); return row;
        }));
        const faults = (this.snapshot.faults ?? []).filter(fault => fault.targetId === target.id);
        byId("selected-fault").textContent = sensor?.fault || faults.map(fault => fault.typeLabel || fault.type || "Fault active").join(" · ") || circuitLabel || "Fault active";
        byId("selected-fault").hidden = !target.faulted;
        byId("focus").disabled = !target.position;
        const ecu = target.controller ?? this.snapshot.ecus?.find(item => item.name === target.ecu);
        byId("diagnose").hidden = !(ecu?.diagIndex >= 0);
        byId("diagnose").disabled = !bridge;
        byId("open-controls").disabled = !bridge;
    }

    syncNetwork() {
        const visibleIds = new Set(visibleTargets(this.targets, this.system, this.faultsOnly).map(target => target.id));
        // A network target refers to controller branches rather than owning a physical cable.
        if (visibleIds.has("can_bus")) for (const target of this.targets) if (target.controller) visibleIds.add(target.id);
        if (visibleIds.has("abs_e2e")) visibleIds.add("abs");
        this.network.setView({ visibleIds, system: this.system, selectedId: this.selectedId,
            diagnostic: this.diagnosticMode, wires: this.wiresEnabled });
        this.network.pulses.visible = this.flowEnabled;
        const summary = this.network.summary();
        byId("connection-summary").textContent = `${summary.wired} wired · ${summary.can} CAN · ${summary.radio} radio`;
        byId("flow-state").textContent = !this.diagnosticMode || !this.wiresEnabled ? "Wiring hidden" :
            !this.flowEnabled ? "Flow off" : this.snapshot.reducedMotion ? "Reduced motion" :
                !this.snapshot.running ? "Flow paused" : this.network.hasActiveFlow() ? "Signal flow" : "No active links";
    }

    setDisplayActive(active) {
        this.displayActive = active;
        this.lastFrameTime = null;
        if ((!active || document.hidden) && this.frame) {
            cancelAnimationFrame(this.frame); this.frame = null;
        }
        if (active) this.invalidate();
    }

    invalidate() {
        if (this.frame || !this.displayActive || document.hidden) return;
        this.frame = requestAnimationFrame(time => {
            this.frame = null;
            if (!this.displayActive || document.hidden) return;
            const flowMotion = shouldAnimateFlow(this.snapshot, this.displayActive, this.flowEnabled) && this.network.hasActiveFlow();
            const wheelMotion = this.vehicle.hasMotion(this.snapshot);
            const animate = flowMotion || wheelMotion;
            // Cap decorative signal flow at 30 fps. Camera movement still renders on demand.
            if (!animate || this.interacting || !this.lastFrameTime || time - this.lastFrameTime >= 32) {
                if (animate) {
                    const elapsed = this.lastFrameTime ? Math.min(.08, (time - this.lastFrameTime) / 1000) : 0;
                    const simulationStep = elapsed * Math.max(.1, Math.min(3, this.snapshot.speedFactor ?? 1));
                    if (flowMotion) this.network.advance(simulationStep);
                    if (wheelMotion) this.vehicle.advance(simulationStep);
                }
                this.lastFrameTime = animate ? time : null;
                this.renderer.render(this.scene, this.camera);
                const bounds = this.stage.getBoundingClientRect();
                const occupied = [];
                const markers = [...this.markers.values()].sort((a, b) => {
                    const priority = marker => marker.button.classList.contains("selected") ? 3 :
                        marker.button.classList.contains("faulted") ? 2 : marker.button.classList.contains("ecu") ? 1 : 0;
                    return priority(b) - priority(a);
                });
                for (const marker of markers) {
                    if (!marker.visible) continue;
                    const position = marker.mesh.position.clone().project(this.camera);
                    marker.button.hidden = position.z < -1 || position.z > 1 || Math.abs(position.x) > 1 || Math.abs(position.y) > 1;
                    marker.button.style.left = `${(position.x + 1) * bounds.width / 2}px`;
                    marker.button.style.top = `${(1 - position.y) * bounds.height / 2}px`;
                    const x = (position.x + 1) * bounds.width / 2, y = (1 - position.y) * bounds.height / 2;
                    const labelWidth = marker.button.querySelector(".marker-label").textContent.length * 6 + 26;
                    const collides = occupied.some(box => x < box.x + box.width && x + labelWidth > box.x && Math.abs(y - box.y) < 24);
                    const label = marker.button.classList.contains("selected") || marker.button.classList.contains("faulted") || marker.button.classList.contains("ecu");
                    marker.button.classList.toggle("label-hidden", collides && !marker.button.classList.contains("selected"));
                    if (label && !collides && !marker.button.hidden) occupied.push({ x, y, width: labelWidth });
                }
            }
            if (animate) this.invalidate();
        });
    }
}

function connectHost() {
    if (!window.qt?.webChannelTransport) return;
    const script = document.createElement("script");
    script.src = "qrc:///qtwebchannel/qwebchannel.js";
    script.onerror = () => fail("The vehicle connection could not be established.");
    script.onload = () => new window.QWebChannel(window.qt.webChannelTransport, channel => {
        bridge = channel.objects.vehicleBridge;
        const apply = () => {
            try { viewer.setState(JSON.parse(bridge.stateJson)); }
            catch (error) { fail(`The vehicle state could not be displayed: ${error.message}`); }
        };
        bridge.stateJsonChanged.connect(apply);
        bridge.displayActiveChanged.connect(() => viewer.setDisplayActive(bridge.displayActive));
        viewer.setDisplayActive(bridge.displayActive);
        apply(); bridge.viewerReady();
    });
    document.head.append(script);
}

try {
    viewer = new VehicleViewer();
    // Also allows the offline viewer to be exercised in a normal browser using recorded snapshots.
    window.vehicleViewer = {
        setState: snapshot => viewer.setState(snapshot),
        setDisplayActive: active => viewer.setDisplayActive(active),
        getRenderState: () => ({ frames: viewer.renderer.info.render.frame, pulses: viewer.network.pulses.count,
            firstPulse: Array.from(viewer.network.pulses.instanceMatrix.array.slice(0, 16)),
            connections: Object.fromEntries([...viewer.network.records].map(([id, record]) => [id, { active: record.active, visible: record.visible }])),
            controls: { doors: [...viewer.vehicle.doorPivots.values()].map(door => door.rotation.y),
                steering: viewer.vehicle.wheels.map(wheel => wheel.rotation.y), rolling: viewer.vehicle.wheelSpinners.map(wheel => wheel.rotation.x),
                belt: viewer.vehicle.belt.visible, bags: viewer.vehicle.bags.map(bag => bag.visible), fuel: viewer.vehicle.fuel.scale.y,
                throttle: viewer.vehicle.throttle.rotation.z,
                lights: Object.fromEntries([...viewer.vehicle.lights].map(([key, meshes]) => [key, meshes.some(mesh => mesh.userData.on)])) } })
    };
    connectHost();
    document.body.dataset.viewerReady = "true";
} catch (error) {
    fail(`The 3D view could not start: ${error.message}`);
}
