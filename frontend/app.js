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

    /* Eject first if something is inserted, then upload */
    fetch('/api/slots/0/eject', { method: 'POST' }).then(function() {
        const url = '/api/slots/0/upload?chip=' + chip + '&label=' + encodeURIComponent(label);
        const xhr = new XMLHttpRequest();
        xhr.open('POST', url, true);
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');
        progressBar.style.display = 'block';
        uploadBtn.disabled = true;
        resultDiv.innerHTML = '';

        xhr.upload.addEventListener('progress', function(e) {
            if (e.lengthComputable) {
                const pct = Math.round((e.loaded / e.total) * 100);
                progressFill.style.width = pct + '%';
                progressText.textContent = pct + '%  (' + formatBytes(e.loaded) + ' / ' + formatBytes(e.total) + ')';
            }
        });

        xhr.onload = function() {
            uploadBtn.disabled = false;
            if (xhr.status === 200) {
                progressFill.style.width = '100%';
                progressText.textContent = '100%';
                resultDiv.innerHTML = '<span style="color:var(--green)">Upload OK! Inserting...</span>';
                /* Auto-insert after upload */
                fetch('/api/slots/0/insert', { method: 'POST' }).then(function() {
                    resultDiv.innerHTML = '<span style="color:var(--green)">Uploaded &amp; inserted! Emulation active.</span>';
                    htmx.trigger('#image-status', 'load');
                    htmx.trigger('#sys-status', 'load');
                });
            } else {
                resultDiv.innerHTML = '<span style="color:var(--red)">Upload failed: ' + xhr.statusText + '</span>';
            }
        };
        xhr.onerror = function() { uploadBtn.disabled = false; resultDiv.innerHTML = '<span style="color:var(--red)">Upload error</span>'; };
        xhr.send(file);
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
