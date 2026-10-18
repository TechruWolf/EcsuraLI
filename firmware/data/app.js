// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

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

ws.onopen = () => {
    statusEl.innerText = "Connected!";
    statusEl.style.color = "#a6e3a1";
    ws.send("GFG_JSON");
    setTimeout(() => {
        hardwareRegistry.forEach(ent => {
            if (ent.type === DEVICE_GPIO) {
                ws.send(`GPIO,${ent.name}`);
            }
        });
    }, 500);
};

ws.onclose = () => {
    statusEl.innerText = "Disconnected!";
    statusEl.style.color = "#f38ba8";
};

ws.onmessage = (event) => {
    const msg = event.data;

    if (msg.startsWith("CFG_RESP,")) {
        try {
            hardwareRegistry = JSON.parse(msg.substring(9));
            console.log("Hardware Registry loaded from MCU:", hardwareRegistry);
            buildDynamicUI();
        } catch (e) {
            console.error("Failed to parse hardware config JSON:", e);
        }
    }
    else if (msg.startsWith("STATUS,")) {
        const parts = msg.split(',');

        if (parts.length >= 4) {
            const loopTimeUs = parseFloat(parts[1]);
            const freeHeap = parseFloat(parts[2]);
            const totalHeap = parseFloat(parts[3]);

            updateSystemDashboardUI(loopTimeUs, freeHeap, totalHeap);
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
        const msgEl = document.getElementById('configStatusMsg');
        if (msgEl) {
            msgEl.style.color = "#a6e3a1";
            msgEl.innerText = "✅ Saved successfully! Reloading page in 3s...";
        }

        setTimeout(() => {
            location.reload();
        }, 3000);
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
    else if (msg.startsWith("BUF_FULL")) {
        waitingForAck = false;
        transmissionDelay += 10;
        bufferWarning.innerText = `⚠️ Buffer Full! Reducing stream speed (+${transmissionDelay}ms)`;
    }
};


// ============================================================================
// 2. DYNAMIC UI GENERATION & CONFIGURATION TEXTAREA
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
                const homeBtn = canHome
                ? `<button onclick="homeAxis('${ent.name}')" style="padding: 5px 10px; background: #f9e2af; color: #11111b; border: none; border-radius: 4px; cursor: pointer; margin-left: 10px;">🏠 Home</button>`
                : '';

                jogHtml += `<div class="axis-control-row" style="margin: 10px 0; padding: 8px; background: #181825; border-radius: 6px;">
                <span style="font-weight: bold; color: #89b4fa; width: 110px; display: inline-block;">${ent.name} (Axis):</span>
                <button onclick="jogAxisDynamic('${ent.name}', -1)" style="padding: 5px 10px; background: #313244; color: #cdd6f4; border: none; border-radius: 4px; cursor: pointer;">(-) Decrease</button>
                <button onclick="jogAxisDynamic('${ent.name}', 1)" style="padding: 5px 10px; background: #313244; color: #cdd6f4; border: none; border-radius: 4px; cursor: pointer;">(+) Increase</button>
                ${homeBtn}
                </div>`;
            }
        });
        jogContainer.innerHTML = jogHtml;
    }

    // 2. Render PWM & Sensor Devices
    const PWMContainer = document.getElementById('dynamicPWMContainer');
    if (PWMContainer) {
        let tempHtml = '<h3>Sensors & PWM Control Devices</h3>';
        hardwareRegistry.forEach(ent => {
            if (ent.type === DEVICE_PWM_SENSOR) {
                const hasPWMOut = (ent.pins && ent.pins.length > 0 && ent.pins[0] !== -1);
                const currentTarget = ent.params?.[0] || 0;
                const sensorType = (ent.params && ent.params.length > 8) ? ent.params[8] : 1;
                const meta = getSensorMeta(sensorType);
                const controlHtml = hasPWMOut
                ? `<input type="number" id="input_${ent.name}" value="${currentTarget}" step="1" style="width: 90px; padding: 5px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 4px;">
                <button onclick="sendIndividualTemp('${ent.name}')" style="padding: 5px 15px; background: #89b4fa; color: #11111b; border: none; border-radius: 4px; font-weight: bold; cursor: pointer;">Set</button>`
                : `<span style="font-size: 11px; color: #6c7086; font-style: italic;">(${meta.name})</span>`;

                tempHtml += `<div class="PWM-control-row" style="margin: 10px 0; padding: 8px; background: #181825; border-radius: 6px; display: flex; align-items: center; gap: 10px;">
                <span style="font-weight: bold; color: #fab387; width: 130px;">${ent.name}:</span>
                <span>Value: <strong id="temp_${ent.name}">0.0</strong> ${meta.unit}</span>
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
                const span = document.createElement('span');
                span.innerHTML = `${ent.name}: <strong id="pos_${ent.name}">0.0</strong>`;
                coordsContainer.appendChild(span);
            }
        });
    }

    // 4. Render GPIO Controls
    const gpioContainer = document.getElementById('dynamicGPIOContainer');
    if (gpioContainer) {
        let gpioHtml = '<h3>GPIO Control</h3>';
        let hasGpio = false;

        hardwareRegistry.forEach(ent => {
            if (ent.type === DEVICE_GPIO) {
                hasGpio = true;
                const isOutput = ent.params?.[0] === 1;
                const actionButtonHtml = isOutput
                ? `<div><button id="gpio_btn_${ent.name}" onclick="toggleGpioState('${ent.name}')" style="padding: 6px 16px; background: #89b4fa; color: #11111b; border: none; border-radius: 4px; font-weight: bold; cursor: pointer;">Toggle</button></div>`
                : `<span style="font-size: 11px; color: #6c7086; font-style: italic;">(Read-only Input)</span>`;

                gpioHtml += `<div class="gpio-control-row" style="margin: 10px 0; padding: 10px; background: #181825; border-radius: 6px; display: flex; align-items: center; justify-content: space-between;">
                <div style="display: flex; align-items: center; gap: 10px;">
                <span style="font-weight: bold; color: #a6e3a1;">${ent.name}:</span>
                <span id="gpio_status_${ent.name}" style="padding: 2px 8px; font-size: 12px; font-weight: bold; background: #313244; color: #cdd6f4; border-radius: 4px;">READING...</span>
                </div>
                ${actionButtonHtml}
                </div>`;
            }
        });

        if (!hasGpio) {
            gpioHtml += '<span style="color: #a6adc8; font-style: italic;">No GPIO devices configured.</span>';
        }
        gpioContainer.innerHTML = gpioHtml;
    }

    // 5. Render Pipeline Rules
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
        let pipelineHtml = '<h3>Pipeline Logic Rules</h3>';
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
                            return `<span style="color: #89b4fa;">Device: ${hardwareRegistry[idx].name}</span>`;
                        } else {
                            const val = (paramVal !== undefined) ? paramVal : defaultVal;
                            return `<span style="color: #fab387;">Constant: ${val}</span>`;
                        }
                    };

                    const descA = getSourceDesc(idxA, params[0], 0.0);
                    const descB = getSourceDesc(idxB, params[1], 1.0);
                    const descC = getSourceDesc(idxC, params[2], 0.0);
                    const tgtName = (tgtIdx >= 0 && hardwareRegistry[tgtIdx]) ? hardwareRegistry[tgtIdx].name : `Index ${tgtIdx}`;

                    detailsHtml = `
                    <div style="grid-column: span 2; background: #11111b; padding: 6px; border-radius: 4px; margin-bottom: 5px; color: #fab387; font-family: monospace;">
                    📐 FMA Formula: Target = (A * B) + C
                    </div>
                    <div>A Factor: ${descA}</div>
                    <div>B Factor: ${descB}</div>
                    <div>C Factor: ${descC}</div>
                    <div>Target Device: <strong style="color: #a6e3a1;">${tgtName}</strong></div>
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
                    <div style="grid-column: span 2; background: #11111b; padding: 6px; border-radius: 4px; margin-bottom: 5px; color: #89b4fa; font-family: monospace;">
                    🔗 Mode: Standard (Source ➔ Target)
                    </div>
                    <div>Source Device: <strong style="color: #89b4fa;">${srcName}</strong></div>
                    <div>Target Device: <strong style="color: #a6e3a1;">${tgtName}</strong></div>
                    <div>Min Trigger (&lt;): <strong>${minTrig}</strong></div>
                    <div>Max Trigger (&gt;): <strong>${maxTrig}</strong></div>
                    <div>Val on True: <strong style="color: #fab387;">${valTrue}</strong></div>
                    <div>Val on False: <strong style="color: #f38ba8;">${valFalse}</strong></div>
                    `;
                }

                pipelineHtml += `<div style="margin: 10px 0; padding: 10px; background: #181825; border-radius: 6px; border-left: 4px solid ${isFMA ? '#fab387' : '#f9e2af'};">
                <div style="font-weight: bold; color: ${isFMA ? '#fab387' : '#f9e2af'}; margin-bottom: 5px;">⚡ ${ent.name} <span style="font-size: 11px; opacity: 0.8;">(${isFMA ? 'FMA Calculation' : 'If-Else Rule'})</span></div>
                <div style="font-size: 12px; color: #cdd6f4; display: grid; grid-template-columns: 1fr 1fr; gap: 5px;">
                ${detailsHtml}
                </div>
                </div>`;
            }
        });

        if (!hasPipeline) {
            pipelineHtml += '<span style="color: #a6adc8; font-style: italic;">No pipeline rules configured.</span>';
        }
        pipelineContainer.innerHTML = pipelineHtml;
    }

    syncRegistryState();
}

function saveDynamicRegistryFromText() {
    const configTextArea = document.getElementById('dynamicConfigJson');
    const msgEl = document.getElementById('configStatusMsg');
    if (!configTextArea) return;

    try {
        hardwareRegistry = JSON.parse(configTextArea.value);
        if (ws.readyState === WebSocket.OPEN) {
            ws.send("SAVE_CONFIG_JSON," + JSON.stringify(hardwareRegistry));
            if (msgEl) {
                msgEl.style.color = "#fab387";
                msgEl.innerText = "⏳ Sending new configuration (Runtime RAM)...";
            }
        } else {
            alert("Lost WebSocket connection!");
        }
    } catch (e) {
        if (msgEl) {
            msgEl.style.color = "#f38ba8";
            msgEl.innerText = "❌ JSON Syntax Error: " + e.message;
        }
        alert("JSON syntax error! Please check brackets and commas.");
    }
}


// ============================================================================
// 3. MANUAL CONTROL & DYNAMIC JOG
// ============================================================================
function jogAxisDynamic(axisName, direction) {
    const stepDist = parseFloat(document.getElementById('jogStep').value) * direction;
    const stepsArray = hardwareRegistry
    .filter(ent => isAxisDeviceJS(ent.type))
    .map(ent => {
        if (ent.name === axisName) {
            const stepsPerUnit = (ent.params && ent.params[0] > 0) ? ent.params[0] : 80;
            return Math.round(stepDist * stepsPerUnit);
        }
        return 0;
    });

    if (ws.readyState === WebSocket.OPEN) {
        ws.send(`50,${stepsArray.join(",")}`);
    }
}

function homeAxis(axisName) {
    if (ws.readyState === WebSocket.OPEN) ws.send(`HOME,${axisName}`);
}

function sendIndividualTemp(entityName) {
    const inputEl = document.getElementById(`input_${entityName}`);
    if (inputEl && ws.readyState === WebSocket.OPEN) {
        ws.send(`SET_PWM,${entityName},${inputEl.value}`);
    }
}

function turnOffAllPWM() {
    hardwareRegistry.forEach(ent => {
        if (ent.type === DEVICE_PWM_SENSOR) {
            const hasPWMOut = (ent.pins && ent.pins.length > 0 && ent.pins[0] !== -1);
            if (hasPWMOut) {
                const inputEl = document.getElementById(`input_${ent.name}`);
                if (inputEl) inputEl.value = 0;
                if (ws.readyState === WebSocket.OPEN) ws.send(`SET_PWM,${ent.name},0`);
            }
        }
    });
}

function setGpioState(entityName, state) {
    if (ws.readyState === WebSocket.OPEN) {
        ws.send(`GPIO,${entityName},${state}`);
    } else {
        alert("Lost WebSocket connection!");
    }
}

function emergencyStop() {
    if (ws.readyState === WebSocket.OPEN) ws.send("EMG_STOP");
    isStreaming = false;
    streamInfo.innerText = "🚨 EMERGENCY STOP SENT!";
    bufferWarning.innerText = "⚠️ System is locked out due to emergency!";
    waitingForAck = false;

    const btn = document.getElementById('emgBtn');
    if (btn) {
        btn.innerText = "⏳ Locking System...";
        btn.style.backgroundColor = "#fab387";
        btn.onclick = null;
        setTimeout(() => {
            btn.innerText = "🔄 REBOOT";
            btn.style.backgroundColor = "#a6e3a1";
            btn.style.color = "#11111b";
            btn.onclick = resetMcu;
        }, 3000);
    }
}

function resetMcu() {
    if (ws.readyState === WebSocket.OPEN) ws.send("R_MCU");
    streamInfo.innerText = "🔄 Sending reboot command";
    setTimeout(() => location.reload(), 3000);
}


// ============================================================================
// 4. 3D VISUALIZER (THREE.JS)
// ============================================================================
const container = document.getElementById('canvas-container');
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x11111b);

const camera = new THREE.PerspectiveCamera(45, container.clientWidth / container.clientHeight, 0.1, 1000);
camera.position.set(100, 250, 300);
camera.lookAt(100, 0, 100);

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setSize(container.clientWidth, container.clientHeight);
container.appendChild(renderer.domElement);

window.addEventListener('resize', () => {
    camera.aspect = container.clientWidth / container.clientHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(container.clientWidth, container.clientHeight);
});

const gridHelper = new THREE.GridHelper(200, 20, 0x89b4fa, 0x45475a);
gridHelper.position.set(100, 0, 100);
scene.add(gridHelper);

const hotendMesh = new THREE.Mesh(
    new THREE.SphereGeometry(6, 32, 32),
                                  new THREE.MeshBasicMaterial({ color: 0xf38ba8 })
);
scene.add(hotendMesh);

const maxTrailPoints = 500;
let trailPoints = [];
const trailGeometry = new THREE.BufferGeometry();
const trailLine = new THREE.Line(trailGeometry, new THREE.LineBasicMaterial({ color: 0x89b4fa, linewidth: 2 }));
scene.add(trailLine);

function updateVisualizer(x, y, z) {
    hotendMesh.position.set(x, z, y);

    ['posX', 'posY', 'posZ'].forEach((id, idx) => {
        const el = document.getElementById(id);
        if (el) el.innerText = [x, y, z][idx].toFixed(1);
    });

        const currentPos = new THREE.Vector3(x, z, y);
        if (trailPoints.length === 0 || trailPoints[trailPoints.length - 1].distanceTo(currentPos) > 0.5) {
            trailPoints.push(currentPos);
            if (trailPoints.length > maxTrailPoints) trailPoints.shift();
            trailGeometry.setFromPoints(trailPoints);
        }
}

function animate() {
    requestAnimationFrame(animate);
    renderer.render(scene, camera);
}
animate();


// ============================================================================
// 5. G-CODE PARSER & DYNAMIC STREAMER
// ============================================================================
let isAbsoluteMode = true;
let isAbsoluteExtrusion = true;
let axisPositions = {};

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
    hardwareRegistry.forEach(ent => {
        if (isAxisDeviceJS(ent.type)) axisPositions[ent.name] = 0.0;
    });

        for (let i = 0; i < lines.length; i++) {
            const line = lines[i].trim().toUpperCase();
            if (!line || line.startsWith(';') || line.startsWith('(')) continue;
            if (line === 'G90') { isAbsoluteMode = true; continue; }
            if (line === 'G91') { isAbsoluteMode = false; continue; }
            if (line === 'M82') { isAbsoluteExtrusion = true; continue; }
            if (line === 'M83') { isAbsoluteExtrusion = false; continue; }

            if (line.startsWith('G0') || line.startsWith('G1')) {
                const cmd = parseGLineDynamic(line);
                if (cmd) commandBatch.push(cmd);
            } else if (line.startsWith('G28')) {
                commandBatch.push("HOME,ALL");
                Object.keys(axisPositions).forEach(k => axisPositions[k] = 0.0);
            }

            if (i % 50 === 0) {
                const percent = ((i / lines.length) * 50).toFixed(1);
                progressBar.style.width = percent + "%";
                progressText.innerText = `Progress: ${percent}% (Dynamic Parsing)`;
                lineInfo.innerText = `${i} / ${lines.length} lines`;
                await new Promise(r => setTimeout(r, 0));
            }
        }

        streamInfo.innerText = `Successfully parsed ${commandBatch.length} dynamic commands. Starting stream...`;
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
    let feedrate = 1200;
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

    const payload = [Math.max(1, Math.round(timeSeconds * 1000))];
    hardwareRegistry.forEach(ent => {
        if (isAxisDeviceJS(ent.type)) payload.push(stepsMap[ent.name] || 0);
    });

        return payload.join(",");
}


// ============================================================================
// 6. UI TAB SWITCHING & VISUAL CONFIGURATOR CARDS
// ============================================================================
function switchTab(tabId, btnElement) {
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    document.getElementById(tabId).classList.add('active');
    btnElement.classList.add('active');
}

function getEntityFieldLabels(type) {
    switch (type) {
        case DEVICE_AXIS:
            return {
                pins: "Pins (Step, Dir, En, MinEndstop, MaxEndstop)",
                params: "Params (Steps/Unit, MaxLimit, StepDelay, HomeBackoff, HomeOffset)"
            };
        case DEVICE_PWM_SENSOR:
            return {
                pins: "Pins (PWM_Out, Sensor_In)",
                params: "Params (Target, Current, Kp, Ki, Kd, Resistor, Nominal, Beta, SensorType, Delay, Samples)"
            };
        case DEVICE_GPIO:
            return {
                pins: "Pins (TargetPin)",
                params: "Params (Mode:1=Out/0=In, InitialState, PullMode, ActiveLevel, DebounceMs)"
            };
        case DEVICE_PIPELINE:
            return {
                pins: "Pins (2 Pins: Src, Tgt | 4 Pins FMA: A, B, C, Tgt)",
                params: "Params (Standard: MinTrig, MaxTrig, TrueVal, FalseVal | FMA: ConstA, B, C..., Min, Max, True, False)"
            };
        default:
            return {
                pins: "Pins (Pin 0, Pin 1...)",
                params: "Params (Param 0, Param 1...)"
            };
    }
}

function renderVisualConfigCards() {
    const container = document.getElementById('visualConfigContainer');
    if (!container) return;

    container.innerHTML = "";
    hardwareRegistry.forEach((ent, index) => {
        const card = document.createElement('div');
        card.style.cssText = "background: #1e1e2e; border: 1px solid #313244; border-radius: 10px; padding: 15px; margin-bottom: 12px; box-shadow: 0 4px 6px rgba(0,0,0,0.2);";

        const labels = getEntityFieldLabels(ent.type);

        card.innerHTML = `
        <div style="display: grid; grid-template-columns: 2fr 2fr 100px; gap: 10px; align-items: center; margin-bottom: 12px;">
        <div>
        <label style="font-size: 11px; color: #a6adc8; display: block; margin-bottom: 3px;">Device Name</label>
        <input type="text" id="ent_name_${index}" value="${ent.name}" style="width: 100%; padding: 8px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 6px; box-sizing: border-box;" onchange="updateEntityFromUI(${index})">
        </div>
        <div>
        <label style="font-size: 11px; color: #a6adc8; display: block; margin-bottom: 3px;">Device Type</label>
        <select id="ent_type_${index}" style="width: 100%; padding: 8px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 6px; box-sizing: border-box;" onchange="updateEntityFromUI(${index})">
        <option value="${DEVICE_AXIS}" ${ent.type === DEVICE_AXIS ? 'selected' : ''}>Axis (Motor)</option>
        <option value="${DEVICE_PWM_SENSOR}" ${ent.type === DEVICE_PWM_SENSOR ? 'selected' : ''}>Sensor / PWM</option>
        <option value="${DEVICE_GPIO}" ${ent.type === DEVICE_GPIO ? 'selected' : ''}>GPIO (Universal)</option>
        <option value="${DEVICE_PIPELINE}" ${ent.type === DEVICE_PIPELINE ? 'selected' : ''}>Pipeline Logic</option>
        </select>
        </div>
        <div style="text-align: right; padding-top: 15px;">
        <button onclick="removeHardwareEntity(${index})" style="padding: 8px 12px; background: #f38ba8; color: #11111b; border: none; border-radius: 6px; font-weight: bold; cursor: pointer; transition: opacity 0.2s;" title="Delete Entity">🗑️ Delete</button>
        </div>
        </div>

        <div style="margin-bottom: 10px;">
        <label id="label_pins_${index}" style="font-size: 11px; color: #89b4fa; display: block; margin-bottom: 3px; font-weight: bold;">${labels.pins}</label>
        <input type="text" id="ent_pins_${index}" value="${ent.pins?.join(", ") || ""}" style="width: 100%; padding: 8px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 6px; box-sizing: border-box; font-family: monospace;" onchange="updateEntityFromUI(${index})">
        </div>

        <div>
        <label id="label_params_${index}" style="font-size: 11px; color: #fab387; display: block; margin-bottom: 3px; font-weight: bold;">${labels.params}</label>
        <input type="text" id="ent_params_${index}" value="${ent.params?.join(", ") || ""}" style="width: 100%; padding: 8px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 6px; box-sizing: border-box; font-family: monospace;" onchange="updateEntityFromUI(${index})">
        </div>
        `;
        container.appendChild(card);
    });
}

function updateEntityFromUI(index) {
    const newType = parseInt(document.getElementById(`ent_type_${index}`).value);

    hardwareRegistry[index] = {
        name: document.getElementById(`ent_name_${index}`).value,
        type: newType,
        pins: document.getElementById(`ent_pins_${index}`).value.split(',').map(v => parseInt(v.trim())).filter(v => !isNaN(v)),
        params: document.getElementById(`ent_params_${index}`).value.split(',').map(v => parseFloat(v.trim())).filter(v => !isNaN(v))
    };

    renderVisualConfigCards();
    syncRegistryState();
}


function addNewHardwareEntity() {
    hardwareRegistry.push({
        name: "NEW_DEVICE",
        type: DEVICE_PWM_SENSOR,
        pins: [-1],
        params: [-1]
    });
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
            msgEl.style.color = "#fab387";
            msgEl.innerText = "⏳ Sending visual configuration (Runtime RAM)...";
        }
    } else {
        alert("Lost WebSocket connection!");
    }
}

function updateGpioStateUI(entityName, state) {
    const statusBadge = document.getElementById(`gpio_status_${entityName}`);
    if (statusBadge) {
        if (state === 1 || state === "1" || state === true) {
            statusBadge.innerText = "ON (HIGH)";
            statusBadge.style.background = "#a6e3a1";
            statusBadge.style.color = "#11111b";
        } else {
            statusBadge.innerText = "OFF (LOW)";
            statusBadge.style.background = "#f38ba8";
            statusBadge.style.color = "#11111b";
        }
    }
}

function toggleGpioState(entityName) {
    const statusBadge = document.getElementById(`gpio_status_${entityName}`);
    let targetState = 1;

    if (statusBadge) {
        const currentText = statusBadge.innerText;
        if (currentText.includes("ON")) {
            targetState = 0;
        } else {
            targetState = 1;
        }
    }

    if (ws.readyState === WebSocket.OPEN) {
        ws.send(`GPIO,${entityName},${targetState}`);
    } else {
        alert("Lost WebSocket connection!");
    }
}

// ============================================================================
// SYSTEM DASHBOARD UI (CPU LOAD & FREE RAM BARS)
// ============================================================================
function updateSystemDashboardUI(loopTimeUs, freeHeap, totalHeap) {
    const loopMs = (loopTimeUs / 1000.0).toFixed(2);

    const cpuLoadPercent = Math.min(Math.max((loopTimeUs / 5000.0) * 100, 0), 100);
    const ramFreePercent = Math.min(Math.max((freeHeap / totalHeap) * 100, 0), 100);
    const freeKb = (freeHeap / 1024).toFixed(1);
    const totalKb = (totalHeap / 1024).toFixed(1);

    let cpuColor = "#a6e3a1";
    if (cpuLoadPercent >= 80) cpuColor = "#f38ba8";
    else if (cpuLoadPercent >= 25) cpuColor = "#fab387";

    let ramColor = "#a6e3a1";
    if (ramFreePercent <= 25) ramColor = "#f38ba8";
    else if (ramFreePercent <= 50) ramColor = "#fab387";

    let dashContainer = document.getElementById('systemDashboardContainer');
    if (!dashContainer) {
        dashContainer = document.createElement('div');
        dashContainer.id = 'systemDashboardContainer';
        dashContainer.style.cssText = "margin: 15px 0; padding: 12px; background: #181825; border-radius: 8px; border: 1px solid #313244;";

        const targetParent = document.getElementById('streamInfo') || document.body;
        targetParent.parentNode.insertBefore(dashContainer, targetParent.nextSibling);
    }

    dashContainer.innerHTML = `
    <div style="font-weight: bold; color: #89b4fa; margin-bottom: 8px; font-size: 13px;">🖥️ MCU System Dashboard</div>
    <div style="margin-bottom: 8px;">
    <div style="display: flex; justify-content: space-between; font-size: 11px; color: #cdd6f4; margin-bottom: 3px;">
    <span>⚡ System Load (Loop Time):</span>
    <strong>${loopMs} ms (${cpuLoadPercent.toFixed(1)}%)</strong>
    </div>
    <div style="width: 100%; background: #313244; border-radius: 4px; overflow: hidden; height: 8px;">
    <div style="width: ${cpuLoadPercent}%; background: ${cpuColor}; height: 100%; transition: width 0.2s ease;"></div>
    </div>
    </div>
    <div>
    <div style="display: flex; justify-content: space-between; font-size: 11px; color: #cdd6f4; margin-bottom: 3px;">
    <span>💾 Free RAM:</span>
    <strong>${freeKb} KB / ${totalKb} KB (${ramFreePercent.toFixed(1)}%)</strong>
    </div>
    <div style="width: 100%; background: #313244; border-radius: 4px; overflow: hidden; height: 8px;">
    <div style="width: ${ramFreePercent}%; background: ${ramColor}; height: 100%; transition: width 0.2s ease;"></div>
    </div>
    </div>
    `;
}
