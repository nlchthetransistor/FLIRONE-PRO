let ws = null;
let palette = null; // Uint8Array(768)
let reconnectTimer = null;

const THERMAL_WIDTH = 160;
const THERMAL_HEIGHT = 120;

const thermalCanvas = document.getElementById('thermal-canvas');
const thermalCtx = thermalCanvas.getContext('2d');
const roiOverlay = document.getElementById('roi-overlay');

// Keep canvas attribute size in sync with CSS display size
// This eliminates ALL coordinate conversion ambiguity
const canvasResizeObserver = new ResizeObserver(entries => {
    for (const entry of entries) {
        const w = Math.round(entry.contentRect.width);
        const h = Math.round(entry.contentRect.height);
        if (w > 0 && h > 0 && (thermalCanvas.width !== w || thermalCanvas.height !== h)) {
            thermalCanvas.width = w;
            thermalCanvas.height = h;
        }
    }
});
canvasResizeObserver.observe(thermalCanvas);

let frames = 0;
let lastFpsTime = Date.now();

let isDragging = false;
let dragStart = null;
let lastSpot = null;
let lastRoi = null;
let markerTimer = null;
let markerStartTime = 0;
const MARKER_DURATION_MS = 10000; // 10 seconds

function connect() {
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${protocol}//${location.host}`, 'flirone');
    
    ws.onopen = () => { 
        updateStatus('Connected', true); 
        if (reconnectTimer) {
            clearTimeout(reconnectTimer);
            reconnectTimer = null;
        }
    };
    
    ws.onclose = () => { 
        updateStatus('Disconnected', false); 
        scheduleReconnect(); 
    };
    
    ws.onerror = (err) => {
        console.error('WebSocket error:', err);
    };

    ws.onmessage = handleMessage;
}

function updateStatus(text, isConnected) {
    const badge = document.getElementById('connection-status');
    badge.textContent = text;
    badge.className = 'badge ' + (isConnected ? 'success' : 'danger');
}

function scheduleReconnect() {
    if (!reconnectTimer) {
        reconnectTimer = setTimeout(() => {
            console.log('Attempting to reconnect...');
            connect();
        }, 2000);
    }
}

function handleMessage(evt) {
    const msg = JSON.parse(evt.data);
    switch(msg.type) {
        case 'init': handleInit(msg); break;
        case 'frame': handleFrame(msg); break;
        case 'spot_result': handleSpotResult(msg); break;
        case 'roi_result': handleRoiResult(msg); break;
        case 'screenshot_saved': handleScreenshot(msg); break;
    }
}

function handleInit(msg) {
    if (msg.palette) {
        palette = new Uint8Array(msg.palette);
        drawColorBar(0, 100); // Temporary bounds until first frame
    }
}

function handleFrame(msg) {
    // FPS Calculation
    frames++;
    const now = Date.now();
    if (now - lastFpsTime >= 1000) {
        document.getElementById('fps-counter').textContent = frames + ' FPS';
        frames = 0;
        lastFpsTime = now;
    }

    // Draw thermal image
    if (msg.image) {
        const img = new Image();
        img.onload = () => {
            thermalCtx.drawImage(img, 0, 0, thermalCanvas.width, thermalCanvas.height);
            drawCrosshair();
            if (lastSpot) {
                drawSpotMarker(lastSpot);
                drawCountdownBar('blue');
            }
            if (lastRoi) {
                drawRoiRectangle(lastRoi);
                drawCountdownBar('#2ea043');
            }
        };
        img.src = `data:image/jpeg;base64,${msg.image}`;
    }

    // Update ambient
    if (msg.ambient_temp !== undefined) {
        document.getElementById('ambient-temp').textContent = msg.ambient_temp.toFixed(1);
    }
    if (msg.humidity !== undefined) {
        document.getElementById('ambient-rh').textContent = msg.humidity.toFixed(1);
    }
    if (msg.dht_ok !== undefined) {
        const dhtBadge = document.getElementById('dht-status');
        dhtBadge.textContent = msg.dht_ok ? 'DHT OK' : 'DHT N/A';
        dhtBadge.className = 'badge ' + (msg.dht_ok ? 'success' : 'danger');
    }

    // Update servo
    if (msg.servo_angle !== undefined) {
        document.getElementById('servo-angle-val').textContent = msg.servo_angle.toFixed(0);
    }
    if (msg.servo_enabled !== undefined) {
        const servoBadge = document.getElementById('servo-status');
        servoBadge.textContent = msg.servo_enabled ? 'ON' : 'OFF';
        servoBadge.className = 'badge ' + (msg.servo_enabled ? 'success' : 'danger');
    }

    // Update tuning if provided
    if (msg.tunables) {
        updateTuningUI(msg.tunables);
    }

    // Update color bar
    if (msg.min_temp !== undefined && msg.max_temp !== undefined) {
        drawColorBar(msg.min_temp, msg.max_temp);
    }
}

function drawColorBar(minTemp, maxTemp) {
    if (!palette) return;
    const canvas = document.getElementById('colorbar-canvas');
    const ctx = canvas.getContext('2d');
    const h = canvas.height;
    const w = canvas.width;
    
    ctx.clearRect(0, 0, w, h);
    
    // Draw gradient
    for (let y = 0; y < h; y++) {
        const idx = Math.floor((1 - y / h) * 255) * 3;
        ctx.fillStyle = `rgb(${palette[idx]}, ${palette[idx+1]}, ${palette[idx+2]})`;
        ctx.fillRect(0, y, w, 1);
    }
    
    document.getElementById('temp-max').textContent = maxTemp.toFixed(1) + '°C';
    document.getElementById('temp-min').textContent = minTemp.toFixed(1) + '°C';
}

function updateTuningUI(tune) {
    const fields = ['emissivity', 'refl_offset', 'raw_scale', 'temp_offset'];
    fields.forEach(f => {
        if (tune[f] !== undefined) {
            const el = document.getElementById(`tune-${f}`);
            const valEl = document.getElementById(`val-${f}`);
            if (el && valEl && document.activeElement !== el) {
                el.value = tune[f];
                valEl.textContent = tune[f].toFixed(f === 'emissivity' ? 2 : 1);
            }
        }
    });
}

function setupTuningListeners() {
    const fields = ['emissivity', 'refl_offset', 'raw_scale', 'temp_offset'];
    fields.forEach(f => {
        const el = document.getElementById(`tune-${f}`);
        const valEl = document.getElementById(`val-${f}`);
        if (el) {
            el.addEventListener('input', (e) => {
                valEl.textContent = parseFloat(e.target.value).toFixed(f === 'emissivity' ? 2 : 1);
            });
            el.addEventListener('change', (e) => {
                if(ws && ws.readyState === WebSocket.OPEN) {
                    ws.send(JSON.stringify({
                        type: 'tune_set',
                        param: f,
                        value: parseFloat(e.target.value)
                    }));
                }
            });
        }
    });

    document.getElementById('btn-reset-tune').addEventListener('click', () => {
        if(ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({type: 'tune_reset'}));
        }
    });
}

function drawCrosshair() {
    const cx = thermalCanvas.width / 2;
    const cy = thermalCanvas.height / 2;
    thermalCtx.strokeStyle = 'rgba(255, 255, 255, 0.5)';
    thermalCtx.lineWidth = 1;
    thermalCtx.beginPath();
    thermalCtx.moveTo(cx - 10, cy);
    thermalCtx.lineTo(cx + 10, cy);
    thermalCtx.moveTo(cx, cy - 10);
    thermalCtx.lineTo(cx, cy + 10);
    thermalCtx.stroke();
}

// Mouse interaction
thermalCanvas.addEventListener('mousedown', (e) => {
    isDragging = true;
    dragStart = canvasToThermal(e);
    roiOverlay.classList.remove('hidden');
    // Position ROI overlay relative to canvas-wrapper
    const wrapperRect = thermalCanvas.parentElement.getBoundingClientRect();
    updateRoiOverlay(
        e.clientX - wrapperRect.left,
        e.clientY - wrapperRect.top,
        e.clientX - wrapperRect.left,
        e.clientY - wrapperRect.top
    );
});

thermalCanvas.addEventListener('mousemove', (e) => {
    if (isDragging) {
        const wrapperRect = thermalCanvas.parentElement.getBoundingClientRect();
        updateRoiOverlay(
            dragStart.wrapperX,
            dragStart.wrapperY,
            e.clientX - wrapperRect.left,
            e.clientY - wrapperRect.top
        );
    }
});

thermalCanvas.addEventListener('mouseup', (e) => {
    if (!isDragging) return;
    isDragging = false;
    roiOverlay.classList.add('hidden');
    
    const end = canvasToThermal(e);
    
    if (Math.abs(end.x - dragStart.x) > 2 && Math.abs(end.y - dragStart.y) > 2) {
        // ROI
        if(ws && ws.readyState === WebSocket.OPEN) {
            const x0 = Math.min(dragStart.x, end.x);
            const y0 = Math.min(dragStart.y, end.y);
            const x1 = Math.max(dragStart.x, end.x);
            const y1 = Math.max(dragStart.y, end.y);
            ws.send(JSON.stringify({type:'roi', x0, y0, x1, y1}));
        }
    } else {
        // Spot
        if(ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({type:'spot', x:dragStart.x, y:dragStart.y}));
        }
    }
});

function canvasToThermal(e) {
    const rect = thermalCanvas.getBoundingClientRect();
    const wrapperRect = thermalCanvas.parentElement.getBoundingClientRect();
    // Mouse position relative to canvas CSS display area
    const cssX = e.clientX - rect.left;
    const cssY = e.clientY - rect.top;
    // Convert CSS display coords → thermal image coords (0-159, 0-119)
    // MUST use rect.width/height (CSS display size), NOT thermalCanvas.width/height (attribute)
    const thermalX = Math.floor(cssX / rect.width * THERMAL_WIDTH);
    const thermalY = Math.floor(cssY / rect.height * THERMAL_HEIGHT);
    return {
        x: Math.min(Math.max(thermalX, 0), THERMAL_WIDTH - 1),
        y: Math.min(Math.max(thermalY, 0), THERMAL_HEIGHT - 1),
        // Canvas attribute coords (for drawing on canvas context)
        canvasX: cssX / rect.width * thermalCanvas.width,
        canvasY: cssY / rect.height * thermalCanvas.height,
        // Wrapper-relative coords (for ROI overlay div positioning)
        wrapperX: e.clientX - wrapperRect.left,
        wrapperY: e.clientY - wrapperRect.top
    };
}

function updateRoiOverlay(x1, y1, x2, y2) {
    const left = Math.min(x1, x2);
    const top = Math.min(y1, y2);
    const width = Math.abs(x2 - x1);
    const height = Math.abs(y2 - y1);
    
    roiOverlay.style.left = left + 'px';
    roiOverlay.style.top = top + 'px';
    roiOverlay.style.width = width + 'px';
    roiOverlay.style.height = height + 'px';
}

function handleSpotResult(msg) {
    lastSpot = msg;
    lastRoi = null;
    addLogEntry('spot', `Spot: ${msg.temp.toFixed(2)}°C at (${msg.x},${msg.y})`);
    startMarkerTimer();
}

function handleRoiResult(msg) {
    lastRoi = msg;
    lastSpot = null;
    addLogEntry('roi', `ROI Avg: ${msg.avg_temp.toFixed(2)}°C`);
    startMarkerTimer();
}

function startMarkerTimer() {
    if (markerTimer) clearTimeout(markerTimer);
    markerStartTime = Date.now();
    markerTimer = setTimeout(() => {
        lastSpot = null;
        lastRoi = null;
        markerTimer = null;
        markerStartTime = 0;
    }, MARKER_DURATION_MS);
}

function drawCountdownBar(color) {
    if (!markerStartTime) return;
    const elapsed = Date.now() - markerStartTime;
    const remaining = 1.0 - elapsed / MARKER_DURATION_MS;
    if (remaining <= 0) return;

    const barWidth = thermalCanvas.width * 0.4;
    const barHeight = 4;
    const barX = (thermalCanvas.width - barWidth) / 2;
    const barY = thermalCanvas.height - 16;

    // Background
    thermalCtx.fillStyle = 'rgba(0,0,0,0.5)';
    thermalCtx.fillRect(barX - 1, barY - 1, barWidth + 2, barHeight + 2);

    // Progress
    thermalCtx.fillStyle = color;
    thermalCtx.fillRect(barX, barY, barWidth * remaining, barHeight);
}

function drawSpotMarker(spot) {
    const scaleX = thermalCanvas.width / THERMAL_WIDTH;
    const scaleY = thermalCanvas.height / THERMAL_HEIGHT;
    const cx = spot.x * scaleX;
    const cy = spot.y * scaleY;
    
    thermalCtx.strokeStyle = 'blue';
    thermalCtx.lineWidth = 2;
    thermalCtx.beginPath();
    thermalCtx.arc(cx, cy, 5, 0, 2 * Math.PI);
    thermalCtx.stroke();
    
    thermalCtx.fillStyle = 'blue';
    thermalCtx.font = '18px Inter';
    thermalCtx.fillText(`${spot.temp.toFixed(1)}°C`, cx + 8, cy - 8);
}

function drawRoiRectangle(roi) {
    const scaleX = thermalCanvas.width / THERMAL_WIDTH;
    const scaleY = thermalCanvas.height / THERMAL_HEIGHT;
    const x = roi.x0 * scaleX;
    const y = roi.y0 * scaleY;
    const w = (roi.x1 - roi.x0) * scaleX;
    const h = (roi.y1 - roi.y0) * scaleY;
    
    thermalCtx.strokeStyle = 'rgba(46, 160, 67, 0.8)';
    thermalCtx.lineWidth = 2;
    thermalCtx.strokeRect(x, y, w, h);
    
    thermalCtx.fillStyle = '#2ea043';
    thermalCtx.font = '18px Inter';
    thermalCtx.fillText(`${roi.avg_temp.toFixed(1)}°C`, x, y - 5);
}

function addLogEntry(type, text) {
    const ul = document.getElementById('measurement-log');
    const li = document.createElement('li');
    li.className = type;
    
    const time = new Date().toLocaleTimeString([], {hour12:false});
    
    li.innerHTML = `<span class="log-time">${time}</span> <span class="log-val">${text}</span>`;
    
    ul.insertBefore(li, ul.firstChild);
    
    if (ul.children.length > 20) {
        ul.removeChild(ul.lastChild);
    }
}

// Controls setup
document.getElementById('btn-servo-left').addEventListener('click', () => {
    if(ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({type:'servo', direction:'left'}));
});

document.getElementById('btn-servo-right').addEventListener('click', () => {
    if(ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({type:'servo', direction:'right'}));
});

document.getElementById('btn-patient-apply').addEventListener('click', () => {
    const name = document.getElementById('patient-name').value;
    const age = parseInt(document.getElementById('patient-age').value) || 0;
    const gender = document.getElementById('patient-gender').value;
    
    if(ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({type:'patient', name, age, gender}));
    }
});

document.getElementById('btn-screenshot').addEventListener('click', () => {
    if(ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({type:'screenshot'}));
    }
});

function handleScreenshot(msg) {
    if (msg.filename) {
        const link = document.getElementById('download-link');
        link.href = '/screenshots/' + msg.filename; // Assuming static files serve this
        link.download = msg.filename;
        link.style.display = 'block';
        link.textContent = '💾 Download ' + msg.filename;
    }
}

// Init
setupTuningListeners();
connect();

// Shutdown button
const shutdownBtn = document.getElementById('btn-shutdown');
if (shutdownBtn) {
    shutdownBtn.addEventListener('click', () => {
        if (confirm('⚠️ Tắt Raspberry Pi?\n\nChương trình sẽ dừng và RPi sẽ shutdown.\nBạn sẽ cần bật lại bằng tay.')) {
            if (ws && ws.readyState === WebSocket.OPEN) {
                ws.send(JSON.stringify({type: 'shutdown'}));
            }
            updateStatus('Shutting down...', false);
        }
    });
}
