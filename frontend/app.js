/* ROMEMU - Frontend JavaScript */

let logPaused = false;
let evtSource = null;
let chipDb = null;  /* Cached chip database */
const MAX_LOG_ENTRIES = 500;

/* ---- Toggles ---- */

function toggleApiDocs() {
    const el = document.getElementById('api-docs-content');
    if (el) el.style.display = el.style.display === 'none' ? 'block' : 'none';
}

function toggleChipDb() {
    const el = document.getElementById('chip-data');
    if (el) el.style.display = el.style.display === 'none' ? 'block' : 'none';
}

function togglePinout() {
    const el = document.getElementById('pinout-data');
    if (el) el.style.display = el.style.display === 'none' ? 'block' : 'none';
}

/* ---- SSE Connection ---- */

function connectSSE() {
    if (evtSource) evtSource.close();
    evtSource = new EventSource('/api/events');

    evtSource.addEventListener('access', function(e) {
        if (logPaused) return;
        try { appendLogEntry(JSON.parse(e.data)); } catch (err) {}
    });

    evtSource.addEventListener('stats', function(e) {
        try { updateStatsFromSSE(JSON.parse(e.data)); } catch (err) {}
    });

    evtSource.addEventListener('status', function(e) {
        try { updateStatusBar(JSON.parse(e.data)); } catch (err) {}
    });

    evtSource.addEventListener('slot', function(e) {
        /* Slot state changed (upload/insert/eject/delete) — refresh UI */
        htmx.trigger('#image-status', 'load');
        htmx.trigger('#sys-status', 'load');
    });

    evtSource.onerror = function() { setTimeout(connectSSE, 3000); };
}

/* ---- Access Log ---- */

function appendLogEntry(entry) {
    const tbody = document.getElementById('log-body');
    if (!tbody) return;
    const tr = document.createElement('tr');
    tr.innerHTML =
        '<td class="ts">' + formatTimestamp(entry.ts) + '</td>' +
        '<td class="bus-' + entry.bus + '">' + entry.bus.toUpperCase() + '</td>' +
        '<td class="op-' + entry.op + '">' + entry.op.toUpperCase() + '</td>' +
        '<td class="addr">' + entry.addr + '</td>' +
        '<td>' + entry.len + 'B</td>' +
        '<td>' + entry.cmd + '</td>';
    tbody.appendChild(tr);
    while (tbody.children.length > MAX_LOG_ENTRIES) tbody.removeChild(tbody.firstChild);
    const c = document.getElementById('log-container');
    if (c) c.scrollTop = c.scrollHeight;
}

function formatTimestamp(ms) {
    const s = Math.floor(ms / 1000);
    return pad(Math.floor(s/3600)) + ':' + pad(Math.floor((s%3600)/60)) + ':' + pad(s%60) + '.' + pad3(ms%1000);
}
function pad(n) { return n < 10 ? '0'+n : ''+n; }
function pad3(n) { return n < 10 ? '00'+n : n < 100 ? '0'+n : ''+n; }

function toggleLogPause() {
    logPaused = !logPaused;
    document.getElementById('log-pause-btn').textContent = logPaused ? 'Resume' : 'Pause';
    const b = document.getElementById('log-status');
    b.textContent = logPaused ? 'Paused' : 'Live';
    b.className = 'badge' + (logPaused ? ' paused' : '');
}

function clearLog() {
    const tbody = document.getElementById('log-body');
    if (tbody) tbody.innerHTML = '';
}

/* ---- Stats ---- */

function updateStatsFromSSE(s) {
    const el = document.getElementById('stats-data');
    if (!el) return;
    el.innerHTML = renderStats({
        spi: { reads: s.spi_reads, writes: s.spi_writes, erases: s.spi_erases,
               bytes_read: s.spi_bytes_read, bytes_written: s.spi_bytes_written },
        i2c: { reads: s.i2c_reads, writes: s.i2c_writes, erases: 0,
               bytes_read: s.i2c_bytes_read, bytes_written: s.i2c_bytes_written }
    });
}

function renderStats(d) {
    let h = '<div class="stats-grid">';
    h += statItem('SPI Reads', d.spi.reads, 'spi') + statItem('SPI Writes', d.spi.writes, 'spi');
    h += statItem('SPI Erases', d.spi.erases, 'spi') + statItem('SPI Bytes Read', formatBytes(d.spi.bytes_read), 'spi');
    h += statItem('I2C Reads', d.i2c.reads, 'i2c') + statItem('I2C Writes', d.i2c.writes, 'i2c');
    h += statItem('I2C Bytes Read', formatBytes(d.i2c.bytes_read), 'i2c');
    h += statItem('I2C Bytes Written', formatBytes(d.i2c.bytes_written), 'i2c');
    return h + '</div>';
}

function statItem(label, value, cls) {
    return '<div class="stat-item"><div class="stat-label">' + label + '</div>' +
           '<div class="stat-value ' + cls + '">' + (typeof value === 'number' ? value.toLocaleString() : value) + '</div></div>';
}

/* ---- Status bar ---- */

function updateStatusBar(data) {
    const el = document.getElementById('sys-status');
    if (!el) return;
    let h = '';
    if (data.target) h += '<span class="tag">' + data.target + '</span>';
    if (data.eth_connected) h += '<span class="tag inserted">ETH: ' + data.eth_ip + '</span>';
    if (data.wifi_ap_mode) h += '<span class="tag ap-mode">AP Mode</span>';
    if (data.wifi_ip && data.wifi_ip !== '0.0.0.0') h += '<span class="tag ip">WiFi: ' + data.wifi_ip + '</span>';
    if (!data.wifi_ap_mode && data.wifi_rssi) h += '<span class="tag rssi">RSSI: ' + data.wifi_rssi + '</span>';
    h += '<span class="tag heap">Heap: ' + formatBytes(data.heap_free) + '</span>';
    h += '<span class="tag heap">PSRAM: ' + formatBytes(data.psram_free) + '</span>';
    h += '<span class="tag uptime">Up: ' + formatUptime(data.uptime) + '</span>';
    if (data.spi_chip !== 'none') h += '<span class="tag chip">SPI: ' + data.spi_chip + '</span>';
    if (data.i2c_chip !== 'none') h += '<span class="tag chip">I2C: ' + data.i2c_chip + '</span>';
    el.innerHTML = h;
}

/* ---- GPIO ---- */

function gpioAction(action, params) {
    const opts = { method: 'POST' };
    if (params) { opts.headers = {'Content-Type':'application/json'}; opts.body = JSON.stringify(params); }
    fetch('/api/gpio/' + action, opts).then(r => r.json()).then(updateGpioStatus).catch(e => alert('GPIO failed: ' + e.message));
}

function updateGpioStatus(data) {
    const el = document.getElementById('gpio-status');
    if (!el) return;
    el.innerHTML = '<div class="gpio-status-row">' +
        '<span class="tag ' + (data.reset_asserted ? 'inserted' : '') + '">Reset: ' + (data.reset_asserted ? 'ASSERTED' : 'Released') + '</span>' +
        ' <span class="tag ' + (data.power_on ? 'inserted' : 'ap-mode') + '">Power: ' + (data.power_on ? 'ON' : 'OFF') + '</span></div>';
}

/* ---- Upload ---- */

document.addEventListener('DOMContentLoaded', function() {
    connectSSE();
    const form = document.getElementById('upload-form');
    if (form) form.addEventListener('submit', handleUpload);
    const chipSelect = document.getElementById('upload-chip');
    if (chipSelect) chipSelect.addEventListener('change', showChipDetail);
});

function handleUpload(e) {
    e.preventDefault();
    const fileInput = document.getElementById('upload-file');
    const chipSelect = document.getElementById('upload-chip');
    const labelInput = document.getElementById('upload-label');
    const resultDiv = document.getElementById('upload-result');
    const progressBar = document.getElementById('upload-progress');
    const progressFill = document.getElementById('progress-fill');
    const progressText = document.getElementById('progress-text');
    const uploadBtn = document.getElementById('upload-btn');

    if (!fileInput.files.length) { resultDiv.innerHTML = '<span style="color:var(--red)">Select a file first</span>'; return; }

    const file = fileInput.files[0];
    const chip = chipSelect.value;
    const label = labelInput.value || file.name;

    progressBar.style.display = 'block';
    uploadBtn.disabled = true;
    progressFill.style.width = '0%';

    /* Skip compression for small files (<64KB) — not worth it */
    const MIN_COMPRESS_SIZE = 65536;
    const useCompression = file.size >= MIN_COMPRESS_SIZE && typeof CompressionStream !== 'undefined';

    resultDiv.innerHTML = useCompression ?
        '<span style="color:var(--text-secondary)">Compressing...</span>' :
        '<span style="color:var(--text-secondary)">Uploading...</span>';
    progressText.textContent = useCompression ? 'Compressing...' : '0%';

    /* Eject first, then optionally compress, then upload */
    fetch('/api/slots/0/eject', { method: 'POST' }).then(function() {
        return useCompression ? compressFile(file) : file.arrayBuffer();
    }).then(function(data) {
        var ratio = useCompression ? (file.size / data.byteLength).toFixed(1) : null;
        if (useCompression) {
            resultDiv.innerHTML = '<span style="color:var(--text-secondary)">Compressed: ' +
                formatBytes(file.size) + ' &rarr; ' + formatBytes(data.byteLength) +
                ' (' + ratio + ':1). Uploading...</span>';
        }

        const url = '/api/slots/0/upload?chip=' + chip +
            '&label=' + encodeURIComponent(label) +
            (useCompression ? '&original_size=' + file.size : '');
        const xhr = new XMLHttpRequest();
        xhr.open('POST', url, true);
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');
        if (useCompression) {
            xhr.setRequestHeader('X-Compressed', 'deflate');
            xhr.setRequestHeader('X-Original-Size', file.size.toString());
        }

        var sendSize = data.byteLength;
        xhr.upload.addEventListener('progress', function(e) {
            if (e.lengthComputable) {
                const pct = Math.round((e.loaded / e.total) * 100);
                progressFill.style.width = pct + '%';
                progressText.textContent = pct + '% (' + formatBytes(e.loaded) + ' / ' + formatBytes(sendSize) + ')';
            }
        });

        xhr.onload = function() {
            uploadBtn.disabled = false;
            if (xhr.status === 200) {
                progressFill.style.width = '100%';
                progressText.textContent = '100%';
                resultDiv.innerHTML = '<span style="color:var(--green)">Upload OK! Inserting...</span>';
                fetch('/api/slots/0/insert', { method: 'POST' }).then(function() {
                    var msg = 'Uploaded &amp; inserted!';
                    if (ratio) msg += ' (' + ratio + ':1 compression)';
                    resultDiv.innerHTML = '<span style="color:var(--green)">' + msg + '</span>';
                });
            } else {
                resultDiv.innerHTML = '<span style="color:var(--red)">Upload failed: ' + xhr.statusText + '</span>';
            }
        };
        xhr.onerror = function() { uploadBtn.disabled = false; resultDiv.innerHTML = '<span style="color:var(--red)">Upload error</span>'; };
        xhr.send(data);
    }).catch(function(err) {
        uploadBtn.disabled = false;
        resultDiv.innerHTML = '<span style="color:var(--red)">Upload failed: ' + err.message + '</span>';
    });
}

/* ---- Chip detail display ---- */

function showChipDetail() {
    const sel = document.getElementById('upload-chip');
    const div = document.getElementById('chip-detail');
    if (!sel || !div || !chipDb) return;

    const type = parseInt(sel.value);
    let info = null;
    for (const c of chipDb.spi_chips) { if (c.type === type) { info = c; info._bus = 'SPI'; break; } }
    if (!info) for (const c of chipDb.i2c_chips) { if (c.type === type) { info = c; info._bus = 'I2C'; break; } }
    if (!info) { div.innerHTML = ''; return; }

    let h = '<div class="chip-detail-inner">';
    h += '<span class="tag chip">' + info._bus + '</span> ';
    h += '<strong>' + info.name + '</strong> &mdash; ' + formatBytes(info.size);
    if (info.jedec) h += ' &mdash; JEDEC: <code>' + info.jedec + '</code>';
    if (info.four_byte) h += ' &mdash; 4-byte addr';
    if (info.addr) h += ' &mdash; I2C addr: <code>' + info.addr + '</code>';
    h += '</div>';
    div.innerHTML = h;
}

/* ---- HTMX response processors ---- */

document.addEventListener('htmx:afterSwap', function(e) {
    if (e.detail.target.id === 'sys-status') {
        try { updateStatusBar(JSON.parse(e.detail.target.textContent)); } catch (err) {}
    }

    /* Image status */
    if (e.detail.target.id === 'image-status') {
        try {
            const slots = JSON.parse(e.detail.target.textContent);
            e.detail.target.innerHTML = renderImageStatus(slots[0]);
        } catch (err) {}
    }

    /* Chip database */
    if (e.detail.target.id === 'chip-data') {
        try {
            chipDb = JSON.parse(e.detail.target.textContent);
            e.detail.target.innerHTML = renderChipDb(chipDb);
            populateChipSelect(chipDb);
            showChipDetail();
        } catch (err) {}
    }

    if (e.detail.target.id === 'stats-data') {
        try { e.detail.target.innerHTML = renderStats(JSON.parse(e.detail.target.textContent)); } catch (err) {}
    }

    if (e.detail.target.id === 'gpio-status') {
        try { updateGpioStatus(JSON.parse(e.detail.target.textContent)); } catch (err) {}
    }

    /* Pinout */
    if (e.detail.target.id === 'pinout-data') {
        try {
            const p = JSON.parse(e.detail.target.textContent);
            e.detail.target.innerHTML = renderPinout(p);
        } catch (err) {}
    }

    if (e.detail.target.id === 'wifi-info') {
        try {
            const d = JSON.parse(e.detail.target.textContent);
            let h = '<p>Mode: <span class="tag">' + d.mode + '</span> IP: <span class="tag ip">' + d.ip + '</span>';
            if (d.rssi) h += ' RSSI: <span class="tag rssi">' + d.rssi + '</span>';
            e.detail.target.innerHTML = h + '</p>';
        } catch (err) {}
    }
});

/* ---- Renderers ---- */

function renderImageStatus(s) {
    if (!s || !s.occupied) {
        return '<div class="slot-item empty"><div class="slot-info">' +
               '<div class="slot-name">No image loaded</div>' +
               '<div class="slot-meta"><span>Upload a ROM image below</span></div>' +
               '</div></div>';
    }
    let h = '<div class="slot-item">';
    h += '<div class="slot-info">';
    h += '<div class="slot-name"><span>' + (s.label || 'unnamed') + '</span>';
    if (s.inserted) h += ' <span class="tag inserted">EMULATING</span>';
    if (s.occupied && !s.has_data) h += ' <span class="tag" style="color:var(--orange)">NO DATA (re-upload)</span>';
    h += '</div>';
    h += '<div class="slot-meta">';
    h += '<span>' + s.chip_name + ' (' + s.bus + ')</span>';
    h += '<span>' + formatBytes(s.image_size);
    if (s.compressed && s.alloc_size) {
        h += ' &rarr; ' + formatBytes(s.alloc_size) + ' (' + (s.image_size / s.alloc_size).toFixed(1) + ':1)';
    }
    h += '</span>';
    h += '<span>CRC: ' + s.checksum + '</span>';
    h += '</div></div>';
    h += '<div class="slot-actions">';
    if (s.has_data) {
        if (s.inserted) {
            h += '<button class="btn-eject" onclick="slotAction(\'eject\')">Eject</button>';
        } else {
            h += '<button class="btn-insert" onclick="slotAction(\'insert\')">Insert</button>';
        }
        h += '<a class="btn btn-small" href="/api/slots/0/download">Download</a>';
        if (!s.inserted) h += '<button class="btn btn-small btn-danger" onclick="slotAction(\'delete\')">Delete</button>';
    }
    h += '</div></div>';
    return h;
}

function slotAction(action) {
    if (action === 'delete' && !confirm('Delete the current image?')) return;
    const method = action === 'delete' ? 'DELETE' : 'POST';
    const url = action === 'delete' ? '/api/slots/0' : '/api/slots/0/' + action;
    fetch(url, { method: method }).then(r => r.json()).then(function() {
        htmx.trigger('#image-status', 'load');
        htmx.trigger('#sys-status', 'load');
    });
}

function renderPinout(p) {
    /* Build a set of used GPIOs for highlighting */
    var used = {};
    used[p.spi.cs]   = {fn:'CS#',      cls:'pin-spi'};
    used[p.spi.clk]  = {fn:'CLK',      cls:'pin-spi'};
    used[p.spi.mosi] = {fn:'MOSI/IO0', cls:'pin-spi'};
    used[p.spi.miso] = {fn:'MISO/IO1', cls:'pin-spi'};
    used[p.spi.wp]   = {fn:'WP/IO2',   cls:'pin-spi'};
    used[p.spi.hd]   = {fn:'HD/IO3',   cls:'pin-spi'};
    used[p.i2c.sda]  = {fn:'SDA',      cls:'pin-i2c'};
    used[p.i2c.scl]  = {fn:'SCL',      cls:'pin-i2c'};
    used[p.control.reset] = {fn:'RESET#', cls:'pin-ctrl'};
    used[p.control.power] = {fn:'POWER',  cls:'pin-ctrl'};

    /* P4-Nano header layout (from Waveshare schematic) */
    var leftHeader = [
        [{t:'3V3',     g:-1, c:'pin-pwr'}, {t:'5V',       g:-1, c:'pin-pwr'}],
        [{t:'GPIO7',   g:7,  c:'pin-rsv', alt:'SDA'}, {t:'5V', g:-1, c:'pin-pwr'}],
        [{t:'GPIO8',   g:8,  c:'pin-rsv', alt:'SCL'}, {t:'GND', g:-1, c:'pin-gnd'}],
        [{t:'GPIO23',  g:23, c:'pin-free'}, {t:'GPIO37', g:37, c:'pin-rsv', alt:'UART0'}],
        [{t:'GND',     g:-1, c:'pin-gnd'}, {t:'GPIO38', g:38, c:'pin-rsv', alt:'UART0'}],
        [{t:'GPIO5',   g:5,  c:'pin-free'}, {t:'GPIO4',  g:4,  c:'pin-free'}],
        [{t:'GPIO20',  g:20, c:'pin-free'}, {t:'GND',    g:-1, c:'pin-gnd'}],
        [{t:'GPIO21',  g:21, c:'pin-free'}, {t:'GPIO22', g:22, c:'pin-free'}],
        [{t:'3V3',     g:-1, c:'pin-pwr'}, {t:'GPIO24', g:24, c:'pin-rsv', alt:'USB'}],
        [{t:'GPIO25',  g:25, c:'pin-rsv', alt:'USB'}, {t:'GND', g:-1, c:'pin-gnd'}],
        [{t:'GPIO26',  g:26, c:'pin-rsv', alt:'USB'}, {t:'GPIO27', g:27, c:'pin-rsv', alt:'USB'}],
        [{t:'GPIO32',  g:32, c:'pin-free'}, {t:'GPIO33', g:33, c:'pin-free'}],
        [{t:'GND',     g:-1, c:'pin-gnd'}, {t:'GPIO36', g:36, c:'pin-free'}],
    ];

    var rightHeader = [
        [{t:'5V',      g:-1, c:'pin-pwr'}, {t:'LDO_VO4', g:-1, c:'pin-pwr'}],
        [{t:'GND',     g:-1, c:'pin-gnd'}, {t:'GND',      g:-1, c:'pin-gnd'}],
        [{t:'3V3',     g:-1, c:'pin-pwr'}, {t:'GPIO0',    g:0,  c:'pin-rsv', alt:'XTAL'}],
        [{t:'GND',     g:-1, c:'pin-gnd'}, {t:'GPIO1',    g:1,  c:'pin-rsv', alt:'XTAL'}],
        [{t:'GPIO3',   g:3,  c:'pin-free'}, {t:'GND',     g:-1, c:'pin-gnd'}],
        [{t:'GPIO2',   g:2,  c:'pin-free'}, {t:'GPIO6',   g:6,  c:'pin-free'}],
        [{t:'GPIO54',  g:54, c:'pin-free'}, {t:'GPIO53',  g:53, c:'pin-free'}],
        [{t:'GPIO47',  g:47, c:'pin-free'}, {t:'GPIO48',  g:48, c:'pin-free'}],
        [{t:'GPIO46',  g:46, c:'pin-free'}, {t:'GND',     g:-1, c:'pin-gnd'}],
        [{t:'GPIO45',  g:45, c:'pin-free'}, {t:'C6_RXD',  g:-2, c:'pin-rsv', alt:'C6'}],
        [{t:'C6_IO12', g:-2, c:'pin-rsv', alt:'C6'}, {t:'C6_TXD', g:-2, c:'pin-rsv', alt:'C6'}],
        [{t:'C6_IO13', g:-2, c:'pin-rsv', alt:'C6'}, {t:'C6_IO9', g:-2, c:'pin-rsv', alt:'C6'}],
        [{t:'GND',     g:-1, c:'pin-gnd'}, {t:'GND',      g:-1, c:'pin-gnd'}],
    ];

    /* Override colors for used pins */
    function applyUsed(header) {
        for (var r = 0; r < header.length; r++) {
            for (var c = 0; c < 2; c++) {
                var pin = header[r][c];
                if (pin.g > 0 && used[pin.g]) {
                    pin.c = used[pin.g].cls;
                    pin.fn = used[pin.g].fn;
                }
            }
        }
    }
    applyUsed(leftHeader);
    applyUsed(rightHeader);

    function renderHeader(name, rows) {
        var h = '<div class="header-diagram"><div class="header-title">' + name + '</div>';
        h += '<div class="header-pins">';
        for (var r = 0; r < rows.length; r++) {
            var L = rows[r][0], R = rows[r][1];
            h += '<div class="pin-row">';
            h += '<span class="pin-label-l">' + (L.fn || L.alt || '') + '</span>';
            h += '<span class="pin-dot ' + L.c + '" title="' + L.t + (L.fn ? ' → '+L.fn : '') + '">' + L.t + '</span>';
            h += '<span class="pin-dot ' + R.c + '" title="' + R.t + (R.fn ? ' → '+R.fn : '') + '">' + R.t + '</span>';
            h += '<span class="pin-label-r">' + (R.fn || R.alt || '') + '</span>';
            h += '</div>';
        }
        h += '</div></div>';
        return h;
    }

    var h = '<p class="muted" style="margin-bottom:12px">Board: <strong>' + p.target + '</strong></p>';
    h += '<div class="header-layout">';
    h += renderHeader('Left Header', leftHeader);
    h += '<div class="header-board-label">ESP32-P4-NANO</div>';
    h += renderHeader('Right Header', rightHeader);
    h += '</div>';

    h += '<div class="pin-legend">';
    h += '<span class="pin-leg-item"><span class="pin-swatch pin-spi"></span>SPI Flash</span>';
    h += '<span class="pin-leg-item"><span class="pin-swatch pin-i2c"></span>I2C EEPROM</span>';
    h += '<span class="pin-leg-item"><span class="pin-swatch pin-ctrl"></span>Control</span>';
    h += '<span class="pin-leg-item"><span class="pin-swatch pin-free"></span>Available</span>';
    h += '<span class="pin-leg-item"><span class="pin-swatch pin-rsv"></span>Reserved</span>';
    h += '<span class="pin-leg-item"><span class="pin-swatch pin-pwr"></span>Power</span>';
    h += '<span class="pin-leg-item"><span class="pin-swatch pin-gnd"></span>GND</span>';
    h += '</div>';

    h += '<p class="muted" style="margin-top:8px;font-size:0.75rem">Connect GND between boards. Do not bridge power rails unless intended. I2C needs external pull-ups if target has none.</p>';
    return h;
}

function renderChipDb(data) {
    let h = '<div class="chip-grid">';
    h += '<div class="chip-select-group"><h3>SPI Flash</h3><table class="log-table" style="font-size:0.78rem"><thead><tr>';
    h += '<th>Name</th><th>Size</th><th>JEDEC</th><th>Addr</th></tr></thead><tbody>';
    for (const c of data.spi_chips) {
        h += '<tr><td>' + c.name + '</td><td>' + formatBytes(c.size) + '</td>';
        h += '<td style="font-family:var(--font-mono)">' + c.jedec + '</td>';
        h += '<td>' + (c.four_byte ? '4B' : '3B') + '</td></tr>';
    }
    h += '</tbody></table></div>';
    h += '<div class="chip-select-group"><h3>I2C EEPROM</h3><table class="log-table" style="font-size:0.78rem"><thead><tr>';
    h += '<th>Name</th><th>Size</th><th>Addr</th></tr></thead><tbody>';
    for (const c of data.i2c_chips) {
        h += '<tr><td>' + c.name + '</td><td>' + formatBytes(c.size) + '</td>';
        h += '<td style="font-family:var(--font-mono)">' + c.addr + '</td></tr>';
    }
    h += '</tbody></table></div></div>';
    return h;
}

function populateChipSelect(data) {
    const sel = document.getElementById('upload-chip');
    if (!sel) return;
    sel.innerHTML = '';
    const g1 = document.createElement('optgroup'); g1.label = 'SPI Flash';
    for (const c of data.spi_chips) {
        const o = document.createElement('option');
        o.value = c.type;
        o.textContent = c.name + ' (' + formatBytes(c.size) + ')';
        g1.appendChild(o);
    }
    sel.appendChild(g1);
    const g2 = document.createElement('optgroup'); g2.label = 'I2C EEPROM';
    for (const c of data.i2c_chips) {
        const o = document.createElement('option');
        o.value = c.type;
        o.textContent = c.name + ' (' + formatBytes(c.size) + ')';
        g2.appendChild(o);
    }
    sel.appendChild(g2);
    sel.value = '4'; /* default W25Q128 */
}

/* ---- Helpers ---- */

function formatBytes(bytes) {
    if (!bytes) return '0 B';
    const u = ['B','KB','MB','GB'];
    let i = 0, v = bytes;
    while (v >= 1024 && i < u.length - 1) { v /= 1024; i++; }
    return (i === 0 ? v : v.toFixed(1)) + ' ' + u[i];
}

function formatUptime(s) {
    if (!s) return '0s';
    const h = Math.floor(s/3600), m = Math.floor((s%3600)/60);
    if (h > 0) return h + 'h ' + m + 'm';
    if (m > 0) return m + 'm ' + (s%60) + 's';
    return s + 's';
}

/* ---- Client-side compression (raw DEFLATE via CompressionStream) ---- */

async function compressFile(file) {
    if (typeof CompressionStream === 'undefined') {
        return await file.arrayBuffer();
    }
    try {
        const cs = new CompressionStream('deflate-raw');
        const writer = cs.writable.getWriter();
        const reader = cs.readable.getReader();

        /* Feed file data into compressor */
        const raw = await file.arrayBuffer();
        writer.write(new Uint8Array(raw));
        writer.close();

        /* Read compressed output */
        const chunks = [];
        let totalLen = 0;
        while (true) {
            const { done, value } = await reader.read();
            if (done) break;
            chunks.push(value);
            totalLen += value.byteLength;
        }

        /* Concatenate chunks */
        const result = new Uint8Array(totalLen);
        let offset = 0;
        for (const chunk of chunks) {
            result.set(chunk, offset);
            offset += chunk.byteLength;
        }
        return result.buffer;
    } catch (err) {
        console.warn('Compression failed, sending raw:', err);
        return await file.arrayBuffer();
    }
}
