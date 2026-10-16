// SPDX-License-Identifier: MIT
// Copyright (c) 2026 TechruWolf & Gemini

// ==========================================
// 1. DYNAMIC CONFIGURATION & WEBSOCKET CONNECTION
// ==========================================
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

const isAxisDeviceJS = (type) => [0, 4, 5, 6].includes(type);
const isHomableAxisJS = (type) => [0, 5].includes(type);

ws.onopen = () => {
    statusEl.innerText = "Connected!";
    statusEl.style.color = "#a6e3a1";
    ws.send("GFG_JSON");
    setTimeout(() => {
        hardwareRegistry.forEach(ent => {
            if (ent.type === 2) {
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
    } else if (msg === "SCA" || msg === "SAVE_CONFIG_RUNTIME_ACK") {
        const msgEl = document.getElementById('configStatusMsg');
        if (msgEl) {
            msgEl.style.color = "#a6e3a1";
            msgEl.innerText = "✅ Configuration applied successfully on RAM (Runtime)!";
        }
    } else if (["ACK", "HACK", "G_ACK", "EMG_ACK"].includes(msg)) {
        waitingForAck = false;
    } else if (msg.startsWith("G_READ,")) {
        const parts = msg.split(',');
        if (parts.length >= 3) {
            const entityName = parts[1];
            const state = parseInt(parts[2]);
            updateGpioStateUI(entityName, state);
        }
    } else if (msg.startsWith("BUF_FULL")) {
        waitingForAck = false;
        transmissionDelay += 10;
        bufferWarning.innerText = `⚠️ Buffer Full! Reducing stream speed (+${transmissionDelay}ms)`;
    } else if (msg.startsWith("STATUS,")) {
        const parts = msg.split(',');
        let partIdx = 1;
        const axisPositionsMap = {};

        hardwareRegistry.forEach(ent => {
            if (ent.type === 1 && parts[partIdx]) {
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
};

// ==========================================
// 2. DYNAMIC UI GENERATION & CONFIG TEXTAREA
// ==========================================
function syncRegistryState() {
    const configTextArea = document.getElementById('dynamicConfigJson');
    if (configTextArea) {
        configTextArea.value = JSON.stringify(hardwareRegistry, null, 4);
    }
    renderVisualConfigCards();
}

function buildDynamicUI() {
    const jogContainer = document.getElementById('dynamicJogContainer');
    if (jogContainer) {
        let jogHtml = '<h3>Axis & Motor Control</h3>';
        hardwareRegistry.forEach(ent => {
            if (isAxisDeviceJS(ent.type)) {
                const typeMap = { 0: "Linear (Limited)", 4: "Linear (Unlimited)", 5: "Rotary (Limited)", 6: "Rotary (Unlimited)" };
                const homeBtn = isHomableAxisJS(ent.type)
                ? `<button onclick="homeAxis('${ent.name}')" style="padding: 5px 10px; background: #f9e2af; color: #11111b; border: none; border-radius: 4px; cursor: pointer; margin-left: 10px;">🏠 Home</button>`
                : '';

                jogHtml += `<div class="axis-control-row" style="margin: 10px 0; padding: 8px; background: #181825; border-radius: 6px;">
                <span style="font-weight: bold; color: #89b4fa; width: 110px; display: inline-block;">${ent.name} (${typeMap[ent.type]}):</span>
                <button onclick="jogAxisDynamic('${ent.name}', -1)" style="padding: 5px 10px; background: #313244; color: #cdd6f4; border: none; border-radius: 4px; cursor: pointer;">(-) Decrease</button>
                <button onclick="jogAxisDynamic('${ent.name}', 1)" style="padding: 5px 10px; background: #313244; color: #cdd6f4; border: none; border-radius: 4px; cursor: pointer;">(+) Increase</button>
                ${homeBtn}
                </div>`;
            }
        });
        jogContainer.innerHTML = jogHtml;
    }

    const PWMContainer = document.getElementById('dynamicPWMContainer');
    if (PWMContainer) {
        let tempHtml = '<h3>PWM Devices</h3>';
        hardwareRegistry.forEach(ent => {
            if (ent.type === 1) {
                const currentTarget = ent.params?.[0] || 0;
                tempHtml += `<div class="PWM-control-row" style="margin: 10px 0; padding: 8px; background: #181825; border-radius: 6px; display: flex; align-items: center; gap: 10px;">
                <span style="font-weight: bold; color: #fab387; width: 130px;"> ${ent.name}:</span>
                <span>Current: <strong id="temp_${ent.name}">0.0</strong></span>
                <input type="number" id="input_${ent.name}" value="${currentTarget}" step="1" style="width: 90px; padding: 5px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 4px;">
                <button onclick="sendIndividualTemp('${ent.name}')" style="padding: 5px 15px; background: #89b4fa; color: #11111b; border: none; border-radius: 4px; font-weight: bold; cursor: pointer;">Set</button>
                </div>`;
            }
        });
        PWMContainer.innerHTML = tempHtml;
    }

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

    const gpioContainer = document.getElementById('dynamicGPIOContainer');
    if (gpioContainer) {
        let gpioHtml = '<h3>GPIO Control</h3>';
        let hasGpio = false;

        hardwareRegistry.forEach(ent => {
            if (ent.type === 2) {
                hasGpio = true;
                const isOutput = ent.params?.[0] === 1;
                const actionButtonHtml = isOutput
                ? `<div><button id="gpio_btn_${ent.name}" onclick="toggleGpioState('${ent.name}')" style="padding: 6px 16px; background: #89b4fa; color: #11111b; border: none; border-radius: 4px; font-weight: bold; cursor: pointer;">Toggle</button></div>`
                : `<span style="font-size: 11px; color: #6c7086; font-style: italic;">(Read-only Input)</span>`;

                gpioHtml += `<div class="gpio-control-row" style="margin: 10px 0; padding: 10px; background: #181825; border-radius: 6px; display: flex; align-items: center; justify-content: space-between;">
                <div style="display: flex; align-items: center; gap: 10px;">
                <span style="font-weight: bold; color: #a6e3a1;"> ${ent.name}:</span>
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
            if (ent.type === 7) {
                hasPipeline = true;
                const srcIdx = ent.pins?.[0] ?? -1;
                const tgtIdx = ent.pins?.[1] ?? -1;

                const srcName = (srcIdx >= 0 && hardwareRegistry[srcIdx]) ? hardwareRegistry[srcIdx].name : `Index ${srcIdx}`;
                const tgtName = (tgtIdx >= 0 && hardwareRegistry[tgtIdx]) ? hardwareRegistry[tgtIdx].name : `Index ${tgtIdx}`;

                const minTrig = ent.params?.[0] !== undefined ? ent.params[0] : 'N/A';
                const maxTrig = ent.params?.[1] !== undefined ? ent.params[1] : 'N/A';
                const valTrue = ent.params?.[2] !== undefined ? ent.params[2] : 'Direct Source';
                const valFalse = ent.params?.[3] !== undefined ? ent.params[3] : 'Ignored/Pass';

                pipelineHtml += `<div style="margin: 10px 0; padding: 10px; background: #181825; border-radius: 6px; border-left: 4px solid #f9e2af;">
                <div style="font-weight: bold; color: #f9e2af; margin-bottom: 5px;">🔗 ${ent.name}</div>
                <div style="font-size: 12px; color: #cdd6f4; display: grid; grid-template-columns: 1fr 1fr; gap: 5px;">
                <div>Source Device: <strong style="color: #89b4fa;">${srcName}</strong></div>
                <div>Target Device: <strong style="color: #a6e3a1;">${tgtName}</strong></div>
                <div>Min Trigger (&lt;): <strong>${minTrig}</strong></div>
                <div>Max Trigger (&gt;): <strong>${maxTrig}</strong></div>
                <div>Val on True: <strong style="color: #fab387;">${valTrue}</strong></div>
                <div>Val on False: <strong style="color: #f38ba8;">${valFalse}</strong></div>
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

// ==========================================
// 3. MANUAL CONTROL & DYNAMIC JOG
// ==========================================
function jogAxisDynamic(axisName, direction) {
    const stepDist = parseFloat(document.getElementById('jogStep').value) * direction;
    const stepsArray = hardwareRegistry
    .filter(ent => isAxisDeviceJS(ent.type))
    .map(ent => {
        if (ent.name === axisName) {
            const stepsPerUnit = ent.params?.[0] > 0 ? ent.params[0] : 80;
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
        if (ent.type === 1) {
            const inputEl = document.getElementById(`input_${ent.name}`);
            if (inputEl) inputEl.value = 0;
            if (ws.readyState === WebSocket.OPEN) ws.send(`SET_PWM,${ent.name},0`);
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

// ==========================================
// 4. 3D VISUALIZER (THREE.JS)
// ==========================================
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

// ==========================================
// 5. G-CODE PARSER & DYNAMIC STREAMER
// ==========================================
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
            const isAbs = (matched.type === 6 || matched.name.startsWith("E")) ? isAbsoluteExtrusion : isAbsoluteMode;
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
            const stepsPerUnit = ent.params?.[0] > 0 ? ent.params[0] : 80;
            const steps = Math.round(dPos * stepsPerUnit);

            stepsMap[name] = steps;
            if ([0, 4].includes(ent.type)) maxDistSq += (dPos * dPos);

            const absSteps = Math.abs(steps);
            if (absSteps > maxStepsInLine) maxStepsInLine = absSteps;
            if (ent.params?.[2] > highestMinDelayUs) highestMinDelayUs = ent.params[2];
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

// ==========================================
// 6. UI TAB SWITCHING SYSTEM
// ==========================================
function switchTab(tabId, btnElement) {
    document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
    document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    document.getElementById(tabId).classList.add('active');
    btnElement.classList.add('active');
}

// ==========================================
// VISUAL HARDWARE ECS CONFIGURATOR (UI CARDS)
// ==========================================
function renderVisualConfigCards() {
    const container = document.getElementById('visualConfigContainer');
    if (!container) return;

    container.innerHTML = "";
    hardwareRegistry.forEach((ent, index) => {
        const card = document.createElement('div');
        card.style.cssText = "background: #1e1e2e; border: 1px solid #313244; border-radius: 8px; padding: 15px; display: grid; grid-template-columns: 2fr 2fr 3fr 3fr 1fr; gap: 10px; align-items: center; margin-bottom: 10px;";

        card.innerHTML = `
        <div>
        <label style="font-size: 11px; color: #a6adc8; display: block;">Device Name</label>
        <input type="text" id="ent_name_${index}" value="${ent.name}" style="width: 100%; padding: 6px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 4px;" onchange="updateEntityFromUI(${index})">
        </div>
        <div>
        <label style="font-size: 11px; color: #a6adc8; display: block;">Device Type</label>
        <select id="ent_type_${index}" style="width: 100%; padding: 6px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 4px;" onchange="updateEntityFromUI(${index})">
        <option value="0" ${ent.type === 0 ? 'selected' : ''}> Linear Limited</option>
        <option value="1" ${ent.type === 1 ? 'selected' : ''}> PWM PID</option>
        <option value="2" ${ent.type === 2 ? 'selected' : ''}> GPIO (Universal)</option>
        <option value="4" ${ent.type === 4 ? 'selected' : ''}> Linear Unlimited</option>
        <option value="5" ${ent.type === 5 ? 'selected' : ''}> Rotary Limited</option>
        <option value="6" ${ent.type === 6 ? 'selected' : ''}> Rotary Unlimited</option>
        <option value="7" ${ent.type === 7 ? 'selected' : ''}> Pipeline Logic</option>
        </select>
        </div>
        <div>
        <label style="font-size: 11px; color: #a6adc8; display: block;">GPIO Pins</label>
        <input type="text" id="ent_pins_${index}" value="${ent.pins?.join(", ") || ""}" style="width: 100%; padding: 6px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 4px;" onchange="updateEntityFromUI(${index})">
        </div>
        <div>
        <label style="font-size: 11px; color: #a6adc8; display: block;">Parameters</label>
        <input type="text" id="ent_params_${index}" value="${ent.params?.join(", ") || ""}" style="width: 100%; padding: 6px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 4px;" onchange="updateEntityFromUI(${index})">
        </div>
        <div style="text-align: center;">
        <button onclick="removeHardwareEntity(${index})" style="padding: 7px 12px; background: #f38ba8; color: #11111b; border: none; border-radius: 4px; font-weight: bold; cursor: pointer; margin-top: 15px;">🗑️ Delete</button>
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
    syncRegistryState();
}

function addNewHardwareEntity() {
    hardwareRegistry.push({
        name: "NEW_DEVICE",
        type: 0,
        pins: [-1],
        params: [0.0]
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
