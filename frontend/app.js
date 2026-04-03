/* ROMEMU - Frontend JavaScript */

let logPaused = false;
let evtSource = null;
const MAX_LOG_ENTRIES = 500;

/* ---- API Docs Toggle ---- */

function toggleApiDocs() {
    const el = document.getElementById('api-docs-content');
    if (el) el.style.display = el.style.display === 'none' ? 'block' : 'none';
}

/* ---- SSE Connection ---- */

function connectSSE() {
    if (evtSource) evtSource.close();
    evtSource = new EventSource('/api/events');

    evtSource.addEventListener('access', function(e) {
        if (logPaused) return;
        try {
            const entry = JSON.parse(e.data);
            appendLogEntry(entry);
        } catch (err) { /* ignore parse errors */ }
    });

    evtSource.addEventListener('stats', function(e) {
        try {
            const stats = JSON.parse(e.data);
            updateStatsFromSSE(stats);
        } catch (err) { /* ignore */ }
    });

    evtSource.addEventListener('status', function(e) {
        try {
            const status = JSON.parse(e.data);
            updateStatusBar(status);
        } catch (err) { /* ignore */ }
    });

    evtSource.onerror = function() {
        setTimeout(connectSSE, 3000);
    };
}

/* ---- Access Log ---- */

function appendLogEntry(entry) {
    const tbody = document.getElementById('log-body');
    if (!tbody) return;

    const tr = document.createElement('tr');

    const ts = formatTimestamp(entry.ts);
    const busClass = 'bus-' + entry.bus;
    const opClass = 'op-' + entry.op;

    tr.innerHTML =
        '<td class="ts">' + ts + '</td>' +
        '<td class="' + busClass + '">' + entry.bus.toUpperCase() + '</td>' +
        '<td class="' + opClass + '">' + entry.op.toUpperCase() + '</td>' +
        '<td class="addr">' + entry.addr + '</td>' +
        '<td>' + entry.len + 'B</td>' +
        '<td>' + entry.cmd + '</td>';

    tbody.appendChild(tr);

    /* Trim old entries */
    while (tbody.children.length > MAX_LOG_ENTRIES) {
        tbody.removeChild(tbody.firstChild);
    }

    /* Auto-scroll */
    const container = document.getElementById('log-container');
    if (container) {
        container.scrollTop = container.scrollHeight;
    }
}

function formatTimestamp(ms) {
    const totalSec = Math.floor(ms / 1000);
    const h = Math.floor(totalSec / 3600);
    const m = Math.floor((totalSec % 3600) / 60);
    const s = totalSec % 60;
    const msRem = ms % 1000;
    return pad(h) + ':' + pad(m) + ':' + pad(s) + '.' + pad3(msRem);
}

function pad(n) { return n < 10 ? '0' + n : '' + n; }

/* ---- GPIO Control ---- */

function gpioAction(action, params) {
    const opts = { method: 'POST' };
    if (params) {
        opts.headers = { 'Content-Type': 'application/json' };
        opts.body = JSON.stringify(params);
    }
    fetch('/api/gpio/' + action, opts)
        .then(function(r) { return r.json(); })
        .then(function(data) {
            updateGpioStatus(data);
        })
        .catch(function(err) { alert('GPIO action failed: ' + err.message); });
}

function updateGpioStatus(data) {
    const el = document.getElementById('gpio-status');
    if (!el) return;
    let html = '<div class="gpio-status-row">';
    html += '<span class="tag ' + (data.reset_asserted ? 'inserted' : '') + '">';
    html += 'Reset: ' + (data.reset_asserted ? 'ASSERTED' : 'Released') + '</span>';
    html += ' <span class="tag ' + (data.power_on ? 'inserted' : 'ap-mode') + '">';
    html += 'Power: ' + (data.power_on ? 'ON' : 'OFF') + '</span>';
    html += '</div>';
    el.innerHTML = html;
}
function pad3(n) { return n < 10 ? '00' + n : n < 100 ? '0' + n : '' + n; }

function toggleLogPause() {
    logPaused = !logPaused;
    const btn = document.getElementById('log-pause-btn');
    const badge = document.getElementById('log-status');
    if (logPaused) {
        btn.textContent = 'Resume';
        badge.textContent = 'Paused';
        badge.className = 'badge paused';
    } else {
        btn.textContent = 'Pause';
        badge.textContent = 'Live';
        badge.className = 'badge';
    }
}

function clearLog() {
    const tbody = document.getElementById('log-body');
    if (tbody) tbody.innerHTML = '';
}

/* ---- Stats update from SSE ---- */

function updateStatsFromSSE(s) {
    const el = document.getElementById('stats-data');
    if (!el) return;
    el.innerHTML = renderStats({
        spi: {
            reads: s.spi_reads, writes: s.spi_writes, erases: s.spi_erases,
            bytes_read: s.spi_bytes_read, bytes_written: s.spi_bytes_written
        },
        i2c: {
            reads: s.i2c_reads, writes: s.i2c_writes, erases: 0,
            bytes_read: s.i2c_bytes_read, bytes_written: s.i2c_bytes_written
        }
    });
}

/* ---- Status bar update ---- */

function updateStatusBar(s) {
    const el = document.getElementById('sys-status');
    if (!el) return;
    let html = '';
    if (s.ap_mode) {
        html += '<span class="tag ap-mode">AP Mode</span>';
    }
    html += '<span class="tag ip">' + s.ip + '</span>';
    if (!s.ap_mode && s.rssi) {
        html += '<span class="tag rssi">RSSI: ' + s.rssi + '</span>';
    }
    html += '<span class="tag heap">Heap: ' + formatBytes(s.heap_free) + '</span>';
    html += '<span class="tag heap">PSRAM: ' + formatBytes(s.psram_free) + '</span>';
    html += '<span class="tag uptime">Up: ' + formatUptime(s.uptime) + '</span>';
    el.innerHTML = html;
}

/* ---- File Upload with Progress ---- */

document.addEventListener('DOMContentLoaded', function() {
    connectSSE();

    const form = document.getElementById('upload-form');
    if (form) {
        form.addEventListener('submit', handleUpload);
    }
});

function handleUpload(e) {
    e.preventDefault();

    const fileInput = document.getElementById('upload-file');
    const slotSelect = document.getElementById('upload-slot');
    const chipSelect = document.getElementById('upload-chip');
    const labelInput = document.getElementById('upload-label');
    const resultDiv = document.getElementById('upload-result');
    const progressBar = document.getElementById('upload-progress');
    const progressFill = document.getElementById('progress-fill');
    const progressText = document.getElementById('progress-text');
    const uploadBtn = document.getElementById('upload-btn');

    if (!fileInput.files.length) {
        resultDiv.innerHTML = '<span style="color:var(--red)">Select a file first</span>';
        return;
    }

    const file = fileInput.files[0];
    const slot = slotSelect.value;
    const chip = chipSelect.value;
    const label = labelInput.value || file.name;

    const url = '/api/slots/' + slot + '/upload?chip=' + chip +
                '&label=' + encodeURIComponent(label);

    const xhr = new XMLHttpRequest();
    xhr.open('POST', url, true);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');

    progressBar.style.display = 'block';
    uploadBtn.disabled = true;

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
            resultDiv.innerHTML = '<span style="color:var(--green)">Upload successful!</span>';
            progressFill.style.width = '100%';
            progressText.textContent = '100%';
            /* Refresh slots list */
            htmx.trigger('#slots-list', 'load');
        } else {
            resultDiv.innerHTML = '<span style="color:var(--red)">Upload failed: ' + xhr.statusText + '</span>';
        }
    };

    xhr.onerror = function() {
        uploadBtn.disabled = false;
        resultDiv.innerHTML = '<span style="color:var(--red)">Upload error</span>';
    };

    xhr.send(file);
}

/* ---- HTMX response processors ---- */

/* Process /api/status response into status bar tags */
document.addEventListener('htmx:afterSwap', function(e) {
    if (e.detail.target.id === 'sys-status') {
        try {
            const data = JSON.parse(e.detail.target.textContent);
            let html = '';
            if (data.target) {
                html += '<span class="tag">' + data.target + '</span>';
            }
            if (data.eth_connected) {
                html += '<span class="tag inserted">ETH: ' + data.eth_ip + '</span>';
            }
            if (data.wifi_ap_mode) {
                html += '<span class="tag ap-mode">AP Mode</span>';
            }
            if (data.wifi_ip && data.wifi_ip !== '0.0.0.0') {
                html += '<span class="tag ip">WiFi: ' + data.wifi_ip + '</span>';
            }
            if (!data.wifi_ap_mode && data.wifi_rssi) {
                html += '<span class="tag rssi">RSSI: ' + data.wifi_rssi + '</span>';
            }
            html += '<span class="tag heap">Heap: ' + formatBytes(data.heap_free) + '</span>';
            html += '<span class="tag heap">PSRAM: ' + formatBytes(data.psram_free) + '</span>';
            html += '<span class="tag uptime">Up: ' + formatUptime(data.uptime) + '</span>';
            if (data.spi_chip !== 'none') {
                html += '<span class="tag chip">SPI: ' + data.spi_chip + '</span>';
            }
            if (data.i2c_chip !== 'none') {
                html += '<span class="tag chip">I2C: ' + data.i2c_chip + '</span>';
            }
            e.detail.target.innerHTML = html;
        } catch (err) { /* leave raw JSON */ }
    }

    /* Process /api/slots response */
    if (e.detail.target.id === 'slots-list') {
        try {
            const slots = JSON.parse(e.detail.target.textContent);
            e.detail.target.innerHTML = renderSlots(slots);
        } catch (err) { /* leave raw */ }
    }

    /* Process /api/chip response */
    if (e.detail.target.id === 'chip-data') {
        try {
            const data = JSON.parse(e.detail.target.textContent);
            e.detail.target.innerHTML = renderChipConfig(data);
            populateUploadChipSelect(data);
        } catch (err) { /* leave raw */ }
    }

    /* Process /api/stats response */
    if (e.detail.target.id === 'stats-data') {
        try {
            const data = JSON.parse(e.detail.target.textContent);
            e.detail.target.innerHTML = renderStats(data);
        } catch (err) { /* leave raw */ }
    }

    /* Process /api/gpio response */
    if (e.detail.target.id === 'gpio-status') {
        try {
            const data = JSON.parse(e.detail.target.textContent);
            updateGpioStatus(data);
        } catch (err) { /* leave raw */ }
    }

    /* Process /api/wifi response */
    if (e.detail.target.id === 'wifi-info') {
        try {
            const data = JSON.parse(e.detail.target.textContent);
            let html = '<p>Mode: <span class="tag">' + data.mode + '</span> ';
            html += 'IP: <span class="tag ip">' + data.ip + '</span>';
            if (data.rssi) html += ' RSSI: <span class="tag rssi">' + data.rssi + '</span>';
            html += '</p>';
            e.detail.target.innerHTML = html;
        } catch (err) { /* leave raw */ }
    }
});

/* ---- Renderers ---- */

function renderSlots(slots) {
    let html = '';
    for (const s of slots) {
        const isEmpty = !s.occupied;
        html += '<div class="slot-item' + (isEmpty ? ' empty' : '') + '">';
        html += '<div class="slot-info">';
        html += '<div class="slot-name">';
        html += '<span>Slot ' + s.index + '</span>';
        if (s.inserted) {
            html += ' <span class="tag inserted">INSERTED</span>';
        }
        if (s.occupied && !s.has_data) {
            html += ' <span class="tag" style="color:var(--orange)">NO DATA (re-upload)</span>';
        }
        html += '</div>';
        if (s.occupied) {
            html += '<div class="slot-meta">';
            html += '<span>' + (s.label || 'unnamed') + '</span>';
            html += '<span>' + s.chip_name + ' (' + s.bus + ')</span>';
            html += '<span>' + formatBytes(s.image_size);
            if (s.compressed && s.alloc_size) {
                var ratio = s.image_size / s.alloc_size;
                html += ' &rarr; ' + formatBytes(s.alloc_size) + ' (' + ratio.toFixed(1) + ':1)';
            }
            html += '</span>';
            html += '<span>CRC: ' + s.checksum + '</span>';
            html += '</div>';
        } else {
            html += '<div class="slot-meta"><span>Empty</span></div>';
        }
        html += '</div>';
        html += '<div class="slot-actions">';
        if (s.occupied && s.has_data) {
            if (s.inserted) {
                html += '<button class="btn-eject" onclick="slotAction(' + s.index + ',\'eject\')">Eject</button>';
            } else {
                html += '<button class="btn-insert" onclick="slotAction(' + s.index + ',\'insert\')">Insert</button>';
            }
            html += '<a class="btn btn-small" href="/api/slots/' + s.index + '/download">DL</a>';
            if (!s.inserted) {
                html += '<button class="btn btn-small btn-danger" onclick="slotAction(' + s.index + ',\'delete\')">Del</button>';
            }
        }
        html += '</div></div>';
    }
    return html;
}

function renderChipConfig(data) {
    let html = '<div class="chip-grid">';

    html += '<div class="chip-select-group"><h3>SPI Flash Chips</h3>';
    html += '<table class="log-table" style="font-size:0.78rem"><thead><tr>';
    html += '<th>Name</th><th>Size</th><th>JEDEC</th><th>Addr</th>';
    html += '</tr></thead><tbody>';
    for (const c of data.spi_chips) {
        html += '<tr>';
        html += '<td>' + c.name + '</td>';
        html += '<td>' + formatBytes(c.size) + '</td>';
        html += '<td style="font-family:var(--font-mono)">' + c.jedec + '</td>';
        html += '<td>' + (c.four_byte ? '4B' : '3B') + '</td>';
        html += '</tr>';
    }
    html += '</tbody></table></div>';

    html += '<div class="chip-select-group"><h3>I2C EEPROMs</h3>';
    html += '<table class="log-table" style="font-size:0.78rem"><thead><tr>';
    html += '<th>Name</th><th>Size</th><th>Addr</th>';
    html += '</tr></thead><tbody>';
    for (const c of data.i2c_chips) {
        html += '<tr>';
        html += '<td>' + c.name + '</td>';
        html += '<td>' + formatBytes(c.size) + '</td>';
        html += '<td style="font-family:var(--font-mono)">' + c.addr + '</td>';
        html += '</tr>';
    }
    html += '</tbody></table></div>';

    html += '</div>';
    return html;
}

function populateUploadChipSelect(data) {
    const select = document.getElementById('upload-chip');
    if (!select) return;
    select.innerHTML = '';

    const group1 = document.createElement('optgroup');
    group1.label = 'SPI Flash';
    for (const c of data.spi_chips) {
        const opt = document.createElement('option');
        opt.value = c.type;
        opt.textContent = c.name + ' (' + formatBytes(c.size) + ')';
        group1.appendChild(opt);
    }
    select.appendChild(group1);

    const group2 = document.createElement('optgroup');
    group2.label = 'I2C EEPROM';
    for (const c of data.i2c_chips) {
        const opt = document.createElement('option');
        opt.value = c.type;
        opt.textContent = c.name + ' (' + formatBytes(c.size) + ')';
        group2.appendChild(opt);
    }
    select.appendChild(group2);

    /* Default to W25Q128 (type=4) */
    select.value = '4';
}

function renderStats(data) {
    let html = '<div class="stats-grid">';
    html += statItem('SPI Reads', data.spi.reads, 'spi');
    html += statItem('SPI Writes', data.spi.writes, 'spi');
    html += statItem('SPI Erases', data.spi.erases, 'spi');
    html += statItem('SPI Bytes Read', formatBytes(data.spi.bytes_read), 'spi');
    html += statItem('I2C Reads', data.i2c.reads, 'i2c');
    html += statItem('I2C Writes', data.i2c.writes, 'i2c');
    html += statItem('I2C Bytes Read', formatBytes(data.i2c.bytes_read), 'i2c');
    html += statItem('I2C Bytes Written', formatBytes(data.i2c.bytes_written), 'i2c');
    html += '</div>';
    return html;
}

function statItem(label, value, cls) {
    return '<div class="stat-item">' +
           '<div class="stat-label">' + label + '</div>' +
           '<div class="stat-value ' + cls + '">' + (typeof value === 'number' ? value.toLocaleString() : value) + '</div>' +
           '</div>';
}

/* ---- Slot Actions ---- */

function slotAction(slot, action) {
    if (action === 'delete' && !confirm('Delete slot ' + slot + '?')) return;

    const method = action === 'delete' ? 'DELETE' : 'POST';
    const url = action === 'delete' ?
        '/api/slots/' + slot :
        '/api/slots/' + slot + '/' + action;

    fetch(url, { method: method })
        .then(function(r) { return r.json(); })
        .then(function() {
            htmx.trigger('#slots-list', 'load');
            htmx.trigger('#sys-status', 'load');
        })
        .catch(function(err) {
            alert('Action failed: ' + err.message);
        });
}

/* ---- Helpers ---- */

function formatBytes(bytes) {
    if (bytes === 0 || bytes === undefined) return '0 B';
    const units = ['B', 'KB', 'MB', 'GB'];
    let i = 0;
    let val = bytes;
    while (val >= 1024 && i < units.length - 1) {
        val /= 1024;
        i++;
    }
    return (i === 0 ? val : val.toFixed(1)) + ' ' + units[i];
}

function formatUptime(seconds) {
    if (!seconds) return '0s';
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    if (h > 0) return h + 'h ' + m + 'm';
    if (m > 0) return m + 'm ' + s + 's';
    return s + 's';
}
