//MIT License
//
//Copyright (c) 2026 TechruWolf & Gemini (AI Collaborator)
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.

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

function isAxisDeviceJS(type) {
    return (type === 0 || type === 4 || type === 5 || type === 6);
}

function isHomableAxisJS(type) {
    return (type === 0 || type === 5);
}

ws.onopen = () => {
    statusEl.innerText = "Connected!";
    statusEl.style.color = "#a6e3a1";
    ws.send("GET_CONFIG_JSON");
};

ws.onclose = () => {
    statusEl.innerText = "Disconnected!";
    statusEl.style.color = "#f38ba8";
};

ws.onmessage = (event) => {
    let msg = event.data;

    if (msg.startsWith("CONFIG_JSON_RESP,")) {
        try {
            let jsonStr = msg.substring(17);
            hardwareRegistry = JSON.parse(jsonStr);
            console.log("Hardware Registry loaded from MCU:", hardwareRegistry);
            buildDynamicUI();
        } catch (e) {
            console.error("Failed to parse hardware config JSON:", e);
        }
    }
    else if (msg === "SAVE_CONFIG_ACK" || msg === "SAVE_CONFIG_RUNTIME_ACK") {
        let msgEl = document.getElementById('configStatusMsg');
        if (msgEl) {
            msgEl.style.color = "#a6e3a1";
            msgEl.innerText = "✅ Configuration applied successfully on RAM (Runtime)!";
        }
    }
    else if (msg === "ACK" || msg === "HOMING_ACK" || msg === "GPIO_ACK") {
        waitingForAck = false;
    }
    else if (msg.startsWith("BUFFER_FULL")) {
        waitingForAck = false;
        transmissionDelay += 10;
        bufferWarning.innerText = "⚠️ Buffer Full! Reducing stream speed (+" + transmissionDelay + "ms)";
    }
    else if (msg.startsWith("STATUS,")) {
        let parts = msg.split(',');
        let axisPositionsMap = {};
        let partIdx = 1;

        hardwareRegistry.forEach(ent => {
            if (ent.type === 1) {
                if (parts[partIdx]) {
                    let val = parseFloat(parts[partIdx]);
                    let tempEl = document.getElementById(`temp_${ent.name}`);
                    if (tempEl) tempEl.innerText = val.toFixed(1);
                }
                partIdx++;
            }
        });

        hardwareRegistry.forEach(ent => {
            if (isAxisDeviceJS(ent.type)) {
                if (parts[partIdx]) {
                    let val = parseFloat(parts[partIdx]);
                    axisPositionsMap[ent.name] = val;
                    let posEl = document.getElementById(`pos_${ent.name}`);
                    if (posEl) posEl.innerText = val.toFixed(1);
                }
                partIdx++;
            }
        });

        let posX = axisPositionsMap["X"] || 0;
        let posY = axisPositionsMap["Y"] || 0;
        let posZ = axisPositionsMap["Z"] || 0;

        updateVisualizer(posX, posY, posZ);
    }
    else if (msg === "EMERGENCY_ACK") {
        waitingForAck = false;
    }
    else if (msg.startsWith("GPIO_READ,")) {
        console.log("GPIO Response:", msg);
    }
};

// ==========================================
// 2. DYNAMIC UI GENERATION & CONFIG TEXTAREA
// ==========================================
function buildDynamicUI() {
    let jogContainer = document.getElementById('dynamicJogContainer');
    if (jogContainer) {
        jogContainer.innerHTML = '<h3>Axis & Motor Control (Dynamic ECS)</h3>';
        hardwareRegistry.forEach((ent) => {
            if (isAxisDeviceJS(ent.type)) {
                let typeDesc = "";
                if (ent.type === 0) typeDesc = "Linear (Limited)";
                else if (ent.type === 4) typeDesc = "Linear (Unlimited)";
                else if (ent.type === 5) typeDesc = "Rotary (Limited)";
                else if (ent.type === 6) typeDesc = "Rotary (Unlimited)";

                let homeButtonHtml = "";
                if (isHomableAxisJS(ent.type)) {
                    homeButtonHtml = `<button onclick="homeAxis('${ent.name}')" style="padding: 5px 10px; background: #f9e2af; color: #11111b; border: none; border-radius: 4px; cursor: pointer; margin-left: 10px;">🏠 Home</button>`;
                }

                let html = `<div class="axis-control-row" style="margin: 10px 0; padding: 8px; background: #181825; border-radius: 6px;">
                <span style="font-weight: bold; color: #89b4fa; width: 110px; display: inline-block;">${ent.name} (${typeDesc}):</span>
                <button onclick="jogAxisDynamic('${ent.name}', -1)" style="padding: 5px 10px; background: #313244; color: #cdd6f4; border: none; border-radius: 4px; cursor: pointer;">(-) Decrease</button>
                <button onclick="jogAxisDynamic('${ent.name}', 1)" style="padding: 5px 10px; background: #313244; color: #cdd6f4; border: none; border-radius: 4px; cursor: pointer;">(+) Increase</button>
                ${homeButtonHtml}
                </div>`;
                jogContainer.innerHTML += html;
            }
        });
    }

    let tempContainer = document.getElementById('dynamicTempContainer');
    if (tempContainer) {
        tempContainer.innerHTML = '<h3>PWM Devices</h3>';
        hardwareRegistry.forEach(ent => {
            if (ent.type === 1) {
                let html = `<div class="thermal-control-row" style="margin: 10px 0; padding: 8px; background: #181825; border-radius: 6px; display: flex; align-items: center; gap: 10px;">
                <span style="font-weight: bold; color: #fab387; width: 130px;">🔥 ${ent.name}:</span>
                <span>Current: <strong id="temp_${ent.name}">0.0</strong></span>
                <input type="number" id="input_${ent.name}" value="0" step="1" style="width: 90px; padding: 5px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 4px;">
                <button onclick="sendIndividualTemp('${ent.name}')" style="padding: 5px 15px; background: #89b4fa; color: #11111b; border: none; border-radius: 4px; font-weight: bold; cursor: pointer;">Set</button>
                </div>`;
                tempContainer.innerHTML += html;
            }
        });
    }

    let configTextArea = document.getElementById('dynamicConfigJson');
    if (configTextArea) {
        configTextArea.value = JSON.stringify(hardwareRegistry, null, 4);
    }

    let coordsContainer = document.getElementById('dynamicCoordsContainer');
    if (coordsContainer) {
        coordsContainer.innerHTML = "";
        hardwareRegistry.forEach(ent => {
            if (isAxisDeviceJS(ent.type)) {
                let span = document.createElement('span');
                span.innerHTML = `${ent.name}: <strong id="pos_${ent.name}">0.0</strong>`;
                coordsContainer.appendChild(span);
            }
        });
    }

    renderVisualConfigCards();
}

function saveDynamicRegistryFromText() {
    let configTextArea = document.getElementById('dynamicConfigJson');
    let msgEl = document.getElementById('configStatusMsg');

    if (!configTextArea) return;

    try {
        let parsedJson = JSON.parse(configTextArea.value);
        hardwareRegistry = parsedJson;
        let jsonString = JSON.stringify(hardwareRegistry);

        if (ws.readyState === WebSocket.OPEN) {
            ws.send("SAVE_CONFIG_JSON," + jsonString);
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
// 3. MANUAL CONTROL & DYNAMIC JOG (ECS MATRIX MAPPING)
// ==========================================
function jogAxisDynamic(axisName, direction) {
    let stepDist = parseFloat(document.getElementById('jogStep').value) * direction;
    let stepsArray = [];

    hardwareRegistry.forEach(ent => {
        if (isAxisDeviceJS(ent.type)) {
            if (ent.name === axisName) {
                let stepsPerUnit = (ent.params && ent.params.length > 0 && ent.params[0] > 0) ? ent.params[0] : 80;
                let steps = Math.round(stepDist * stepsPerUnit);
                stepsArray.push(steps);
            } else {
                stepsArray.push(0);
            }
        }
    });

    let timeUnits = 50;
    let cmd = timeUnits + "," + stepsArray.join(",");
    if (ws.readyState === WebSocket.OPEN) ws.send(cmd);
}

function homeAxis(axisName) {
    if (ws.readyState === WebSocket.OPEN) {
        ws.send("HOME," + axisName);
    }
}

function sendIndividualTemp(entityName) {
    let inputEl = document.getElementById(`input_${entityName}`);
    if (inputEl && ws.readyState === WebSocket.OPEN) {
        let targetVal = inputEl.value;
        ws.send(`SET_,${entityName},${targetVal}`);
    }
}

function turnOffAllPWM() {
    hardwareRegistry.forEach(ent => {
        if (ent.type === 1) {
            let inputEl = document.getElementById(`input_${ent.name}`);
            if (inputEl) inputEl.value = 0;
            if (ws.readyState === WebSocket.OPEN) {
                ws.send(`SET_,${ent.name},0`);
            }
        }
    });
}

function emergencyStop() {
    if (ws.readyState === WebSocket.OPEN) {
        ws.send("EMERGENCY_STOP");
    }
    isStreaming = false;

    streamInfo.innerText = "🚨 EMERGENCY STOP SENT!";
    bufferWarning.innerText = "⚠️ System is locked out due to emergency!";
    waitingForAck = false;

    let btn = document.getElementById('emgBtn');
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
    if (ws.readyState === WebSocket.OPEN) {
        ws.send("RESET_MCU");
    }
    streamInfo.innerText = "🔄 Sending reboot command";
    setTimeout(() => {
        location.reload();
    }, 3000);
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
    const width = container.clientWidth;
    const height = container.clientHeight;
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
    renderer.setSize(width, height);
});

const gridHelper = new THREE.GridHelper(200, 20, 0x89b4fa, 0x45475a);
gridHelper.position.set(100, 0, 100);
scene.add(gridHelper);

const hotendGeometry = new THREE.SphereGeometry(6, 32, 32);
const hotendMaterial = new THREE.MeshBasicMaterial({ color: 0xf38ba8 });
const hotendMesh = new THREE.Mesh(hotendGeometry, hotendMaterial);
scene.add(hotendMesh);

const maxTrailPoints = 500;
let trailPoints = [];

const trailGeometry = new THREE.BufferGeometry();
const trailMaterial = new THREE.LineBasicMaterial({
    color: 0x89b4fa,
    linewidth: 2
});
const trailLine = new THREE.Line(trailGeometry, trailMaterial);
scene.add(trailLine);

function updateVisualizer(x, y, z) {
    hotendMesh.position.set(x, z, y);

    if (document.getElementById('posX')) document.getElementById('posX').innerText = x.toFixed(1);
    if (document.getElementById('posY')) document.getElementById('posY').innerText = y.toFixed(1);
    if (document.getElementById('posZ')) document.getElementById('posZ').innerText = z.toFixed(1);

    let currentPos = new THREE.Vector3(x, z, y);

    if (trailPoints.length === 0 || trailPoints[trailPoints.length - 1].distanceTo(currentPos) > 0.5) {
        trailPoints.push(currentPos);
        if (trailPoints.length > maxTrailPoints) {
            trailPoints.shift();
        }
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
    const file = fileInput.files[0];
    const text = await file.text();
    const lines = text.split('\n');

    streamInfo.innerText = "Parsing G-code dynamically based on Hardware Registry...";
    bufferWarning.innerText = "";
    transmissionDelay = 0;
    let commandBatch = [];

    axisPositions = {};
    hardwareRegistry.forEach(ent => {
        if (isAxisDeviceJS(ent.type)) axisPositions[ent.name] = 0.0;
    });

        for (let i = 0; i < lines.length; i++) {
            let line = lines[i].trim().toUpperCase();

            if (line.startsWith(';') || line === '' || line.startsWith('(')) continue;
            if (line === 'G90') { isAbsoluteMode = true; continue; }
            if (line === 'G91') { isAbsoluteMode = false; continue; }
            if (line.startsWith('M82')) { isAbsoluteExtrusion = true; continue; }
            if (line.startsWith('M83')) { isAbsoluteExtrusion = false; continue; }

            if (line.startsWith('G0') || line.startsWith('G1')) {
                let cmd = parseGLineDynamic(line);
                if (cmd) commandBatch.push(cmd);
            }
            else if (line.startsWith('G28')) {
                commandBatch.push("HOME,ALL");
                Object.keys(axisPositions).forEach(k => axisPositions[k] = 0.0);
            }

            if (i % 2000 === 0) {
                let percent = ((i / lines.length) * 50).toFixed(1);
                progressBar.style.width = percent + "%";
                progressText.innerText = "Progress: " + percent + "% (Dynamic Parsing)";
                lineInfo.innerText = i + " / " + lines.length + " lines";
                await new Promise(r => setTimeout(r, 0));
            }
        }

        streamInfo.innerText = "Successfully parsed " + commandBatch.length + " dynamic commands. Starting stream...";
        isStreaming = true;

        for (let i = 0; i < commandBatch.length; i++) {
            if (!isStreaming) break;
            if (ws.readyState !== WebSocket.OPEN) break;

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
    let parts = line.trim().split(/\s+/);
    let feedrate = 1200;
    let targetPositions = { ...axisPositions };
    let hasMovement = false;

    parts.forEach((p, idx) => {
        if (idx === 0) return;
        let code = p.charAt(0).toUpperCase();
        let val = parseFloat(p.substring(1));
        if (isNaN(val)) return;

        let matched = hardwareRegistry.find(ent =>
        isAxisDeviceJS(ent.type) && ent.name.toUpperCase().startsWith(code)
        );

        if (matched) {
            let axisKey = matched.name;
            let isAbs = (matched.type === 6 || axisKey.startsWith("E")) ? isAbsoluteExtrusion : isAbsoluteMode;

            let currentVal = targetPositions[axisKey] !== undefined ? targetPositions[axisKey] : axisPositions[axisKey];
            targetPositions[axisKey] = isAbs ? val : (currentVal + val);
            hasMovement = true;
        } else if (code === 'F') {
            feedrate = val;
        }
    });

    if (!hasMovement) return null;

    let stepsMap = {}, maxDistSq = 0;
    hardwareRegistry.forEach(ent => {
        if (isAxisDeviceJS(ent.type)) {
            let axisName = ent.name;
            let oldVal = axisPositions[axisName] || 0;
            let newVal = targetPositions[axisName] !== undefined ? targetPositions[axisName] : oldVal;
            let dPos = newVal - oldVal;

            let stepsPerUnit = (ent.params && ent.params[0] > 0) ? ent.params[0] : 80;
            stepsMap[axisName] = Math.round(dPos * stepsPerUnit);

            if (ent.type === 0 || ent.type === 4) {
                maxDistSq += (dPos * dPos);
            }
        }
    });

    let distance = Math.sqrt(maxDistSq);
    axisPositions = { ...targetPositions };

    let timeSeconds = distance > 0 ? (distance / (feedrate / 60)) : 0.1;
    let timeUnits = Math.max(1, Math.round(timeSeconds * 100));

    let payload = [timeUnits];
    hardwareRegistry.forEach(ent => {
        if (isAxisDeviceJS(ent.type)) {
            payload.push(stepsMap[ent.name] || 0);
        }
    });

    return payload.join(",");
}

// ==========================================
// 6. UI TAB SWITCHING SYSTEM
// ==========================================
function switchTab(tabId, btnElement) {
    const contents = document.querySelectorAll('.tab-content');
    contents.forEach(content => content.classList.remove('active'));

    const buttons = document.querySelectorAll('.tab-btn');
    buttons.forEach(btn => btn.classList.remove('active'));

    document.getElementById(tabId).classList.add('active');
    btnElement.classList.add('active');
}

// ==========================================
// VISUAL HARDWARE ECS CONFIGURATOR (UI CARDS)
// ==========================================
function getDeviceTypeName(type) {
    switch(type) {
        case 0: return "Linear Limited";
        case 1: return "PWM PID";
        case 2: return "GPIO Out";
        case 3: return "GPIO In";
        case 4: return "Linear Unlimited";
        case 5: return "Rotary Limited";
        case 6: return "Rotary Unlimited";
        default: return "Unknown";
    }
}

function renderVisualConfigCards() {
    let container = document.getElementById('visualConfigContainer');
    if (!container) return;

    container.innerHTML = "";

    hardwareRegistry.forEach((ent, index) => {
        let card = document.createElement('div');
        card.style.cssText = "background: #1e1e2e; border: 1px solid #313244; border-radius: 8px; padding: 15px; display: grid; grid-template-columns: 2fr 2fr 3fr 3fr 1fr; gap: 10px; align-items: center; margin-bottom: 10px;";

        let nameHtml = `<div>
        <label style="font-size: 11px; color: #a6adc8; display: block;">Device Name</label>
        <input type="text" id="ent_name_${index}" value="${ent.name}" style="width: 100%; padding: 6px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 4px;" onchange="updateEntityFromUI(${index})">
        </div>`;

        let typeHtml = `<div>
        <label style="font-size: 11px; color: #a6adc8; display: block;">Device Type</label>
        <select id="ent_type_${index}" style="width: 100%; padding: 6px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 4px;" onchange="updateEntityFromUI(${index})">
        <option value="0" ${ent.type === 0 ? 'selected' : ''}>0: Linear Limited</option>
        <option value="1" ${ent.type === 1 ? 'selected' : ''}>1: PWM PID</option>
        <option value="2" ${ent.type === 2 ? 'selected' : ''}>2: GPIO Out</option>
        <option value="3" ${ent.type === 3 ? 'selected' : ''}>3: GPIO In</option>
        <option value="4" ${ent.type === 4 ? 'selected' : ''}>4: Linear Unlimited</option>
        <option value="5" ${ent.type === 5 ? 'selected' : ''}>5: Rotary Limited</option>
        <option value="6" ${ent.type === 6 ? 'selected' : ''}>6: Rotary Unlimited</option>
        </select>
        </div>`;

        let pinsStr = ent.pins ? ent.pins.join(", ") : "";
        let pinsHtml = `<div>
        <label style="font-size: 11px; color: #a6adc8; display: block;">GPIO Pins</label>
        <input type="text" id="ent_pins_${index}" value="${pinsStr}" style="width: 100%; padding: 6px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 4px;" onchange="updateEntityFromUI(${index})">
        </div>`;

        let paramsStr = ent.params ? ent.params.join(", ") : "";
        let paramsHtml = `<div>
        <label style="font-size: 11px; color: #a6adc8; display: block;">Parameters</label>
        <input type="text" id="ent_params_${index}" value="${paramsStr}" style="width: 100%; padding: 6px; background: #11111b; border: 1px solid #45475a; color: #cdd6f4; border-radius: 4px;" onchange="updateEntityFromUI(${index})">
        </div>`;

        let deleteHtml = `<div style="text-align: center;">
        <button onclick="removeHardwareEntity(${index})" style="padding: 7px 12px; background: #f38ba8; color: #11111b; border: none; border-radius: 4px; font-weight: bold; cursor: pointer; margin-top: 15px;">🗑️ Delete</button>
        </div>`;

        card.innerHTML = nameHtml + typeHtml + pinsHtml + paramsHtml + deleteHtml;
        container.appendChild(card);
    });
}

function updateEntityFromUI(index) {
    let nameVal = document.getElementById(`ent_name_${index}`).value;
    let typeVal = parseInt(document.getElementById(`ent_type_${index}`).value);
    let pinsVal = document.getElementById(`ent_pins_${index}`).value.split(',').map(v => parseInt(v.trim())).filter(v => !isNaN(v));
    let paramsVal = document.getElementById(`ent_params_${index}`).value.split(',').map(v => parseFloat(v.trim())).filter(v => !isNaN(v));

    hardwareRegistry[index] = {
        name: nameVal,
        type: typeVal,
        pins: pinsVal,
        params: paramsVal
    };

    let configTextArea = document.getElementById('dynamicConfigJson');
    if (configTextArea) {
        configTextArea.value = JSON.stringify(hardwareRegistry, null, 4);
    }
}

function addNewHardwareEntity() {
    hardwareRegistry.push({
        name: "AXIS_NEW",
        type: 0,
        pins: [-1, -1, -1, -1, -1],
        params: [0.0, 0.0, 0.0, 0.0, 0.0]
    });
    renderVisualConfigCards();
    let configTextArea = document.getElementById('dynamicConfigJson');
    if (configTextArea) {
        configTextArea.value = JSON.stringify(hardwareRegistry, null, 4);
    }
}

function removeHardwareEntity(index) {
    hardwareRegistry.splice(index, 1);
    renderVisualConfigCards();
    let configTextArea = document.getElementById('dynamicConfigJson');
    if (configTextArea) {
        configTextArea.value = JSON.stringify(hardwareRegistry, null, 4);
    }
}

function saveDynamicRegistryVisual() {
    let msgEl = document.getElementById('configStatusMsg');

    try {
        let jsonString = JSON.stringify(hardwareRegistry);

        if (ws.readyState === WebSocket.OPEN) {
            ws.send("SAVE_CONFIG_JSON," + jsonString);
            if (msgEl) {
                msgEl.style.color = "#fab387";
                msgEl.innerText = "⏳ Sending visual configuration (Runtime RAM)...";
            }
        } else {
            alert("Lost WebSocket connection!");
        }
    } catch (e) {
        if (msgEl) {
            msgEl.style.color = "#f38ba8";
            msgEl.innerText = "❌ Data Error: " + e.message;
        }
    }
}
