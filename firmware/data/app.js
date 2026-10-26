/* ==========================================================
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 TechruWolf & Gemini
 * ========================================================== */

// ============================================================================
// 1. GLOBAL STATE & WEBSOCKET CONNECTION
// ============================================================================
let isStreaming = false;
const ws = new WebSocket(`ws://${window.location.host}/ws`);

const statusEl = document.getElementById('status');
const progressBar = document.getElementById('progressBar');
const progressText = document.getElementById('progressText');
const lineInfo = document.getElementById('lineInfo');
const streamInfo = document.getElementById('streamInfo');
const bufferWarning = document.getElementById('bufferWarning');

let waitingForAck = false;
let transmissionDelay = 0;
let MaxTransmissionDelay = 5000;
let hardwareRegistry = [];

const DEVICE_AXIS = 0;
const DEVICE_PWM_SENSOR = 1;
const DEVICE_GPIO = 2;
const DEVICE_PIPELINE = 3;

const isAxisDeviceJS = (type) => (type === DEVICE_AXIS);
const isHomableAxisJS = (ent) => {
    if (ent.type !== DEVICE_AXIS) return false;
    const minPin = (ent.pins && ent.pins.length > 3) ? ent.pins[3] : -1;
    return minPin !== -1;
};

const getSensorMeta = (sensorType) => {
    switch (sensorType) {
        case 1: return { name: "Raw Value", unit: "V" };
        case 2: return { name: "Temperature", unit: "°C" };
        case 3: return { name: "RPM", unit: "RPM" };
        case 4: return { name: "Velocity", unit: "mm/s" };
        case 5: return { name: "Acceleration", unit: "mm/s²" };
        case 6: return { name: "Frequency", unit: "Hz" };
        default: return { name: "Sensor", unit: "" };
    }
};

// --- THEME & BOOT SCREEN UTILS ---
function setTheme(themeName, btn) {
    document.documentElement.setAttribute('data-theme', themeName);
    document.querySelectorAll('.theme-btn').forEach(b => b.classList.remove('active-theme'));
    if (btn) btn.classList.add('active-theme');
}

let bootProgress = 0;
function addBootLog(msg) {
    const logContainer = document.getElementById('bootLog');
    if (!logContainer) return;
    const timeTag = (performance.now() / 1000).toFixed(2);
    logContainer.innerHTML += `[${timeTag}s] ${msg}<br>`;
    logContainer.scrollTop = logContainer.scrollHeight;
}

function updateBootProgress(stepVal, statusText) {
    bootProgress = Math.min(100, bootProgress + stepVal);
    const bar = document.getElementById('bootProgressBar');
    if (bar) bar.style.width = bootProgress + '%';
    const txt = document.getElementById('bootStatusText');
    if (txt) txt.innerText = statusText;

    if (bootProgress >= 100) {
        if (txt) txt.innerText = "System initialization complete. Ready.";
        const enterBtn = document.getElementById('enterAppBtn');
        if (enterBtn) enterBtn.style.display = 'inline-block';
    }
}

function dismissBootScreen() {
    const screen = document.getElementById('bootScreen');
    if (screen) {
        screen.style.opacity = '0';
        setTimeout(() => { screen.style.display = 'none'; }, 600);
    }
}

document.addEventListener("DOMContentLoaded", () => {
    addBootLog("Initializing  web client environment...");
    updateBootProgress(25, "Loading Three.js graphics context...");
    setTimeout(() => {
        addBootLog("Three.js engine ready.");
        updateBootProgress(25, "Establishing WebSocket connection...");
    }, 400);
});

ws.onopen = () => {
    statusEl.innerHTML = `Connected! <span id="pingValue" style="margin-left: 10px; font-weight: normal; font-size: 11px; color: var(--peach);">Ping: -- ms</span>`;
    statusEl.style.color = "var(--green)";

    addBootLog("WebSocket connection established successfully.");
    updateBootProgress(30, "Requesting hardware registry from MCU...");

    pingStartTime = performance.now();
    ws.send("GFG_JSON");

    setTimeout(() => {
        hardwareRegistry.forEach(ent => {
            if (ent.type === DEVICE_GPIO) ws.send(`GPIO,${ent.name}`);
        });
    }, 500);

    setInterval(() => {
        if (ws.readyState === WebSocket.OPEN && pingStartTime === 0) {
            pingStartTime = performance.now();
            ws.send("GET_STATS");
        }
    }, 4000);
};

ws.onclose = () => {
    statusEl.innerText = "Disconnected!";
    statusEl.style.color = "var(--red)";
    addBootLog("⚠️ WebSocket disconnected.");
};

ws.onmessage = (event) => {
    if (pingStartTime > 0) {
        const latency = performance.now() - pingStartTime;
        pingStartTime = 0;

        const pingEl = document.getElementById('pingValue');
        if (pingEl) {
            const color = latency < 50 ? "var(--green)" : (latency < 150 ? "var(--yellow)" : "var(--red)");
            pingEl.innerHTML = `Ping: <b style="color: ${color}">+${latency.toFixed(2)} ms</b>`;        }
    }
    const msg = event.data;

    if (msg.startsWith("CFG_RESP,")) {
        try {
            hardwareRegistry = JSON.parse(msg.substring(9));
            addBootLog(`Loaded ${hardwareRegistry.length} hardware entities.`);
            updateBootProgress(20, "Building dynamic hardware components...");
            buildDynamicUI();
        } catch (e) {
            console.error("Failed to parse hardware config JSON:", e);
        }
    }
    else if (msg.startsWith("STATUS,")) {
        const parts = msg.split(',');

        if (parts.length >= 4) {
            updateSystemDashboardUI(parseFloat(parts[1]), parseFloat(parts[2]), parseFloat(parts[3]));
        }

        let partIdx = 4;
        const axisPositionsMap = {};

        hardwareRegistry.forEach(ent => {
            if (ent.type === DEVICE_PWM_SENSOR && parts[partIdx]) {
                const val = parseFloat(parts[partIdx]);
                const tempEl = document.getElementById(`temp_${ent.name}`);
                if (tempEl) tempEl.innerText = val.toFixed(1);
                partIdx++;
            }
        });

        hardwareRegistry.forEach(ent => {
            if (isAxisDeviceJS(ent.type) && parts[partIdx]) {
                const val = parseFloat(parts[partIdx]);
                axisPositionsMap[ent.name] = val;
                const posEl = document.getElementById(`pos_${ent.name}`);
                if (posEl) posEl.innerText = val.toFixed(1);
                partIdx++;
            }
        });

        updateVisualizer(axisPositionsMap["X"] || 0, axisPositionsMap["Y"] || 0, axisPositionsMap["Z"] || 0);
    }
    else if (msg === "SCA" || msg === "SAVE_CONFIG_RUNTIME_ACK") {
        waitingForAck = false;
        isStreaming = false;
        transmissionDelay = 0;
        const msgEl = document.getElementById('configStatusMsg');
        if (msgEl) {
            msgEl.style.color = "var(--green)";
            msgEl.innerText = "✅ Saved successfully! Reloading page in 3s...";
        }
        setTimeout(() => location.reload(), 3000);
    }
    else if (["ACK", "HACK", "G_ACK", "EMG_ACK"].includes(msg)) {
        waitingForAck = false;
    }
    else if (msg.startsWith("G_READ,")) {
        const parts = msg.split(',');
        if (parts.length >= 3) {
            updateGpioStateUI(parts[1], parseInt(parts[2]));
        }
    }
    else if (msg.startsWith("{")) {
        try {
            const res = JSON.parse(msg);
            if (res.status === "ACK") {
                waitingForAck = false;
                if (transmissionDelay > 0) transmissionDelay = Math.max(0, transmissionDelay - 2);
                if (transmissionDelay === 0) bufferWarning.innerText = "";
            }
            else if (res.status === "BUF_FULL") {
                waitingForAck = false;
                transmissionDelay = Math.min(MaxTransmissionDelay, transmissionDelay + 15);
                bufferWarning.innerText = `⚠️ Buffer Full! Throttle active (+${transmissionDelay}ms)`;
            } else if (isStreaming && res.status === "ERR_GCODE") {
                waitingForAck = false;
                bufferWarning.innerText = `❌ G-code Syntax Error on MCU!`;
            }

            if (res.head !== undefined && res.tail !== undefined && res.count !== undefined) {
                document.getElementById('bufHeadVal').innerText = res.head;
                document.getElementById('bufTailVal').innerText = res.tail;
                document.getElementById('bufCountText').innerText = `${res.count} / ${res.size}`;

                let percent = (res.count / res.size) * 100;
                let bar = document.getElementById('bufProgressBar');
                bar.style.width = percent + "%";
                bar.style.backgroundColor = percent > 85 ? "var(--red)" : (percent > 50 ? "var(--peach)" : "var(--green)");
            }
        } catch (e) {
            console.error("Failed to parse JSON status message:", e);
        }
    }
};

// --- SYSTEM DASHBOARD UI UPDATER ---
function updateSystemDashboardUI(loopTimeUs, freeHeap, totalHeap) {
    const loopMs = (loopTimeUs / 1000.0).toFixed(2);
    const cpuLoadPercent = Math.min(100, (loopTimeUs / 20000.0) * 100).toFixed(1);

    const cpuText = document.getElementById('cpuLoadText');
    if (cpuText) cpuText.innerText = `${loopMs} ms (${cpuLoadPercent}%)`;
    const cpuBar = document.getElementById('cpuBar');
    if (cpuBar) {
        cpuBar.style.width = `${cpuLoadPercent}%`;
        cpuBar.style.backgroundColor = cpuLoadPercent > 80 ? "var(--red)" : (cpuLoadPercent > 50 ? "var(--peach)" : "var(--green)");
    }

    const freeKb = (freeHeap / 1024.0).toFixed(1);
    const totalKb = (totalHeap / 1024.0).toFixed(1);
    const ramUsagePercent = totalHeap > 0 ? (((totalHeap - freeHeap) / totalHeap) * 100).toFixed(1) : 0;

    const ramText = document.getElementById('ramText');
    if (ramText) ramText.innerText = `${freeKb} KB / ${totalKb} KB (${ramUsagePercent}%)`;
    const ramBar = document.getElementById('ramBar');
    if (ramBar) {
        ramBar.style.width = `${ramUsagePercent}%`;
        ramBar.style.backgroundColor = ramUsagePercent > 85 ? "var(--red)" : "var(--green)";
    }
}


// ============================================================================
// 2. DYNAMIC UI & CONFIGURATOR
// ============================================================================
function syncRegistryState() {
    const configTextArea = document.getElementById('dynamicConfigJson');
    if (configTextArea) {
        configTextArea.value = JSON.stringify(hardwareRegistry, null, 4);
    }
    renderVisualConfigCards();
}

function buildDynamicUI() {
    // 1. Render Axis & Motor Control Jogs
    const jogContainer = document.getElementById('dynamicJogContainer');
    if (jogContainer) {
        let jogHtml = '<h3>Axis & Motor Control</h3>';
        hardwareRegistry.forEach(ent => {
            if (isAxisDeviceJS(ent.type)) {
                const canHome = isHomableAxisJS(ent);
                const homeBtn = canHome ? `<button onclick="homeAxis('${ent.name}')" style="padding: 5px 10px; background: var(--peach); color: var(--bg-color); border: none; border-radius: 4px; cursor: pointer; margin-left: 10px; width:auto;">🏠 Home</button>` : '';
                jogHtml += `<div style="margin: 10px 0; padding: 8px; background: var(--surface-color); border-radius: 6px; border: 1px solid var(--overlay-color);">
                <span style="font-weight: bold; color: var(--blue); width: 80px; display: inline-block;">${ent.name}:</span>
                <button onclick="jogAxisDynamic('${ent.name}', -1)" style="padding: 5px 10px; background: var(--overlay-color); color: var(--text-color); border: none; border-radius: 4px; cursor: pointer; width:auto;">(-) Step</button>
                <button onclick="jogAxisDynamic('${ent.name}', 1)" style="padding: 5px 10px; background: var(--overlay-color); color: var(--text-color); border: none; border-radius: 4px; cursor: pointer; width:auto; margin-left:5px;">(+) Step</button>
                ${homeBtn}
                </div>`;
            }
        });
        jogContainer.innerHTML = jogHtml;
    }

    // 2. Render Sensors & PWM Control
    const PWMContainer = document.getElementById('dynamicPWMContainer');
    if (PWMContainer) {
        let tempHtml = '<span style="font-weight:bold; color:var(--blue); font-size:13px;">🔥 Sensors & PWM Control</span>';
        hardwareRegistry.forEach(ent => {
            if (ent.type === DEVICE_PWM_SENSOR) {
                const hasPWMOut = (ent.pins && ent.pins.length > 0 && ent.pins[0] !== -1);
                const currentTarget = ent.params?.[0] || 0;
                const meta = getSensorMeta((ent.params && ent.params.length > 8) ? ent.params[8] : 1);
                const controlHtml = hasPWMOut ? `<input type="number" id="input_${ent.name}" value="${currentTarget}" step="1" style="width: 70px; padding: 4px; background: var(--bg-color); border: 1px solid var(--overlay-color); color: var(--text-color); border-radius: 4px;"> <button onclick="sendIndividualTemp('${ent.name}')" style="padding: 4px 10px; background: var(--blue); color: var(--bg-color); border: none; border-radius: 4px; width:auto;">Set</button>` : '';

                tempHtml += `<div style="margin: 6px 0; padding: 8px; background: var(--bg-color); border-radius: 6px; border: 1px solid var(--overlay-color); display: flex; align-items: center; justify-content: space-between;">
                <span>${ent.name}: <strong id="temp_${ent.name}" style="color:var(--peach)">0.0</strong> ${meta.unit}</span>
                ${controlHtml}
                </div>`;
            }
        });
        PWMContainer.innerHTML = tempHtml;
    }

    // 3. Render Coordinates View
    const coordsContainer = document.getElementById('dynamicCoordsContainer');
    if (coordsContainer) {
        coordsContainer.innerHTML = "";
        hardwareRegistry.forEach(ent => {
            if (isAxisDeviceJS(ent.type)) {
                coordsContainer.innerHTML += `<div><strong>${ent.name}:</strong> <span id="pos_${ent.name}" style="color:var(--green)">0.0</span> mm</div>`;
            }
        });
    }

    // 4. Render GPIO Controls
    const gpioContainer = document.getElementById('dynamicGPIOContainer');
    if (gpioContainer) {
        let gpioHtml = '<span style="font-weight:bold; color:var(--blue); font-size:13px;">🔌 GPIO Controls</span>';
        let hasGpio = false;
        hardwareRegistry.forEach(ent => {
            if (ent.type === DEVICE_GPIO) {
                hasGpio = true;
                const isOutput = ent.params?.[0] === 1;
                const btn = isOutput ? `<button onclick="toggleGpioState('${ent.name}')" style="width: 70px; margin-top:0; padding:4px; background: var(--overlay-color); color: var(--text-color);" id="gpio_btn_${ent.name}">Toggle</button>` : '';
                gpioHtml += `<div style="display: flex; justify-content: space-between; align-items: center; background: var(--bg-color); padding: 8px; border-radius: 6px; border: 1px solid var(--overlay-color); margin-top:6px;">
                <span>${ent.name} <span id="gpio_status_${ent.name}" style="font-size:11px; color:var(--green);">(READING)</span></span>
                ${btn}
                </div>`;
            }
        });
        if (!hasGpio) gpioHtml += '<div style="font-size:12px; color:#a6adc8; font-style:italic; margin-top:5px;">No GPIO devices configured.</div>';
        gpioContainer.innerHTML = gpioHtml;
    }

    // 5. Render Pipeline Logic Rules
    let pipelineContainer = document.getElementById('dynamicPipelineContainer');
    if (!pipelineContainer) {
        const jogContainer = document.getElementById('dynamicJogContainer');
        if (jogContainer) {
            pipelineContainer = document.createElement('div');
            pipelineContainer.id = 'dynamicPipelineContainer';
            pipelineContainer.style.cssText = "margin-top: 15px;";
            jogContainer.parentNode.insertBefore(pipelineContainer, jogContainer.nextSibling);
        }
    }

    if (pipelineContainer) {
        let pipelineHtml = '<span style="font-weight:bold; color:var(--blue); font-size:13px;">⚡ Automation Pipelines</span>';
        let hasPipeline = false;

        hardwareRegistry.forEach(ent => {
            if (ent.type === DEVICE_PIPELINE) {
                hasPipeline = true;
                const pins = ent.pins || [];
                const params = ent.params || [];
                const isFMA = pins.length >= 4;

                let detailsHtml = '';

                if (isFMA) {
                    const idxA = pins[0];
                    const idxB = pins[1];
                    const idxC = pins[2];
                    const tgtIdx = pins[3];

                    const getSourceDesc = (idx, paramVal, defaultVal) => {
                        if (idx >= 0 && hardwareRegistry[idx]) {
                            return `<span style="color: var(--blue);">Device: ${hardwareRegistry[idx].name}</span>`;
                        } else {
                            const val = (paramVal !== undefined) ? paramVal : defaultVal;
                            return `<span style="color: var(--peach);">Constant: ${val}</span>`;
                        }
                    };

                    const descA = getSourceDesc(idxA, params[0], 0.0);
                    const descB = getSourceDesc(idxB, params[1], 1.0);
                    const descC = getSourceDesc(idxC, params[2], 0.0);
                    const tgtName = (tgtIdx >= 0 && hardwareRegistry[tgtIdx]) ? hardwareRegistry[tgtIdx].name : `Index ${tgtIdx}`;

                    detailsHtml = `
                    <div style="grid-column: span 2; background: var(--bg-color); padding: 6px; border-radius: 4px; margin-bottom: 5px; color: var(--peach); font-family: monospace;">
                    📐 FMA Formula: Target = (A * B) + C
                    </div>
                    <div>A Factor: ${descA}</div>
                    <div>B Factor: ${descB}</div>
                    <div>C Factor: ${descC}</div>
                    <div>Target Device: <strong style="color: var(--green);">${tgtName}</strong></div>
                    `;
                } else {
                    const srcIdx = pins[0] ?? -1;
                    const tgtIdx = pins[1] ?? -1;

                    const srcName = (srcIdx >= 0 && hardwareRegistry[srcIdx]) ? hardwareRegistry[srcIdx].name : `Index ${srcIdx}`;
                    const tgtName = (tgtIdx >= 0 && hardwareRegistry[tgtIdx]) ? hardwareRegistry[tgtIdx].name : `Index ${tgtIdx}`;

                    const minTrig = params[0] !== undefined ? params[0] : 'N/A';
                    const maxTrig = params[1] !== undefined ? params[1] : 'N/A';
                    const valTrue = params[2] !== undefined ? params[2] : 'Direct Source';
                    const valFalse = params[3] !== undefined ? params[3] : 'Ignored/Pass';

                    detailsHtml = `
                    <div style="grid-column: span 2; background: var(--bg-color); padding: 6px; border-radius: 4px; margin-bottom: 5px; color: var(--blue); font-family: monospace;">
                    🔗 Mode: Standard (Source ➔ Target)
                    </div>
                    <div>Source Device: <strong style="color: var(--blue);">${srcName}</strong></div>
                    <div>Target Device: <strong style="color: var(--green);">${tgtName}</strong></div>
                    <div>Min Trigger: <strong>${minTrig}</strong></div>
                    <div>Max Trigger: <strong>${maxTrig}</strong></div>
                    <div>Val on True: <strong style="color: var(--peach);">${valTrue}</strong></div>
                    <div>Val on False: <strong style="color: var(--red);">${valFalse}</strong></div>
                    `;
                }

                pipelineHtml += `<div style="margin: 10px 0; padding: 10px; background: var(--bg-color); border-radius: 6px; border-left: 4px solid ${isFMA ? 'var(--peach)' : 'var(--blue)'}; border: 1px solid var(--overlay-color);">
                <div style="font-weight: bold; color: ${isFMA ? 'var(--peach)' : 'var(--blue)'}; margin-bottom: 5px;">⚡ ${ent.name} <span style="font-size: 11px; opacity: 0.8;">(${isFMA ? 'FMA Calculation' : 'If-Else Rule'})</span></div>
                <div style="font-size: 12px; color: var(--text-color); display: grid; grid-template-columns: 1fr 1fr; gap: 5px;">
                ${detailsHtml}
                </div>
                </div>`;
            }
        });

        if (!hasPipeline) {
            pipelineHtml += '<div style="font-size: 12px; color: #a6adc8; font-style: italic; margin-top:5px;">No pipeline rules configured.</div>';
        }
        pipelineContainer.innerHTML = pipelineHtml;
    }

    syncRegistryState();
}


// ============================================================================
// 3. MANUAL CONTROL
// ============================================================================
function jogAxisDynamic(axisName, direction) {
    const stepDist = parseFloat(document.getElementById('jogStep').value) * direction;
    const stepsArray = hardwareRegistry.filter(ent => isAxisDeviceJS(ent.type)).map(ent => {
        if (ent.name === axisName) {
            const stepsPerUnit = (ent.params && ent.params[0] > 0) ? ent.params[0] : 80;
            return Math.round(stepDist * stepsPerUnit);
        }
        return 0;
    });
    if (ws.readyState === WebSocket.OPEN) ws.send(`50,${stepsArray.join(",")}`);
}

function homeAxis(axisName) {
    if (ws.readyState === WebSocket.OPEN) ws.send(`HOME,${axisName}`);
}

function sendIndividualTemp(entityName) {
    const inputEl = document.getElementById(`input_${entityName}`);
    if (inputEl && ws.readyState === WebSocket.OPEN) ws.send(`SET_PWM,${entityName},${inputEl.value}`);
}

function turnOffAllPWM() {
    hardwareRegistry.forEach(ent => {
        if (ent.type === DEVICE_PWM_SENSOR && ent.pins?.[0] !== -1) {
            const inputEl = document.getElementById(`input_${ent.name}`);
            if (inputEl) inputEl.value = 0;
            if (ws.readyState === WebSocket.OPEN) ws.send(`SET_PWM,${ent.name},0`);
        }
    });
}

function toggleGpioState(entityName) {
    const statusBadge = document.getElementById(`gpio_status_${entityName}`);
    const targetState = (statusBadge && statusBadge.innerText.includes("ON")) ? 0 : 1;
    if (ws.readyState === WebSocket.OPEN) ws.send(`GPIO,${entityName},${targetState}`);
}

function updateGpioStateUI(entityName, state) {
    const badge = document.getElementById(`gpio_status_${entityName}`);
    if (badge) {
        badge.innerText = state ? "ON" : "OFF";
        badge.style.color = state ? "var(--green)" : "var(--red)";
    }
}

function emergencyStop() {
    if (ws.readyState === WebSocket.OPEN) ws.send("EMG_STOP");
    isStreaming = false;
    streamInfo.innerText = "🚨 EMERGENCY STOP SENT!";
    bufferWarning.innerText = "⚠️ System locked!";
}


// ============================================================================
// 4. 3D VISUALIZER (THREE.JS)
// ============================================================================
const container = document.getElementById('canvas-container');
let scene, camera, renderer, nozzleMesh, trailGeometry, trailLine;
let trailPoints = [];

if (container) {
    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x11111b);

    camera = new THREE.PerspectiveCamera(45, container.clientWidth / container.clientHeight, 0.1, 1000);
    camera.position.set(100, 250, 300);
    camera.lookAt(100, 0, 100);

    renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setSize(container.clientWidth, container.clientHeight);
    container.appendChild(renderer.domElement);

    const gridHelper = new THREE.GridHelper(200, 20, 0x89b4fa, 0x45475a);
    gridHelper.position.set(100, 0, 100);
    scene.add(gridHelper);

    nozzleMesh = new THREE.Mesh(
        new THREE.BoxGeometry(15, 10, 15),
                                new THREE.MeshStandardMaterial({ color: 0x89b4fa, roughness: 0.3 })
    );
    scene.add(nozzleMesh);

    trailGeometry = new THREE.BufferGeometry();
    trailLine = new THREE.Line(trailGeometry, new THREE.LineBasicMaterial({ color: 0xf38ba8, linewidth: 2 }));
    scene.add(trailLine);

    window.addEventListener('resize', () => {
        if (!container) return;
        camera.aspect = container.clientWidth / container.clientHeight;
        camera.updateProjectionMatrix();
        renderer.setSize(container.clientWidth, container.clientHeight);
    });

    function animateThree() {
        requestAnimationFrame(animateThree);
        renderer.render(scene, camera);
    }
    animateThree();
}

function updateVisualizer(x, y, z) {
    if (nozzleMesh) {
        nozzleMesh.position.set(x, z, y);
        const currentPos = new THREE.Vector3(x, z, y);
        if (trailPoints.length === 0 || trailPoints[trailPoints.length - 1].distanceTo(currentPos) > 0.5) {
            trailPoints.push(currentPos);
            if (trailPoints.length > 500) trailPoints.shift();
            if (trailGeometry) trailGeometry.setFromPoints(trailPoints);
        }
    }
}


// ============================================================================
// 5. G-CODE PARSER & DYNAMIC STREAMER
// ============================================================================
let isAbsoluteMode = true;
let isAbsoluteExtrusion = true;
let axisPositions = {};
let activeAxisNames = [];
let feedrate = 1200;

async function processAndStreamGCode() {
    const fileInput = document.getElementById('gcodeFile');
    if (!fileInput.files.length) {
        alert("Please select a valid G-code file!");
        return;
    }
    const lines = (await fileInput.files[0].text()).split('\n');

    streamInfo.innerText = "Parsing G-code dynamically based on Hardware Registry...";
    bufferWarning.innerText = "";
    transmissionDelay = 0;
    let commandBatch = [];

    axisPositions = {};
    activeAxisNames = [];
    hardwareRegistry.forEach(ent => {
        if (isAxisDeviceJS(ent.type)) {
            axisPositions[ent.name] = 0.0;
            activeAxisNames.push(ent.name);
        }
    });

    for (let i = 0; i < lines.length; i++) {
        let rawLine = lines[i].split(';')[0];
        rawLine = rawLine.split('(')[0];
        const line = rawLine.trim().toUpperCase();

        if (!line) continue;
        if (line === 'G90') { isAbsoluteMode = true; continue; }
        if (line === 'G91') { isAbsoluteMode = false; continue; }
        if (line === 'M82') { isAbsoluteExtrusion = true; continue; }
        if (line === 'M83') { isAbsoluteExtrusion = false; continue; }

        if (line.startsWith('G0') || line.startsWith('G1')) {
            const cmd = parseGLineDynamic(line);
            if (cmd) commandBatch.push(cmd);
        } else if (line.startsWith('G28')) {
            const parts = line.split(/\s+/);
            let homedAxes = [];
            parts.forEach((p, idx) => {
                if (idx === 0) return;
                const code = p.charAt(0).toUpperCase();
                const matched = hardwareRegistry.find(ent => isAxisDeviceJS(ent.type) && ent.name.toUpperCase().startsWith(code));
                if (matched) {
                    homedAxes.push(matched.name);
                }
            });

            if (homedAxes.length > 0) {
                homedAxes.forEach(axisName => {
                    commandBatch.push(`HOME,${axisName}`);
                    axisPositions[axisName] = 0.0;
                });
            } else {
                commandBatch.push("HOME,ALL");
                Object.keys(axisPositions).forEach(k => axisPositions[k] = 0.0);
            }
        }

        if (i % 50 === 0) {
            const percent = ((i / lines.length) * 50).toFixed(1);
            progressBar.style.width = percent + "%";
            progressText.innerText = `Progress: ${percent}% (Dynamic Parsing)`;
            lineInfo.innerText = `${i} / ${lines.length} lines`;
            await new Promise(r => setTimeout(r, 0));
        }
    }

    streamInfo.innerText = `Successfully parsed ${commandBatch.length} dynamic commands. Starting stream with buffer control...`;
    isStreaming = true;

    for (let i = 0; i < commandBatch.length; i++) {
        if (!isStreaming || ws.readyState !== WebSocket.OPEN) break;

        waitingForAck = true;
        ws.send(commandBatch[i]);

        let waitTime = 0;
        while (waitingForAck && waitTime < 2000) {
            if (!isStreaming) break;
            await new Promise(r => setTimeout(r, 5));
            waitTime += 5;
        }

        if (!isStreaming) break;
        if (waitingForAck) {
            streamInfo.innerText = "Warning: timeout! Stopping stream.";
            break;
        }

        if (transmissionDelay > 0) {
            await new Promise(r => setTimeout(r, transmissionDelay));
        }
    }

    streamInfo.innerText = "G-code file streamed successfully with dynamic configuration!";
    progressBar.style.width = "100%";
}

function parseGLineDynamic(line) {
    const parts = line.trim().split(/\s+/);
    const targetPositions = { ...axisPositions };
    let hasMovement = false;

    parts.forEach((p, idx) => {
        if (idx === 0) return;
        const code = p.charAt(0).toUpperCase();
        const val = parseFloat(p.substring(1));
        if (isNaN(val)) return;

        const matched = hardwareRegistry.find(ent => isAxisDeviceJS(ent.type) && ent.name.toUpperCase().startsWith(code));
        if (matched) {
            const isAbs = (matched.name.startsWith("E")) ? isAbsoluteExtrusion : isAbsoluteMode;
            targetPositions[matched.name] = isAbs ? val : ((targetPositions[matched.name] || 0) + val);
            hasMovement = true;
        } else if (code === 'F') {
            feedrate = val;
        }
    });

    if (!hasMovement) return null;

    let maxDistSq = 0;
    const stepsMap = {};
    let maxStepsInLine = 0;
    let highestMinDelayUs = 5.0;

    hardwareRegistry.forEach(ent => {
        if (isAxisDeviceJS(ent.type)) {
            const name = ent.name;
            const dPos = (targetPositions[name] !== undefined ? targetPositions[name] : axisPositions[name]) - (axisPositions[name] || 0);
            const stepsPerUnit = (ent.params && ent.params[0] > 0) ? ent.params[0] : 80;
            const steps = Math.round(dPos * stepsPerUnit);

            stepsMap[name] = steps;
            maxDistSq += (dPos * dPos);

            const absSteps = Math.abs(steps);
            if (absSteps > maxStepsInLine) maxStepsInLine = absSteps;

            const minDelay = (ent.params && ent.params.length > 2) ? ent.params[2] : 2.0;
            if (minDelay > highestMinDelayUs) highestMinDelayUs = minDelay;
        }
    });

    axisPositions = { ...targetPositions };
    const distance = Math.sqrt(maxDistSq);
    let timeSeconds = distance > 0 ? (distance / (feedrate / 60)) : 0.1;

    const minSafeTime = (maxStepsInLine * highestMinDelayUs) / 1000000.0;
    timeSeconds = Math.max(minSafeTime, Math.min(timeSeconds, 5.0));

    const timeMs = Math.max(1, Math.round(timeSeconds * 1000.0));

    const payload = [timeMs];
    activeAxisNames.forEach(axisName => { payload.push(stepsMap[axisName] || 0); });

    return payload.join(",");
}

// ==========================================
// DETAILED INSTRUCTIONS FOR EACH DEVICE TYPE
// ==========================================
const DEVICE_INSTRUCTIONS = {
    [DEVICE_AXIS]: `
    <b>DEVICE_AXIS (Motion Motor & Endstop Control)</b><br>
    • <b>Pins:</b> [0]: STEP | [1]: DIR | [2]: EN | [3]: ENDSTOP MIN (-1 if virtual/disabled) | [4]: ENDSTOP MAX (-1 if virtual/disabled)<br>
    • <b>Params:</b> [0]: stepsPerUnit | [1]: maxLimit | [2]: minStepDelay | [3]: homingBackoffSteps | [4]: homingPostOffset | [5]: minInitialState | [6]: maxInitialState
    `,
    [DEVICE_PWM_SENSOR]: `
    <b>DEVICE_PWM_SENSOR (Thermal, PID Control & Sensor Monitoring)</b><br>
    • <b>Pins:</b> [0]: OUTPUT PWM Pin (-1 if pure sensor/virtual) | [1]: FEEDBACK ANALOG / SENSOR Pin (-1 if unused)<br>
    • <b>Params:</b> [0]: Setpoint | [1]: Current Value | [2,3,4]: Kp, Ki, Kd | [5,6,7]: NTC (Resistor, Nominal, Beta) | [8]: Sensor Type (1-6) | [9]: usPerRead | [10]: sampleCount
    `,
    [DEVICE_GPIO]: `
    <b>DEVICE_GPIO (Universal GPIO Configuration)</b><br>
    • <b>Pins:</b> [0]: Target MCU Pin Number<br>
    • <b>Params:</b> [0]: Mode (1.0 = Output, 0.0 = Input Pullup) | [1]: Initial State (0=LOW, 1=HIGH) | [2]: Pull Mode (0:Up, 1:Down, 2:Floating) | [3]: Active Level (0:Low, 1:High) | [4]: Debounce (ms)
    `,
    [DEVICE_PIPELINE]: `
    <b>DEVICE_PIPELINE (Logic Interlock & Automation Pipeline)</b><br>
    • <b>IF-ELSE Mode - Pins:</b> [0]: Source Index, [1]: Target Index | <b>Params:</b> [0]: Min Threshold, [1]: Max Threshold, [2]: Val True, [3]: Val False<br>
    • <b>FMA Mode - Pins:</b> [0,1,2]: Source Indices (or -1 for constant), [3]: Target Index | <b>Params:</b> [0,1,2]: Constant A, B, C
    `
};

// ============================================================================
// 6. UI TAB SWITCHING & CONFIG CARDS
// ============================================================================
function switchTab(tabId, btnElement) {
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    document.getElementById(tabId).classList.add('active');
    btnElement.classList.add('active');
}

function renderVisualConfigCards() {
    const container = document.getElementById('visualConfigContainer');
    if (!container) return;

    container.innerHTML = "";
    hardwareRegistry.forEach((ent, index) => {
        const card = document.createElement('div');
        card.style.cssText = "background: var(--bg-color); border: 1px solid var(--overlay-color); border-radius: 10px; padding: 15px; margin-bottom: 15px;";

        // Get instruction corresponding to current device type
        const instructionHTML = DEVICE_INSTRUCTIONS[ent.type] || "<i>No instructions available for this device type.</i>";

        card.innerHTML = `
        <div style="display: grid; grid-template-columns: 2fr 2fr 100px; gap: 10px; align-items: center; margin-bottom: 12px;">
        <div>
        <label style="font-size: 11px; color: #a6adc8; display: block; margin-bottom: 3px;">Device Name</label>
        <input type="text" id="ent_name_${index}" value="${ent.name}" style="width: 100%; padding: 8px; background: var(--surface-color); border: 1px solid var(--overlay-color); color: var(--text-color); border-radius: 6px; box-sizing: border-box;" onchange="updateEntityFromUI(${index})">
        </div>
        <div>
        <label style="font-size: 11px; color: #a6adc8; display: block; margin-bottom: 3px;">Device Type</label>
        <select id="ent_type_${index}" style="width: 100%; padding: 8px; background: var(--surface-color); border: 1px solid var(--overlay-color); color: var(--text-color); border-radius: 6px; box-sizing: border-box;" onchange="updateEntityFromUI(${index})">
        <option value="${DEVICE_AXIS}" ${ent.type === DEVICE_AXIS ? 'selected' : ''}>Axis (Motor)</option>
        <option value="${DEVICE_PWM_SENSOR}" ${ent.type === DEVICE_PWM_SENSOR ? 'selected' : ''}>Sensor / PWM</option>
        <option value="${DEVICE_GPIO}" ${ent.type === DEVICE_GPIO ? 'selected' : ''}>GPIO (Universal)</option>
        <option value="${DEVICE_PIPELINE}" ${ent.type === DEVICE_PIPELINE ? 'selected' : ''}>Pipeline Logic</option>
        </select>
        </div>
        <div style="text-align: right; padding-top: 15px;">
        <button onclick="removeHardwareEntity(${index})" class="btn-danger" style="padding: 8px 12px; width:auto; margin:0;" title="Delete Entity">🗑️</button>
        </div>
        </div>

        <div style="margin-bottom: 10px;">
        <label style="font-size: 11px; color: var(--blue); display: block; margin-bottom: 3px; font-weight: bold;">Pins (Comma separated)</label>
        <input type="text" id="ent_pins_${index}" value="${ent.pins?.join(", ") || ""}" style="width: 100%; padding: 8px; background: var(--surface-color); border: 1px solid var(--overlay-color); color: var(--text-color); border-radius: 6px; box-sizing: border-box; font-family: monospace;" onchange="updateEntityFromUI(${index})">
        </div>

        <div style="margin-bottom: 10px;">
        <label style="font-size: 11px; color: var(--peach); display: block; margin-bottom: 3px; font-weight: bold;">Params (Comma separated)</label>
        <input type="text" id="ent_params_${index}" value="${ent.params?.join(", ") || ""}" style="width: 100%; padding: 8px; background: var(--surface-color); border: 1px solid var(--overlay-color); color: var(--text-color); border-radius: 6px; box-sizing: border-box; font-family: monospace;" onchange="updateEntityFromUI(${index})">
        </div>

        <div id="ent_help_${index}" style="background: rgba(137, 180, 250, 0.05); border-left: 3px solid var(--blue); padding: 8px 12px; border-radius: 4px; font-size: 11px; color: #a6adc8; line-height: 1.4;">
        ${instructionHTML}
        </div>
        `;
        container.appendChild(card);
    });
}

function updateEntityFromUI(index) {
    hardwareRegistry[index] = {
        name: document.getElementById(`ent_name_${index}`).value,
        type: parseInt(document.getElementById(`ent_type_${index}`).value),
        pins: document.getElementById(`ent_pins_${index}`).value.split(',').map(v => parseInt(v.trim())).filter(v => !isNaN(v)),
        params: document.getElementById(`ent_params_${index}`).value.split(',').map(v => parseFloat(v.trim())).filter(v => !isNaN(v))
    };

    // Refresh cards to update the help box instantly when device type changes
    renderVisualConfigCards();
    syncRegistryState();
}

function addNewHardwareEntity() {
    hardwareRegistry.push({ name: "NEW_DEVICE", type: DEVICE_PWM_SENSOR, pins: [-1], params: [-1] });
    syncRegistryState();
}

function removeHardwareEntity(index) {
    hardwareRegistry.splice(index, 1);
    syncRegistryState();
}

function saveDynamicRegistryVisual() {
    const msgEl = document.getElementById('configStatusMsg');
    if (ws.readyState === WebSocket.OPEN) {
        ws.send("SAVE_CONFIG_JSON," + JSON.stringify(hardwareRegistry));
        if (msgEl) {
            msgEl.style.color = "var(--blue)";
            msgEl.innerText = "⏳ Sending visual configuration (Runtime RAM)...";
        }
    } else {
        alert("Lost WebSocket connection!");
    }
}

/* ============================================================================
 * REAL-TIME SYSTEM CLOCK
 * ============================================================================ */
let sessionStartTime = Date.now();
let sessionTimerInterval = null;

function initSystemClock() {
    const clockEl = document.getElementById('sysClock');
    if (!clockEl) return;

    const updateClock = () => {
        const now = new Date();
        clockEl.innerText = now.toTimeString().split(' ')[0];
    };

    updateClock();
    setInterval(updateClock, 1000);
}

function startSessionTimer() {
    sessionStartTime = Date.now();
    if (sessionTimerInterval) clearInterval(sessionTimerInterval);

    const timerEl = document.getElementById('sessionTimer');
    if (!timerEl) return;

    const updateTimer = () => {
        const elapsedSec = Math.floor((Date.now() - sessionStartTime) / 1000);
        const hrs = String(Math.floor(elapsedSec / 3600)).padStart(2, '0');
        const mins = String(Math.floor((elapsedSec % 3600) / 60)).padStart(2, '0');
        const secs = String(elapsedSec % 60).padStart(2, '0');

        timerEl.innerText = `${hrs}:${mins}:${secs}`;
    };

    updateTimer();
    sessionTimerInterval = setInterval(updateTimer, 1000);
}

document.addEventListener("DOMContentLoaded", () => {
    initSystemClock();
    startSessionTimer();
});
