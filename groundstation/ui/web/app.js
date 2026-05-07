const state = {
  data: null,
  selected: "SF-MISSION",
  view: "theater",
  hoverNode: null,
  theater: {
    started: false,
    packets: [],
    stars: [],
    lastPacketKeys: new Set(),
    sweep: 0,
  },
  map: {
    zoom: 1.0,
    targetZoom: 1.0,
    panX: 0,
    panY: 0,
    targetPanX: 0,
    targetPanY: 0,
    dragging: false,
    dragStart: null,
    lastClickNode: null,
  },
  leaflet: {
    map: null,
    overlay: null,
    baseLayer: null,
    initialized: false,
    available: false,
    layer: "dark",
    didFit: false,
  },
  graph: {
    window: 96,
    min: 24,
    max: 180,
  },
  crypto: {
    selected: 0,
  },
  timeline: {
    selected: 0,
  },
  packets: {
    selected: null,
    drawerOpen: false,
    waterfall: [],
  },
  arm: {
    command: "",
    expiresAt: 0,
    button: null,
    label: "",
  },
  focusPulseUntil: 0,
  lastSelected: "SF-MISSION",
  terminal: {
    history: [],
    historyIndex: -1,
    completions: [
      "help", "clear", "nodes", "status", "classify", "rx", "rx latest", "rx clear",
      "rf", "queue", "record",
      "tx ping", "tx selftest", "tx downlink", "tx rotate", "tx arm", "tx isolate", "tx connect",
      "use all", "use selected", "cadence auto", "cadence fast", "cadence normal", "cadence slow",
      "transport lora", "transport wifi", "transport auto", "pair kem", "replay", "save", "export all",
      "addnode TEST-NODE sensor-node lat=37.7749 lon=-122.4194 radio=7 session=0x12345678", "delnode TEST-NODE",
      "view theater", "view overview", "view mission", "view terminal", "view crypto", "view rf", "view operations", "view fleet", "view bringup",
    ],
  },
};

const $ = (id) => document.getElementById(id);

function escapeHtml(value) {
  return String(value ?? "").replace(/[&<>"']/g, (ch) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    '"': "&quot;",
    "'": "&#039;",
  })[ch]);
}

function sevClass(sev) {
  if (sev === "bad" || sev === "FAIL") return "sev-bad";
  if (sev === "warn" || sev === "WARN" || sev === "command") return "sev-warn";
  return "sev-info";
}

function packetKey(packet) {
  return [
    packet.time || "",
    packet.node || "",
    packet.type || "",
    packet.counter ?? "",
    packet.session || "",
    packet.route || "",
    packet.status || "",
  ].join("|");
}

function packetColor(type, status = "") {
  const value = String(type || "").toLowerCase();
  if (status === "rejected" || status === "failed") return "#df5d67";
  if (value.includes("ack")) return "#8fe1ad";
  if (value.includes("diagnostic")) return "#dfc56c";
  if (value.includes("handshake") || value.includes("crypto")) return "#9db7e8";
  if (value.includes("command") || value.includes("tx-")) return "#d8b4ff";
  if (value.includes("replay")) return "#df5d67";
  return "#67d8ee";
}

async function fetchState() {
  const response = await fetch("/api/state", { cache: "no-store" });
  state.data = await response.json();
  state.selected = state.data.selected;
  render();
}

async function runCommand(command) {
  if (!command.trim()) return;
  command = command.trim();
  if (handleLocalCommand(command)) return;
  state.terminal.history.push(command);
  state.terminal.history = state.terminal.history.slice(-80);
  state.terminal.historyIndex = state.terminal.history.length;
  const response = await fetch("/api/command", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ command }),
  });
  const result = await response.json();
  if (result.lines && result.lines.length) {
    toast(result.lines[result.lines.length - 1]);
  }
  if (result.state) {
    state.data = result.state;
    state.selected = state.data.selected;
    render();
  } else {
    fetchState();
  }
}

function handleLocalCommand(command) {
  const lower = command.toLowerCase();
  const viewAliases = {
    "overview": "overview",
    "theater": "theater",
    "stage": "theater",
    "show": "theater",
    "map": "overview",
    "mission": "mission",
    "timeline": "mission",
    "flow": "mission",
    "composer": "mission",
    "terminal": "terminal",
    "term": "terminal",
    "console": "terminal",
    "crypto": "crypto",
    "rxcrypto": "crypto",
    "rf": "rf",
    "radio": "rf",
    "rangepi": "rf",
    "operations": "operations",
    "ops": "operations",
    "fleet": "fleet",
    "bringup": "bringup",
  };
  if (lower.startsWith("view ") || lower.startsWith("open ")) {
    const target = lower.split(/\s+/)[1];
    if (viewAliases[target]) {
      selectView(viewAliases[target]);
      toast(`view ${viewAliases[target]}`);
      return true;
    }
  }
  if (viewAliases[lower] && !["bringup"].includes(lower)) {
    selectView(viewAliases[lower]);
    toast(`view ${viewAliases[lower]}`);
    return true;
  }
  return false;
}

function toast(message) {
  const host = $("toastHost");
  const el = document.createElement("div");
  el.className = "toast";
  el.textContent = message;
  host.appendChild(el);
  setTimeout(() => el.remove(), 3600);
}

function selectView(view) {
  if (!$(`view-${view}`)) return;
  state.view = view;
  document.querySelectorAll(".view").forEach((el) => el.classList.toggle("active", el.id === `view-${view}`));
  document.querySelectorAll(".rail-btn").forEach((el) => el.classList.toggle("active", el.dataset.view === view));
  if (location.hash !== `#${view}`) history.replaceState(null, "", `#${view}`);
  if (view === "overview" && state.leaflet.map) {
    setTimeout(() => state.leaflet.map.invalidateSize(), 80);
  }
  if (view === "theater") {
    setTimeout(() => drawTheaterFrame(performance.now()), 80);
  }
}

function selectedNode() {
  if (!state.data) return null;
  return state.data.nodes.find((node) => node.name === state.data.selected) || state.data.nodes[0];
}

function render() {
  if (!state.data) return;
  const data = state.data;
  if (state.lastSelected !== data.selected) {
    state.lastSelected = data.selected;
    state.focusPulseUntil = performance.now() + 1400;
    document.querySelector(".map-wrap")?.classList.add("focus-pulse");
    setTimeout(() => document.querySelector(".map-wrap")?.classList.remove("focus-pulse"), 1450);
  }
  $("modePill").textContent = data.mode;
  $("routePill").textContent = `ROUTE ${data.transport.toUpperCase()}`;
  $("promptLabel").textContent = `master[${data.target}]>`;
  $("promptLabelMain").textContent = `master[${data.target}]>`;
  $("promptLabelFull").textContent = `master[${data.target}]>`;
  $("acceptedFrames").textContent = data.stats.accepted_frames.toLocaleString();
  $("replayRejects").textContent = data.stats.replay_rejects.toLocaleString();
  $("avgLink").textContent = `${data.stats.avg_link.toFixed(0)}%`;
  $("avgHealth").textContent = `${data.stats.avg_health.toFixed(0)}%`;
  $("avgRisk").textContent = `${data.stats.avg_risk.toFixed(0)}%`;
  $("openAlerts").textContent = data.stats.open_alerts.toLocaleString();
  renderTheater(data);
  renderNodeList(data.nodes);
  renderInspector(selectedNode());
  renderMap(data);
  renderAlerts(data.alerts);
  renderTerminal(data.feed);
  renderTables(data);
  renderPacketWaterfall(data.packets || []);
  renderCrypto(data.crypto_rx || []);
  renderSecurity(data.security || []);
  renderRf(data.hardware || {}, data.recorder || {});
  renderTimeline(data.timeline || []);
  renderFlow(data.flow || []);
  renderComposerTargets(data.nodes || []);
  renderFleetTable(data.nodes);
  renderBringup(data);
  renderScheduler(data.nodes);
  renderCharts(selectedNode());
  drawDonut($("donutHealth"), data.stats.avg_health, "HEALTH", "#7dffb0", false);
  drawDonut($("donutLink"), data.stats.avg_link, "LINK", "#5ce7ff", false);
  drawDonut($("donutRisk"), data.stats.avg_risk, "RISK", "#ff4b57", true);
}

function renderNodeList(nodes) {
  $("nodeList").innerHTML = nodes.map((node) => `
    <div class="node-row ${node.name === state.data.selected ? "active" : ""}" data-node="${escapeHtml(node.name)}">
      <div class="node-dot" style="color:${node.color}; background:${node.color}"></div>
      <div class="node-main">
        <div class="node-name">${escapeHtml(node.name)}</div>
        <div class="node-meta">${escapeHtml(node.role)} / ${escapeHtml(node.classifier_label)} / next ${node.next_tx_s.toFixed(1)}s</div>
      </div>
      <div class="node-link">${node.link_margin.toFixed(0)}%</div>
    </div>
  `).join("");
  document.querySelectorAll(".node-row").forEach((row) => {
    row.addEventListener("click", () => runCommand(`use ${row.dataset.node}`));
  });
}

function renderTheater(data) {
  const node = selectedNode();
  if (!node) return;
  $("theaterTitle").textContent = node.name;
  $("theaterSub").textContent = `${node.role} / ${node.classifier_label} / ${node.session}`;
  $("theaterFrames").textContent = Number(data.stats.accepted_frames || 0).toLocaleString();
  $("theaterHealth").textContent = `${data.stats.avg_health.toFixed(0)}%`;
  $("theaterLink").textContent = `${data.stats.avg_link.toFixed(0)}%`;
  $("theaterRisk").textContent = `${data.stats.avg_risk.toFixed(0)}%`;
  $("theaterNodeCard").innerHTML = `
    <div class="theater-node-name" style="color:${node.color}">${escapeHtml(node.name)}</div>
    <div class="theater-node-meta">${escapeHtml(node.crypto_state)} / ${escapeHtml(node.transport)}</div>
    <div class="theater-meter"><span style="width:${node.link_margin.toFixed(0)}%; background:${node.color}"></span></div>
    <div class="theater-mini-grid">
      ${kv("RSSI", `${node.rssi_dbm.toFixed(1)} dBm`)}
      ${kv("SNR", `${node.snr_db.toFixed(2)} dB`)}
      ${kv("GNSS", `${node.satellites} sats`)}
      ${kv("HDOP", node.hdop.toFixed(2))}
      ${kv("Temp", `${node.temperature_c.toFixed(1)} C`)}
      ${kv("Battery", `${node.battery_percent.toFixed(0)}%`)}
    </div>
  `;
  const latestCommand = data.commands?.[data.commands.length - 1];
  $("theaterCommandSpotlight").innerHTML = latestCommand ? `
    <div class="spot-command">#${latestCommand.command_id} ${escapeHtml(latestCommand.command)}</div>
    <div class="theater-node-meta">${escapeHtml(latestCommand.node)} / ${escapeHtml(latestCommand.route)} / ${escapeHtml(latestCommand.status)}</div>
    <div class="theater-mini-grid">
      ${kv("attempts", latestCommand.attempts ? `${latestCommand.attempts}/${latestCommand.max_attempts}` : "acked")}
      ${kv("created", latestCommand.created)}
      ${kv("detail", latestCommand.detail)}
    </div>
  ` : `<div class="theater-node-meta">No commands yet.</div>`;
  $("theaterEvents").innerHTML = (data.timeline || []).slice(-8).reverse().map((event) => `
    <div class="theater-event">
      <span>${escapeHtml(event.clock || "")}</span>
      <strong class="${sevClass(event.severity)}">${escapeHtml(event.kind || "event")}</strong>
      <em>${escapeHtml(event.summary || "")}</em>
    </div>
  `).join("");
  ingestTheaterPackets(data.packets || []);
  if (!state.theater.started) {
    state.theater.started = true;
    seedTheaterStars();
    requestAnimationFrame(drawTheaterFrame);
  }
}

function ingestTheaterPackets(packets) {
  const keys = new Set();
  packets.slice(-90).forEach((packet) => {
    const key = packetKey(packet);
    keys.add(key);
    if (!state.theater.lastPacketKeys.has(key)) {
      state.theater.packets.push({
        key,
        type: packet.type,
        node: packet.node,
        color: packetColor(packet.type, packet.status),
        born: performance.now(),
        duration: packet.status === "rejected" ? 1800 : 1300,
        rejected: packet.status === "rejected",
      });
    }
  });
  state.theater.lastPacketKeys = keys;
  state.theater.packets = state.theater.packets.slice(-80);
}

function seedTheaterStars() {
  if (state.theater.stars.length) return;
  for (let index = 0; index < 180; index += 1) {
    state.theater.stars.push({
      x: Math.random(),
      y: Math.random(),
      r: 0.4 + Math.random() * 1.8,
      a: 0.16 + Math.random() * 0.52,
      drift: 0.08 + Math.random() * 0.32,
    });
  }
}

function kv(label, value) {
  return `<span>${escapeHtml(label)}</span><strong>${escapeHtml(value)}</strong>`;
}

function renderInspector(node) {
  if (!node) return;
  $("mapSelected").textContent = node.name;
  $("mapCoords").textContent = `lat=${node.latitude.toFixed(5)} lon=${node.longitude.toFixed(5)} session=${node.session}`;
  const faultClass = node.last_fault === "none" ? "ok" : "warn";
  $("inspector").innerHTML = `
    <div class="inspector-head">
      <div>
        <div class="inspector-name">${escapeHtml(node.name)}</div>
        <div class="node-meta">${escapeHtml(node.role)} / ${escapeHtml(node.session)}</div>
      </div>
      <span class="badge ${faultClass}">${escapeHtml(node.last_fault)}</span>
    </div>
    <div class="kv">
      ${kv("state", node.state)}
      ${kv("command", node.command_state)}
      ${kv("crypto", node.crypto_state)}
      ${kv("transport", node.transport)}
      ${kv("classifier", `${node.classifier_label} / ${node.anomaly_score.toFixed(0)}`)}
    </div>
    <div class="kv">
      ${kv("link margin", `${node.link_margin.toFixed(1)}%`)}
      ${kv("MA link", `${node.moving_link.toFixed(1)}%`)}
      ${kv("RSSI", `${node.rssi_dbm.toFixed(1)} dBm`)}
      ${kv("SNR", `${node.snr_db.toFixed(2)} dB`)}
      ${kv("packets", node.packet_counter.toLocaleString())}
      ${kv("interval", `${node.telemetry_interval_s.toFixed(1)}s -> ${node.target_interval_s.toFixed(1)}s`)}
      ${kv("next send", `${node.next_tx_s.toFixed(1)}s`)}
      ${kv("cadence reason", node.cadence_reason)}
    </div>
    <div class="kv">
      ${kv("GNSS sats", node.satellites)}
      ${kv("HDOP", node.hdop.toFixed(2))}
      ${kv("fix age", `${node.fix_age_ms} ms`)}
      ${kv("altitude", `${node.altitude_m.toFixed(1)} m`)}
      ${kv("speed/course", `${node.speed_mps.toFixed(2)} m/s / ${node.course_deg.toFixed(1)} deg`)}
    </div>
    <div class="kv">
      ${kv("temperature", `${node.temperature_c.toFixed(2)} C`)}
      ${kv("battery", `${node.battery_percent.toFixed(1)}%`)}
      ${kv("bus voltage", `${node.bus_voltage.toFixed(3)} V`)}
      ${kv("current", `${node.current_ma.toFixed(1)} mA`)}
    </div>
    <div class="kv">
      ${kv("CO2", `${node.gas_co2_ppm.toFixed(0)} ppm`)}
      ${kv("VOC", `${node.gas_voc_ppb.toFixed(0)} ppb`)}
      ${kv("magnetic", `${node.magnet_ut.toFixed(2)} uT`)}
      ${kv("accel", `${node.accel_g.toFixed(3)} g`)}
      ${kv("gyro", `${node.gyro_dps.toFixed(3)} dps`)}
      ${kv("radiation", `${node.radiation_cpm.toFixed(1)} cpm`)}
    </div>
  `;
}

function renderAlerts(alerts) {
  $("alertList").innerHTML = alerts.slice(-60).reverse().map((item) => `
    <div class="alert-item">
      <span>${escapeHtml(item.time)}</span>
      <span class="${sevClass(item.severity)}">${escapeHtml(item.severity).toUpperCase()}</span>
      <span>${escapeHtml(item.message)}</span>
    </div>
  `).join("");
}

function renderTerminal(feed) {
  const html = feed.slice(-140).map((item) => `
    <div class="feed-line">
      <span>${escapeHtml(item.time)}</span>
      <span class="${sevClass(item.severity)}">${escapeHtml(item.severity).toUpperCase()}</span>
      <span>${escapeHtml(item.message)}</span>
    </div>
  `).join("");
  ["terminalOutput", "terminalOutputMain", "terminalOutputFull"].forEach((id) => {
    const output = $(id);
    if (!output) return;
    const atBottom = output.scrollTop + output.clientHeight >= output.scrollHeight - 24;
    output.innerHTML = html;
    if (atBottom) output.scrollTop = output.scrollHeight;
  });
}

function renderTables(data) {
  const pending = data.stats.pending_commands || 0;
  const failed = data.stats.failed_commands || 0;
  const acked = data.commands.filter((item) => item.status === "acked").length;
  if ($("commandHealth")) {
    $("commandHealth").innerHTML = `
      <div><span class="muted">Pending</span><strong>${pending}</strong></div>
      <div><span class="muted">Failed</span><strong>${failed}</strong></div>
      <div><span class="muted">ACKed</span><strong>${acked}</strong></div>
      <div><span class="muted">Retry Policy</span><strong>${data.commands.length ? `${data.commands[data.commands.length - 1].max_attempts || 0} max` : "idle"}</strong></div>
    `;
  }
  $("commandTable").innerHTML = data.commands.slice(-80).reverse().map((item) => `
    <div class="table-row">
      <span>#${item.command_id}</span>
      <span>${escapeHtml(item.node)}</span>
      <span>${escapeHtml(item.command)}</span>
      <span class="${sevClass(item.status === "acked" ? "good" : item.status === "failed" ? "bad" : "warn")}">${escapeHtml(item.status)} ${item.attempts ? `${item.attempts}/${item.max_attempts}` : ""}</span>
    </div>
  `).join("");
  const orderedPackets = data.packets.slice(-80).reverse();
  $("packetTable").innerHTML = orderedPackets.map((item, index) => `
    <div class="table-row packet-row ${state.packets.selected === packetKey(item) ? "active" : ""}" data-packet="${index}">
      <span>${escapeHtml(item.time)}</span>
      <span>${escapeHtml(item.node)}</span>
      <span>${escapeHtml(item.type)}</span>
      <span class="${sevClass(item.status === "accepted" ? "good" : "bad")}">${escapeHtml(item.status)}</span>
    </div>
  `).join("");
  document.querySelectorAll(".packet-row[data-packet]").forEach((row) => {
    row.addEventListener("click", () => {
      const packet = orderedPackets[Number(row.dataset.packet || 0)];
      openPacketDrawer(packet);
    });
  });
  if (state.packets.drawerOpen) {
    const current = orderedPackets.find((item) => packetKey(item) === state.packets.selected);
    if (current) renderPacketDrawer(current);
  }
}

function renderCrypto(frames) {
  const table = $("cryptoTable");
  const detail = $("cryptoDetail");
  if (!table || !detail) return;
  const ordered = frames.slice(-140).reverse();
  if (state.crypto.selected >= ordered.length) state.crypto.selected = 0;
  table.innerHTML = `
    <div class="crypto-row header">
      <span>Time</span><span>Node</span><span>Type</span><span>Session</span><span>Auth</span><span>Replay</span><span>Ciphertext</span>
    </div>
    ${ordered.map((frame, index) => `
      <div class="crypto-row ${index === state.crypto.selected ? "active" : ""}" data-frame="${index}">
        <span>${escapeHtml(frame.time)}</span>
        <span>${escapeHtml(frame.node)}</span>
        <span>${escapeHtml(frame.packet_type)}</span>
        <span>${escapeHtml(frame.session)}</span>
        <span class="${frame.auth === "PASS" ? "sev-info" : "sev-bad"}">${escapeHtml(frame.auth)}</span>
        <span class="${frame.replay === "PASS" ? "sev-info" : "sev-bad"}">${escapeHtml(frame.replay)}</span>
        <span class="hex-snippet">${escapeHtml(frame.ciphertext_hex)}</span>
      </div>
    `).join("")}
  `;
  document.querySelectorAll(".crypto-row[data-frame]").forEach((row) => {
    row.addEventListener("click", () => {
      state.crypto.selected = Number(row.dataset.frame || 0);
      renderCrypto(frames);
    });
  });
  const selected = ordered[state.crypto.selected];
  detail.textContent = selected ? formatCryptoDetail(selected) : "No encrypted receive frames yet. Wait for adaptive telemetry or run `ping`.";
}

function formatCryptoDetail(frame) {
  return [
    `time          ${frame.time}`,
    `node          ${frame.node}`,
    `route         ${frame.route}`,
    `packet_type   ${frame.packet_type}`,
    `session       ${frame.session}`,
    `counter       ${frame.counter}`,
    `algorithm     ${frame.algorithm}`,
    `classifier    ${frame.classifier}`,
    `cadence       ${frame.cadence_s}s`,
    `auth          ${frame.auth}`,
    `replay        ${frame.replay}`,
    `status        ${frame.status}`,
    `rssi/snr      ${frame.rssi_dbm} dBm / ${frame.snr_db} dB`,
    "",
    "header_hex",
    wrapHex(frame.header_hex),
    "",
    "nonce_hex",
    wrapHex(frame.nonce_hex),
    "",
    "ciphertext_hex",
    wrapHex(frame.ciphertext_hex),
    "",
    "auth_tag_hex",
    wrapHex(frame.tag_hex),
  ].join("\n");
}

function renderSecurity(items) {
  const target = $("securityLifecycle");
  if (!target) return;
  target.innerHTML = items.map((item) => `
    <div class="check-row">
      <span class="${sevClass(item.state)}">${escapeHtml(item.state)}</span>
      <strong>${escapeHtml(item.name)}</strong>
      <span>${escapeHtml(item.detail)}</span>
    </div>
  `).join("");
}

function renderRf(hardware, recorder) {
  const grid = $("rfHealthGrid");
  if (!grid) return;
  const lastRx = hardware.rangepi_last_rx_age_s == null ? "never" : `${hardware.rangepi_last_rx_age_s.toFixed(1)}s`;
  grid.innerHTML = `
    <div class="metric"><span>Link</span><strong>${hardware.rangepi_connected ? "ONLINE" : "OFFLINE"}</strong></div>
    <div class="metric"><span>Port</span><strong>${escapeHtml(hardware.rangepi_port || "none")}</strong></div>
    <div class="metric"><span>RX Packets</span><strong>${Number(hardware.rangepi_rx_packets || 0).toLocaleString()}</strong></div>
    <div class="metric"><span>TX Lines</span><strong>${Number(hardware.rangepi_tx_lines || 0).toLocaleString()}</strong></div>
    <div class="metric"><span>Parse Errors</span><strong>${Number(hardware.rangepi_parse_errors || 0).toLocaleString()}</strong></div>
    <div class="metric"><span>Last RX</span><strong>${lastRx}</strong></div>
  `;
  const lines = hardware.recent_lines || [];
  $("rfRawLines").innerHTML = lines.slice().reverse().map((line) => `
    <div class="raw-line-row">
      <span>${escapeHtml(line.time)}</span>
      <span class="${line.direction === "TX" ? "sev-warn" : "sev-info"}">${escapeHtml(line.direction)}</span>
      <span>${escapeHtml(line.text)}</span>
    </div>
  `).join("");
  $("recorderStatus").textContent = [
    `session_path   ${recorder.session_path || "not started"}`,
    `bytes_rx       ${hardware.rangepi_bytes_rx || 0}`,
    `bytes_tx       ${hardware.rangepi_bytes_tx || 0}`,
    `packet_rate    ${(hardware.rangepi_packet_rate || 0).toFixed(2)}/s`,
    `rx_lines       ${hardware.rangepi_rx_lines || 0}`,
    `tx_lines       ${hardware.rangepi_tx_lines || 0}`,
  ].join("\n");
  drawRfChart($("rfPacketChart"), hardware);
}

function renderTimeline(events) {
  const list = $("incidentTimeline");
  const detail = $("timelineDetail");
  if (!list) return;
  const ordered = events.slice(-140).reverse();
  if (state.timeline.selected >= ordered.length) state.timeline.selected = 0;
  list.innerHTML = ordered.map((event, index) => `
    <div class="timeline-event ${index === state.timeline.selected ? "active" : ""}" data-event="${index}">
      <span>${escapeHtml(event.clock || "")}</span>
      <span class="timeline-dot ${sevClass(event.severity)}"></span>
      <div>
        <div class="summary">${escapeHtml(event.summary || event.kind)}</div>
        <div class="meta">${escapeHtml(event.kind)} ${event.node ? `/ ${escapeHtml(event.node)}` : ""}</div>
      </div>
    </div>
  `).join("");
  document.querySelectorAll(".timeline-event[data-event]").forEach((row) => {
    row.addEventListener("click", () => {
      state.timeline.selected = Number(row.dataset.event || 0);
      renderTimeline(events);
    });
  });
  const selected = ordered[state.timeline.selected];
  if (detail) detail.textContent = selected ? JSON.stringify(selected, null, 2) : "No timeline events yet.";
}

function renderFlow(stages) {
  const target = $("packetFlow");
  if (!target) return;
  target.innerHTML = stages.map((stage) => {
    const stateName = String(stage.state || "WARN").toLowerCase();
    return `
      <div class="flow-stage ${stateName === "pass" ? "pass" : stateName === "fail" ? "fail" : "warn"}">
        <strong>${escapeHtml(stage.name)}</strong>
        <span class="${sevClass(stage.state)}">${escapeHtml(stage.state)}</span>
        <span>${escapeHtml(stage.detail)}</span>
      </div>
    `;
  }).join("");
}

function renderComposerTargets(nodes) {
  const target = $("composerTarget");
  if (!target) return;
  const current = target.value || "selected";
  const options = [
    ["selected", "Selected Node"],
    ["all", "All Nodes"],
    ...nodes.map((node) => [node.name, node.name]),
  ];
  target.innerHTML = options.map(([value, label]) => `<option value="${escapeHtml(value)}">${escapeHtml(label)}</option>`).join("");
  target.value = options.some(([value]) => value === current) ? current : "selected";
  updateComposerPreview();
}

function composerCommandText() {
  const target = $("composerTarget")?.value || "selected";
  const command = $("composerCommand")?.value || "tx ping";
  const arg = $("composerArg")?.value || "";
  if (command === "cadence" || command === "transport") {
    const value = arg || (command === "cadence" ? "auto" : "auto");
    if (target === "selected") return `${command} ${value}`;
    if (target === "all") return `use all && ${command} ${value}`;
    return `use ${target} && ${command} ${value}`;
  }
  const parts = command.split(/\s+/);
  if (target === "selected") return command;
  if (target === "all") return `${command} all`;
  if (parts[0] === "tx") return `${command} ${target}`;
  return `${command} ${target}`;
}

function updateComposerPreview() {
  const preview = $("composerPreview");
  if (!preview) return;
  const auth = $("composerAuth")?.value || "session";
  preview.textContent = [
    `command        ${composerCommandText().replace(" && ", " ; ")}`,
    `auth_mode      ${auth}`,
    `target         ${$("composerTarget")?.value || "selected"}`,
    `transport      ${state.data?.transport || "pending"}`,
    `expected_ack   yes`,
    `timeout        retry policy from mission core`,
  ].join("\n");
}

async function submitComposer() {
  const text = composerCommandText();
  if (isDangerousCommand(text) && !window.confirm(`Transmit protected command "${text.replace(" && ", " ; ")}"?`)) {
    return;
  }
  if (text.includes(" && ")) {
    for (const part of text.split(" && ")) {
      await runCommand(part);
    }
    return;
  }
  await runCommand(text);
}

function isDangerousCommand(command) {
  const lower = String(command || "").toLowerCase();
  return /\b(replay|isolate|rotate|arm|delnode)\b/.test(lower) || lower.includes("tx rotate") || lower.includes("tx isolate") || lower.includes("tx arm");
}

function handleCommandButton(btn) {
  const command = btn.dataset.command || "";
  if (btn.dataset.arm === "true" || isDangerousCommand(command)) {
    const now = Date.now();
    const armed = state.arm.button === btn && state.arm.command === command && state.arm.expiresAt > now;
    if (!armed) {
      clearArmedButton();
      state.arm = {
        command,
        expiresAt: now + 3200,
        button: btn,
        label: btn.textContent,
      };
      btn.classList.add("armed");
      btn.textContent = `Confirm ${state.arm.label}`;
      toast(`armed: ${command}`);
      setTimeout(() => {
        if (state.arm.button === btn && state.arm.expiresAt <= Date.now()) clearArmedButton();
      }, 3300);
      return;
    }
    clearArmedButton();
  }
  runCommand(command);
}

function clearArmedButton() {
  if (state.arm.button) {
    state.arm.button.classList.remove("armed");
    state.arm.button.textContent = state.arm.label;
  }
  state.arm = { command: "", expiresAt: 0, button: null, label: "" };
}

function drawRfChart(canvas, hardware) {
  if (!canvas) return;
  const rx = Number(hardware.rangepi_rx_packets || 0);
  const tx = Number(hardware.rangepi_tx_lines || 0);
  const errors = Number(hardware.rangepi_parse_errors || 0);
  drawBarChart(canvas, [
    ["RX", rx, "#67d8ee"],
    ["TX", tx, "#dfc56c"],
    ["ERR", errors, "#df5d67"],
  ], "RANGEPI ACTIVITY");
}

function renderPacketWaterfall(packets) {
  const canvas = $("packetWaterfall");
  if (!canvas) return;
  const newest = packets.slice(-160);
  const known = new Set(state.packets.waterfall.map((item) => item.key));
  newest.forEach((packet) => {
    const key = packetKey(packet);
    if (!known.has(key)) {
      state.packets.waterfall.push({
        key,
        time: packet.time,
        node: packet.node,
        type: packet.type,
        status: packet.status,
        route: packet.route,
        counter: packet.counter,
        color: packetColor(packet.type, packet.status),
        amp: packet.status === "rejected" ? 1 : Math.max(0.3, Math.min(1, String(packet.raw_hex || "").length / 96)),
      });
      known.add(key);
    }
  });
  state.packets.waterfall = state.packets.waterfall.slice(-220);
  drawPacketWaterfall(canvas, state.packets.waterfall);
}

function drawPacketWaterfall(canvas, rows) {
  const surface = scaleCanvas(canvas);
  const ctx = surface.ctx;
  const w = surface.width;
  const h = surface.height;
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#020506";
  ctx.fillRect(0, 0, w, h);
  ctx.strokeStyle = "#142126";
  ctx.lineWidth = 1;
  for (let y = 26; y < h; y += 22) {
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  }
  const visible = rows.slice(-Math.max(20, Math.floor((w - 36) / 7)));
  visible.forEach((packet, index) => {
    const x = 26 + index * ((w - 48) / Math.max(1, visible.length - 1));
    const lane = packetLane(packet.type);
    const y = 34 + lane * Math.max(22, (h - 62) / 5);
    const height = 12 + packet.amp * 34;
    ctx.globalAlpha = packet.status === "rejected" ? 0.95 : 0.72;
    ctx.strokeStyle = packet.color;
    ctx.lineWidth = packet.status === "rejected" ? 3 : 2;
    ctx.beginPath();
    ctx.moveTo(x, y - height / 2);
    ctx.lineTo(x, y + height / 2);
    ctx.stroke();
    ctx.globalAlpha = 0.18;
    ctx.fillStyle = packet.color;
    ctx.fillRect(x - 3, y - height / 2, 6, height);
    ctx.globalAlpha = 1;
  });
  ctx.fillStyle = "#f3f7fa";
  ctx.font = "12px Consolas";
  ctx.fillText("RX/TX FRAME WATERFALL", 10, 17);
  ctx.fillStyle = "#81909a";
  ctx.textAlign = "right";
  const latest = rows[rows.length - 1];
  ctx.fillText(latest ? `${latest.time} ${latest.node} ${latest.type}` : "waiting for packets", w - 10, 17);
  ctx.textAlign = "left";
}

function packetLane(type) {
  const value = String(type || "").toLowerCase();
  if (value.includes("ack")) return 1;
  if (value.includes("diagnostic")) return 2;
  if (value.includes("command") || value.includes("tx-")) return 3;
  if (value.includes("handshake") || value.includes("crypto")) return 4;
  if (value.includes("replay")) return 5;
  return 0;
}

function drawBarChart(canvas, rows, label) {
  const surface = scaleCanvas(canvas);
  const ctx = surface.ctx;
  const w = surface.width;
  const h = surface.height;
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#020506";
  ctx.fillRect(0, 0, w, h);
  ctx.fillStyle = "#f3f7fa";
  ctx.font = "12px Consolas";
  ctx.fillText(label, 10, 17);
  const max = Math.max(1, ...rows.map((row) => row[1]));
  rows.forEach(([name, value, color], index) => {
    const y = 38 + index * 34;
    const barW = (w - 96) * value / max;
    ctx.fillStyle = "#142126";
    ctx.fillRect(54, y, w - 86, 14);
    ctx.fillStyle = color;
    ctx.fillRect(54, y, barW, 14);
    ctx.fillStyle = "#c7d0d5";
    ctx.fillText(name, 10, y + 11);
    ctx.textAlign = "right";
    ctx.fillText(String(value), w - 12, y + 11);
    ctx.textAlign = "left";
  });
}

function wrapHex(hex, width = 64) {
  const text = String(hex || "");
  const lines = [];
  for (let index = 0; index < text.length; index += width) {
    lines.push(text.slice(index, index + width));
  }
  return lines.join("\n");
}

function openPacketDrawer(packet) {
  if (!packet) return;
  state.packets.selected = packetKey(packet);
  state.packets.drawerOpen = true;
  renderPacketDrawer(packet);
  $("packetDrawer")?.setAttribute("aria-hidden", "false");
  document.body.classList.add("packet-drawer-open");
}

function closePacketDrawer() {
  state.packets.drawerOpen = false;
  $("packetDrawer")?.setAttribute("aria-hidden", "true");
  document.body.classList.remove("packet-drawer-open");
}

function renderPacketDrawer(packet) {
  const sub = $("packetDrawerSub");
  const body = $("packetDrawerBody");
  if (!sub || !body) return;
  const rawHex = String(packet.raw_hex || "");
  const headerHex = rawHex.slice(0, 24);
  const payloadHex = rawHex.slice(24);
  sub.textContent = `${packet.time || "--"} / ${packet.node || "unknown"} / ${packet.type || "packet"}`;
  body.innerHTML = `
    <div class="decoder-status" style="--packet-color:${packetColor(packet.type, packet.status)}">
      <div>
        <span>${escapeHtml(packet.status || "unknown")}</span>
        <strong>${escapeHtml(packet.type || "packet")}</strong>
      </div>
      <div>
        <span>counter</span>
        <strong>${escapeHtml(packet.counter ?? "-")}</strong>
      </div>
    </div>
    <div class="decoder-grid">
      ${kv("node", packet.node || "-")}
      ${kv("route", packet.route || "-")}
      ${kv("session", packet.session || "-")}
      ${kv("time", packet.time || "-")}
      ${kv("raw bytes", rawHex ? Math.ceil(rawHex.length / 2) : "n/a")}
      ${kv("waterfall lane", packetLane(packet.type))}
    </div>
    <div class="decoder-section">
      <div class="decoder-title">Header</div>
      <pre>${escapeHtml(wrapHex(headerHex || "unavailable"))}</pre>
    </div>
    <div class="decoder-section">
      <div class="decoder-title">Payload / Ciphertext</div>
      <pre>${escapeHtml(wrapHex(payloadHex || rawHex || "No raw hex exposed for this packet."))}</pre>
    </div>
    <div class="decoder-section">
      <div class="decoder-title">Interpretation</div>
      <pre>${escapeHtml(packetInterpretation(packet))}</pre>
    </div>
  `;
}

function packetInterpretation(packet) {
  const lines = [
    `packet_type    ${packet.type || "unknown"}`,
    `status         ${packet.status || "unknown"}`,
    `route          ${packet.route || "unknown"}`,
  ];
  const type = String(packet.type || "").toLowerCase();
  if (type.includes("tx-")) lines.push("direction      ground -> node", "ack_expected   yes");
  else lines.push("direction      node -> ground");
  if (type.includes("ack")) lines.push("meaning        command acknowledgement frame");
  if (type.includes("telemetry")) lines.push("meaning        sensor/health downlink frame");
  if (type.includes("diagnostic")) lines.push("meaning        board diagnostic or fault mask frame");
  if (type.includes("replay") || packet.status === "rejected") lines.push("operator       replay guard rejected this counter/session");
  return lines.join("\n");
}

function renderFleetTable(nodes) {
  $("fleetTable").innerHTML = `
    <div class="fleet-row header">
      <span>Node</span><span>Role</span><span>Radio</span><span>Link</span><span>Health</span><span>Risk</span><span>Session</span>
    </div>
    ${nodes.map((node) => `
      <div class="fleet-row ${node.name === state.data.selected ? "active" : ""}" data-node="${escapeHtml(node.name)}">
        <span>${escapeHtml(node.name)}</span>
        <span>${escapeHtml(node.role)}</span>
        <span>${node.radio_id}</span>
        <span>${node.link_margin.toFixed(0)}%</span>
        <span>${node.health.toFixed(0)}%</span>
        <span>${node.risk.toFixed(0)}%</span>
        <span>${escapeHtml(node.session)}</span>
      </div>
    `).join("")}
  `;
  document.querySelectorAll(".fleet-row[data-node]").forEach((row) => {
    row.addEventListener("click", () => runCommand(`use ${row.dataset.node}`));
  });
}

function renderScheduler(nodes) {
  $("schedulerList").innerHTML = nodes.map((node) => {
    const interval = Math.max(1, node.telemetry_interval_s);
    const progress = Math.max(0, Math.min(100, ((interval - node.next_tx_s) / interval) * 100));
    const color = node.classifier_label.includes("PRIORITY") || node.classifier_label.includes("FAST") ? "#df5d67" :
      node.classifier_label.includes("WATCH") ? "#dfc56c" :
      node.classifier_label.includes("CONSERVE") || node.classifier_label.includes("SLOW") ? "#9db7e8" : "#c7d0d5";
    return `
      <div class="scheduler-row">
        <div class="scheduler-node">
          <div class="scheduler-name">${escapeHtml(node.name)}</div>
          <div class="scheduler-meta">${escapeHtml(node.classifier_label)} / ${escapeHtml(node.cadence_reason)}</div>
        </div>
        <div>
          <div>${node.telemetry_interval_s.toFixed(1)}s</div>
          <div class="cadence-bar"><span style="width:${progress.toFixed(0)}%; background:${color}"></span></div>
        </div>
        <div>${node.next_tx_s.toFixed(1)}s</div>
      </div>
    `;
  }).join("");
}

function renderBringup(data) {
  const score = readinessScore(data.bringup || []);
  const card = $("readinessCard");
  if (card) {
    card.classList.toggle("ready-pass", score.state === "PASS");
    card.classList.toggle("ready-warn", score.state === "WARN");
    card.classList.toggle("ready-fail", score.state === "FAIL");
  }
  $("readinessScore").textContent = `${score.percent}%`;
  drawReadinessGauge($("readinessGauge"), score.percent, score.state);
  $("bringupList").innerHTML = data.bringup.map((item) => `
    <div class="check-row">
      <span class="${sevClass(item.state)}">${escapeHtml(item.state)}</span>
      <strong>${escapeHtml(item.name)}</strong>
      <span>${escapeHtml(item.detail)}</span>
    </div>
  `).join("");
  const node = selectedNode();
  $("summaryText").textContent = JSON.stringify({
    time: data.time,
    uptime_s: data.uptime_s,
    mode: data.mode,
    transport: data.transport,
    selected: data.selected,
    target: data.target,
    accepted_frames: data.stats.accepted_frames,
    replay_rejects: data.stats.replay_rejects,
    node: node,
  }, null, 2);
}

function readinessScore(items) {
  if (!items.length) return { percent: 0, state: "FAIL" };
  const points = items.reduce((total, item) => {
    if (item.state === "PASS") return total + 1;
    if (item.state === "WARN") return total + 0.55;
    return total;
  }, 0);
  const hasFail = items.some((item) => item.state === "FAIL");
  const percent = Math.round((points / items.length) * 100);
  return {
    percent,
    state: hasFail ? "FAIL" : percent >= 86 ? "PASS" : "WARN",
  };
}

function drawTheaterFrame(now) {
  const canvas = $("theaterCanvas");
  if (!canvas) {
    requestAnimationFrame(drawTheaterFrame);
    return;
  }
  const surface = scaleCanvas(canvas);
  const ctx = surface.ctx;
  const w = surface.width;
  const h = surface.height;
  ctx.clearRect(0, 0, w, h);
  drawTheaterBackdrop(ctx, w, h, now);
  if (state.data) {
    drawTheaterConstellation(ctx, w, h, state.data, now);
  }
  requestAnimationFrame(drawTheaterFrame);
}

function drawTheaterBackdrop(ctx, w, h, now) {
  const gradient = ctx.createRadialGradient(w * 0.5, h * 0.48, 20, w * 0.5, h * 0.5, Math.max(w, h) * 0.72);
  gradient.addColorStop(0, "#102127");
  gradient.addColorStop(0.45, "#05090b");
  gradient.addColorStop(1, "#020304");
  ctx.fillStyle = gradient;
  ctx.fillRect(0, 0, w, h);
  state.theater.stars.forEach((star) => {
    const x = (star.x * w + (now * star.drift * 0.006)) % w;
    const y = star.y * h;
    ctx.globalAlpha = star.a * (0.72 + Math.sin(now * 0.001 + star.x * 10) * 0.28);
    ctx.fillStyle = "#dbeff6";
    ctx.beginPath();
    ctx.arc(x, y, star.r, 0, Math.PI * 2);
    ctx.fill();
  });
  ctx.globalAlpha = 1;
  ctx.strokeStyle = "rgba(103, 216, 238, 0.08)";
  ctx.lineWidth = 1;
  for (let radius = 90; radius < Math.max(w, h); radius += 82) {
    ctx.beginPath();
    ctx.ellipse(w * 0.48, h * 0.55, radius * 1.28, radius * 0.46, -0.18, 0, Math.PI * 2);
    ctx.stroke();
  }
}

function drawTheaterConstellation(ctx, w, h, data, now) {
  const nodes = data.nodes || [];
  const selected = selectedNode() || nodes[0];
  const cx = w * 0.47;
  const cy = h * 0.54;
  const earthR = Math.min(w, h) * 0.16;
  drawEarthDisc(ctx, cx, cy, earthR, now);
  const positions = new Map();
  nodes.forEach((node, index) => {
    const phase = Number.isFinite(Number(node.phase)) ? Number(node.phase) : index;
    const angle = (index / Math.max(1, nodes.length)) * Math.PI * 2 + now * 0.00018 + phase * 0.08;
    const radius = earthR * (1.22 + (index % 3) * 0.24);
    const x = cx + Math.cos(angle) * radius * 1.48;
    const y = cy + Math.sin(angle) * radius * 0.62;
    positions.set(node.name, { x, y, angle, radius });
  });
  nodes.forEach((node) => {
    if (node.name === selected?.name) return;
    const p = positions.get(node.name);
    const s = positions.get(selected?.name);
    if (!p || !s) return;
    ctx.strokeStyle = node.contact === "OPEN" ? "rgba(199, 208, 213, 0.20)" : "rgba(223, 93, 103, 0.26)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(s.x, s.y);
    ctx.lineTo(p.x, p.y);
    ctx.stroke();
  });
  drawTheaterPackets(ctx, positions, selected, now);
  nodes.forEach((node) => {
    const p = positions.get(node.name);
    if (p) drawTheaterNode(ctx, node, p, node.name === selected?.name, now);
  });
  drawTheaterSweep(ctx, cx, cy, Math.max(w, h), now);
  drawTheaterReadout(ctx, w, h, data, selected);
}

function drawEarthDisc(ctx, cx, cy, r, now) {
  const gradient = ctx.createRadialGradient(cx - r * 0.35, cy - r * 0.45, r * 0.1, cx, cy, r);
  gradient.addColorStop(0, "#6be5df");
  gradient.addColorStop(0.42, "#266c78");
  gradient.addColorStop(1, "#06171b");
  ctx.fillStyle = gradient;
  ctx.beginPath();
  ctx.arc(cx, cy, r, 0, Math.PI * 2);
  ctx.fill();
  ctx.strokeStyle = "rgba(238, 242, 244, 0.46)";
  ctx.lineWidth = 1.2;
  ctx.stroke();
  ctx.save();
  ctx.beginPath();
  ctx.arc(cx, cy, r * 0.98, 0, Math.PI * 2);
  ctx.clip();
  ctx.strokeStyle = "rgba(238, 242, 244, 0.13)";
  for (let i = -2; i <= 2; i += 1) {
    const offset = ((now * 0.012 + i * 34) % (r * 1.8)) - r * 0.9;
    ctx.beginPath();
    ctx.moveTo(cx - r, cy + offset);
    ctx.bezierCurveTo(cx - r * 0.25, cy + offset - 22, cx + r * 0.25, cy + offset + 22, cx + r, cy + offset - 6);
    ctx.stroke();
  }
  ctx.restore();
}

function drawTheaterNode(ctx, node, p, active, now) {
  const pulse = active ? 1 + Math.sin(now * 0.006) * 0.18 : 1;
  const r = active ? 11 * pulse : 7;
  ctx.strokeStyle = node.color;
  ctx.fillStyle = active ? "rgba(238, 242, 244, 0.15)" : "#030607";
  ctx.lineWidth = active ? 2.4 : 1.6;
  ctx.beginPath();
  ctx.arc(p.x, p.y, r + 10, 0, Math.PI * 2);
  ctx.stroke();
  ctx.beginPath();
  ctx.arc(p.x, p.y, r, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();
  ctx.fillStyle = node.color;
  ctx.beginPath();
  ctx.arc(p.x, p.y, 3, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = active ? "#ffffff" : "#c7d0d5";
  ctx.font = `${active ? "700 " : ""}11px Consolas`;
  ctx.fillText(node.name.replace(/-.*/, ""), p.x + 16, p.y + 4);
  const barW = 56;
  ctx.fillStyle = "rgba(5, 7, 8, 0.86)";
  ctx.fillRect(p.x - barW / 2, p.y + 20, barW, 5);
  ctx.fillStyle = node.color;
  ctx.fillRect(p.x - barW / 2, p.y + 20, barW * Math.max(0, Math.min(100, node.link_margin)) / 100, 5);
}

function drawTheaterPackets(ctx, positions, selected, now) {
  const selectedPos = positions.get(selected?.name);
  state.theater.packets = state.theater.packets.filter((packet) => now - packet.born < packet.duration);
  state.theater.packets.forEach((packet, index) => {
    const p = positions.get(packet.node) || selectedPos;
    if (!p || !selectedPos) return;
    const t = Math.max(0, Math.min(1, (now - packet.born) / packet.duration));
    const outbound = String(packet.type || "").startsWith("tx-");
    const from = outbound ? selectedPos : p;
    const to = outbound ? p : selectedPos;
    const x = from.x + (to.x - from.x) * t;
    const y = from.y + (to.y - from.y) * t - Math.sin(t * Math.PI) * (30 + (index % 4) * 9);
    ctx.globalAlpha = 1 - t * 0.1;
    ctx.fillStyle = packet.color;
    ctx.beginPath();
    ctx.arc(x, y, packet.rejected ? 5.5 : 4.2, 0, Math.PI * 2);
    ctx.fill();
    ctx.globalAlpha = 0.22 * (1 - t);
    ctx.strokeStyle = packet.color;
    ctx.lineWidth = packet.rejected ? 3 : 2;
    ctx.beginPath();
    ctx.arc(x, y, 15 + t * 34, 0, Math.PI * 2);
    ctx.stroke();
    ctx.globalAlpha = 1;
  });
}

function drawTheaterSweep(ctx, cx, cy, span, now) {
  const angle = now * 0.00055;
  const length = span * 0.46;
  const gradient = ctx.createLinearGradient(cx, cy, cx + Math.cos(angle) * length, cy + Math.sin(angle) * length);
  gradient.addColorStop(0, "rgba(103, 216, 238, 0.38)");
  gradient.addColorStop(1, "rgba(103, 216, 238, 0)");
  ctx.strokeStyle = gradient;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(cx + Math.cos(angle) * length, cy + Math.sin(angle) * length);
  ctx.stroke();
}

function drawTheaterReadout(ctx, w, h, data, selected) {
  const label = selected ? `${selected.name}  LINK ${selected.link_margin.toFixed(0)}%  TX ${selected.telemetry_interval_s.toFixed(1)}s` : "NO NODE";
  ctx.fillStyle = "rgba(2, 5, 6, 0.7)";
  ctx.fillRect(18, h - 54, Math.min(w - 36, 720), 34);
  ctx.strokeStyle = "rgba(199, 208, 213, 0.28)";
  ctx.strokeRect(18, h - 54, Math.min(w - 36, 720), 34);
  ctx.fillStyle = "#eef2f4";
  ctx.font = "12px Consolas";
  ctx.fillText(label, 168, h - 33);
  ctx.textAlign = "right";
  ctx.fillStyle = "#81909a";
  ctx.fillText(`${data.mode} / ${data.transport}`, w - 24, h - 24);
  ctx.textAlign = "left";
}

function drawReadinessGauge(canvas, percent, stateName) {
  if (!canvas) return;
  const surface = scaleCanvas(canvas);
  const ctx = surface.ctx;
  const w = surface.width;
  const h = surface.height;
  const cx = w / 2;
  const cy = h * 0.82;
  const radius = Math.min(w * 0.42, h * 0.74);
  const color = stateName === "PASS" ? "#8fe1ad" : stateName === "FAIL" ? "#df5d67" : "#dfc56c";
  ctx.clearRect(0, 0, w, h);
  ctx.lineWidth = 10;
  ctx.strokeStyle = "#172027";
  ctx.beginPath();
  ctx.arc(cx, cy, radius, Math.PI, Math.PI * 2);
  ctx.stroke();
  ctx.strokeStyle = color;
  ctx.beginPath();
  ctx.arc(cx, cy, radius, Math.PI, Math.PI + Math.PI * Math.max(0, Math.min(100, percent)) / 100);
  ctx.stroke();
  const angle = Math.PI + Math.PI * Math.max(0, Math.min(100, percent)) / 100;
  ctx.fillStyle = color;
  ctx.beginPath();
  ctx.arc(cx + Math.cos(angle) * radius, cy + Math.sin(angle) * radius, 5, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = "#eef2f4";
  ctx.font = "700 12px Consolas";
  ctx.textAlign = "center";
  ctx.fillText(stateName, cx, cy - 10);
  ctx.textAlign = "left";
}

function initLeafletMap() {
  if (state.leaflet.initialized) return state.leaflet.available;
  state.leaflet.initialized = true;
  const mapEl = $("realMap");
  const fallback = $("mapCanvas");
  if (!window.L || !mapEl) {
    if (mapEl) mapEl.style.display = "none";
    if (fallback) fallback.style.display = "block";
    state.leaflet.available = false;
    return false;
  }

  state.leaflet.available = true;
  if (fallback) fallback.style.display = "none";
  state.leaflet.map = L.map(mapEl, {
    zoomControl: true,
    doubleClickZoom: false,
    preferCanvas: false,
    attributionControl: true,
    minZoom: 5,
    maxZoom: 19,
  }).setView([37.78, -122.33], 10);
  state.leaflet.overlay = L.layerGroup().addTo(state.leaflet.map);
  setBaseLayer("dark");
  state.leaflet.map.on("dragstart zoomstart", () => {
    state.leaflet.didFit = true;
  });
  return true;
}

function setBaseLayer(layer) {
  if (!state.leaflet.map || !window.L) return;
  if (state.leaflet.baseLayer) {
    state.leaflet.map.removeLayer(state.leaflet.baseLayer);
  }
  state.leaflet.layer = layer;
  if (layer === "street") {
    state.leaflet.baseLayer = L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
      maxZoom: 19,
      attribution: "&copy; OpenStreetMap contributors",
    });
    $("mapLayer").textContent = "Dark";
  } else {
    state.leaflet.baseLayer = L.tileLayer("https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png", {
      maxZoom: 20,
      attribution: "&copy; OpenStreetMap &copy; CARTO",
    });
    $("mapLayer").textContent = "Street";
  }
  state.leaflet.baseLayer.addTo(state.leaflet.map);
}

function renderMap(data) {
  if (!initLeafletMap()) return;
  const selected = selectedNode();
  if (!selected || !state.leaflet.overlay) return;

  state.leaflet.overlay.clearLayers();
  const bounds = [];
  const selectedLatLng = [selected.map_latitude, selected.map_longitude];

  data.nodes.forEach((node) => {
    const latlng = [node.map_latitude, node.map_longitude];
    bounds.push(latlng);
    if (node.name !== selected.name) {
      L.polyline([selectedLatLng, latlng], {
        color: node.contact === "OPEN" ? "rgba(220, 226, 230, 0.52)" : "rgba(223, 93, 103, 0.45)",
        weight: node.name === state.hoverNode ? 2 : 1,
        opacity: 0.9,
        interactive: false,
      }).addTo(state.leaflet.overlay);
    }
  });

  data.nodes.forEach((node) => {
    const active = node.name === selected.name;
    const marker = L.marker([node.map_latitude, node.map_longitude], {
      icon: L.divIcon({
        className: "node-div-icon",
        iconSize: [140, 26],
        iconAnchor: [10, 10],
        html: `
          <div class="node-icon-wrap">
            <div class="node-marker ${active ? "selected" : ""}" style="--marker-color:${node.color}"></div>
            <div class="map-label">${escapeHtml(node.name.split("-")[0])}</div>
          </div>
        `,
      }),
      riseOnHover: true,
      title: node.name,
    }).addTo(state.leaflet.overlay);
    marker.bindTooltip(
      `<strong>${escapeHtml(node.name)}</strong><br>link ${node.link_margin.toFixed(0)}% / risk ${node.risk.toFixed(0)}%<br>${escapeHtml(node.classifier_label)} / tx ${node.telemetry_interval_s.toFixed(1)}s<br>${node.satellites} sats / HDOP ${node.hdop.toFixed(2)}`,
      { className: "node-tooltip", direction: "top", offset: [0, -12], opacity: 0.96 }
    );
    marker.on("click", () => runCommand(`use ${node.name}`));
    marker.on("dblclick", () => {
      state.leaflet.map.flyTo([node.map_latitude, node.map_longitude], Math.max(14, state.leaflet.map.getZoom()), { duration: 0.55 });
      runCommand(`use ${node.name}`);
    });
    marker.on("mouseover", () => {
      state.hoverNode = node.name;
    });
    marker.on("mouseout", () => {
      state.hoverNode = null;
    });
  });

  if (!state.leaflet.didFit && bounds.length) {
    state.leaflet.map.fitBounds(bounds, { padding: [72, 72], maxZoom: 12, animate: false });
    state.leaflet.didFit = true;
  }
}

function focusSelectedOnMap() {
  const node = selectedNode();
  if (!node || !state.leaflet.map) return;
  state.leaflet.map.flyTo([node.map_latitude, node.map_longitude], Math.max(14, state.leaflet.map.getZoom()), { duration: 0.65 });
}

function resetMapView() {
  if (!state.data || !state.leaflet.map) return;
  const bounds = state.data.nodes.map((node) => [node.map_latitude, node.map_longitude]);
  state.leaflet.map.fitBounds(bounds, { padding: [72, 72], maxZoom: 12, animate: true, duration: 0.6 });
}

function drawDonut(canvas, value, label, color, inverse) {
  const surface = scaleCanvas(canvas);
  const ctx = surface.ctx;
  const w = surface.width;
  const h = surface.height;
  ctx.clearRect(0, 0, w, h);
  const pct = Math.max(0, Math.min(100, value));
  const cx = w / 2;
  const cy = h / 2;
  const radius = Math.min(w, h) * 0.35;
  ctx.lineWidth = 7;
  ctx.strokeStyle = "#18262b";
  ctx.beginPath();
  ctx.arc(cx, cy, radius, 0, Math.PI * 2);
  ctx.stroke();
  ctx.strokeStyle = inverse ? (pct > 60 ? "#ff4b57" : "#f1d36b") : color;
  ctx.beginPath();
  ctx.arc(cx, cy, radius, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * pct / 100);
  ctx.stroke();
  ctx.fillStyle = "#f3f7fa";
  ctx.font = "700 15px Consolas";
  ctx.textAlign = "center";
  ctx.fillText(`${pct.toFixed(0)}%`, cx, cy + 2);
  ctx.fillStyle = "#90a0a8";
  ctx.font = "10px Consolas";
  ctx.fillText(label, cx, cy + 18);
}

function renderCharts(node) {
  if (!node || !node.history) return;
  $("graphWindow").textContent = `${state.graph.window} samples`;
  drawSpark($("chartLink"), node.history.link, "LINK MARGIN", "#67d8ee", "%", node.history_ma?.link);
  drawSpark($("chartRisk"), node.history.risk, "SECURITY RISK", "#df5d67", "%", node.history_ma?.risk);
  drawSpark($("chartInterval"), node.history.interval, "TX INTERVAL", "#c7d0d5", "s", node.history_ma?.interval, { invert: true });
  drawSpark($("chartAnomaly"), node.history.anomaly, "CLASSIFIER SCORE", "#dfc56c", "", node.history_ma?.anomaly);
  drawSpark($("chartFleetLink"), node.history.link, "LINK MARGIN", "#67d8ee", "%", node.history_ma?.link);
  drawSpark($("chartFleetTemp"), node.history.temp, "TEMPERATURE", "#dfc56c", "C", node.history_ma?.temp);
  drawSpark($("chartFleetRisk"), node.history.risk, "RISK", "#df5d67", "%", node.history_ma?.risk);
  drawSpark($("chartFleetPackets"), node.history.packets, "PACKET RATE", "#8fe1ad", "/s", node.history_ma?.packets);
}

function drawSpark(canvas, values, label, color, suffix, maValues = null, options = {}) {
  if (!canvas) return;
  const surface = scaleCanvas(canvas);
  const ctx = surface.ctx;
  const w = surface.width;
  const h = surface.height;
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#020506";
  ctx.fillRect(0, 0, w, h);
  ctx.strokeStyle = "#142126";
  ctx.lineWidth = 1;
  for (let x = 0; x < w; x += 38) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, h);
    ctx.stroke();
  }
  for (let y = 0; y < h; y += 28) {
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  }
  if (!values || values.length < 2) return;
  const windowSize = Math.max(state.graph.min, Math.min(state.graph.max, state.graph.window));
  const raw = values.slice(-windowSize).map(Number);
  const ma = maValues ? maValues.slice(-windowSize).map(Number) : null;
  const combined = ma ? raw.concat(ma) : raw;
  const min = Math.min(...combined);
  const max = Math.max(...combined);
  const span = Math.max(1, max - min);
  drawSeries(ctx, raw, min, span, w, h, color, 1.4, 0.62);
  if (ma) {
    drawSeries(ctx, ma, min, span, w, h, "#eef2f4", 2.0, 0.9);
  }
  ctx.fillStyle = "#f3f7fa";
  ctx.font = "12px Consolas";
  ctx.fillText(label, 10, 17);
  ctx.fillStyle = color;
  ctx.textAlign = "right";
  const latest = raw[raw.length - 1];
  const avg = ma ? ma[ma.length - 1] : latest;
  ctx.fillText(`${latest.toFixed(1)}${suffix}  MA ${avg.toFixed(1)}${suffix}`, w - 10, 17);
  if (options.invert) {
    ctx.fillStyle = "#81909a";
    ctx.fillText("lower is faster", w - 10, h - 9);
  }
  ctx.textAlign = "left";
}

function drawSeries(ctx, values, min, span, w, h, color, width, alpha) {
  ctx.save();
  ctx.globalAlpha = alpha;
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.beginPath();
  values.forEach((value, index) => {
    const x = 12 + (w - 24) * index / Math.max(1, values.length - 1);
    const y = h - 22 - ((value - min) / span) * (h - 46);
    if (index === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();
  ctx.restore();
}

function scaleCanvas(canvas) {
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  const width = Math.max(1, Math.floor(rect.width * dpr));
  const height = Math.max(1, Math.floor(rect.height * dpr));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, width: rect.width, height: rect.height };
}

function mapProject(lat, lon, rect) {
  const centerLat = 37.70;
  const centerLon = -122.25;
  const baseScale = Math.min(rect.width, rect.height) * 1.18 * state.map.zoom;
  const x = rect.width / 2 + (lon - centerLon) * baseScale * 1.22 + state.map.panX;
  const y = rect.height / 2 - (lat - centerLat) * baseScale * 1.55 + state.map.panY;
  return { x, y };
}

function screenToMapDistance() {
  return Math.max(10, 15 * state.map.zoom);
}

function drawMap() {
  const canvas = $("mapCanvas");
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  if (rect.width <= 0 || rect.height <= 0) {
    requestAnimationFrame(drawMap);
    return;
  }
  if (canvas.width !== Math.floor(rect.width * dpr) || canvas.height !== Math.floor(rect.height * dpr)) {
    canvas.width = Math.floor(rect.width * dpr);
    canvas.height = Math.floor(rect.height * dpr);
  }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, rect.width, rect.height);
  state.map.zoom += (state.map.targetZoom - state.map.zoom) * 0.12;
  state.map.panX += (state.map.targetPanX - state.map.panX) * 0.12;
  state.map.panY += (state.map.targetPanY - state.map.panY) * 0.12;
  drawBasemap(ctx, rect);
  if (state.data) drawNodes(ctx, rect, state.data.nodes);
  requestAnimationFrame(drawMap);
}

function drawBasemap(ctx, rect) {
  const w = rect.width;
  const h = rect.height;
  ctx.fillStyle = "#050606";
  ctx.fillRect(0, 0, w, h);
  ctx.strokeStyle = "rgba(92, 231, 255, 0.08)";
  ctx.lineWidth = 1;
  const grid = 42 * state.map.zoom;
  for (let x = (state.map.panX % grid); x < w; x += grid) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, h);
    ctx.stroke();
  }
  for (let y = (state.map.panY % grid); y < h; y += grid) {
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  }
  const water = [
    [37.96, -122.52], [37.88, -122.46], [37.82, -122.43], [37.78, -122.40],
    [37.72, -122.36], [37.64, -122.33], [37.55, -122.27], [37.42, -122.18],
    [37.40, -122.03], [37.55, -122.05], [37.68, -122.12], [37.80, -122.20],
    [37.91, -122.32], [38.05, -122.39],
  ];
  drawPolygon(ctx, rect, water, "rgba(120, 136, 142, 0.28)", "rgba(210, 222, 228, 0.22)");
  const eastRidge = [[38.08, -122.05], [37.99, -122.03], [37.88, -122.05], [37.73, -122.03], [37.58, -121.95], [37.34, -121.85]];
  const peninsula = [[37.88, -122.50], [37.72, -122.47], [37.55, -122.38], [37.33, -122.12]];
  const roads = [
    [[37.78, -122.42], [37.80, -122.32], [37.84, -122.28], [37.87, -122.27]],
    [[37.77, -122.42], [37.70, -122.36], [37.62, -122.30], [37.51, -122.25], [37.34, -121.89]],
    [[37.80, -122.27], [37.79, -122.18], [37.81, -122.05]],
    [[37.97, -122.53], [37.90, -122.45], [37.83, -122.36], [37.78, -122.28]],
  ];
  drawLine(ctx, rect, eastRidge, "rgba(235, 239, 241, 0.17)", 2);
  drawLine(ctx, rect, peninsula, "rgba(235, 239, 241, 0.13)", 2);
  roads.forEach((line) => drawLine(ctx, rect, line, "rgba(245, 249, 250, 0.35)", 1.3));
  drawLabels(ctx, rect);
}

function drawPolygon(ctx, rect, coords, fill, stroke) {
  ctx.beginPath();
  coords.forEach(([lat, lon], index) => {
    const p = mapProject(lat, lon, rect);
    if (index === 0) ctx.moveTo(p.x, p.y);
    else ctx.lineTo(p.x, p.y);
  });
  ctx.closePath();
  ctx.fillStyle = fill;
  ctx.fill();
  ctx.strokeStyle = stroke;
  ctx.stroke();
}

function drawLine(ctx, rect, coords, color, width) {
  ctx.beginPath();
  coords.forEach(([lat, lon], index) => {
    const p = mapProject(lat, lon, rect);
    if (index === 0) ctx.moveTo(p.x, p.y);
    else ctx.lineTo(p.x, p.y);
  });
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.stroke();
}

function drawLabels(ctx, rect) {
  const labels = [
    ["San Francisco", 37.7749, -122.4194],
    ["Berkeley", 37.8715, -122.2730],
    ["Oakland", 37.8044, -122.2712],
    ["San Jose", 37.3382, -121.8863],
    ["Marin", 37.9735, -122.5311],
    ["Bay", 37.70, -122.32],
  ];
  ctx.font = "12px Segoe UI";
  ctx.fillStyle = "rgba(232, 238, 241, 0.48)";
  labels.forEach(([label, lat, lon]) => {
    const p = mapProject(lat, lon, rect);
    ctx.fillText(label, p.x + 7, p.y - 7);
  });
}

function drawNodes(ctx, rect, nodes) {
  const selected = selectedNode();
  if (!selected) return;
  const selectedPoint = mapProject(selected.map_latitude, selected.map_longitude, rect);
  nodes.forEach((node) => {
    if (node.name === selected.name) return;
    const p = mapProject(node.map_latitude, node.map_longitude, rect);
    ctx.beginPath();
    ctx.moveTo(selectedPoint.x, selectedPoint.y);
    ctx.lineTo(p.x, p.y);
    ctx.strokeStyle = node.contact === "OPEN" ? "rgba(255,255,255,0.48)" : "rgba(255,75,87,0.28)";
    ctx.lineWidth = 1.2;
    ctx.stroke();
  });
  state.nodeHit = [];
  nodes.forEach((node) => {
    const p = mapProject(node.map_latitude, node.map_longitude, rect);
    const radius = node.name === selected.name ? 9 : 7;
    state.nodeHit.push({ node, x: p.x, y: p.y, radius: screenToMapDistance() });
    ctx.strokeStyle = node.color;
    ctx.fillStyle = "#020405";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(p.x, p.y, radius + 9, 0, Math.PI * 2);
    ctx.stroke();
    ctx.beginPath();
    ctx.arc(p.x, p.y, radius, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();
    ctx.fillStyle = node.color;
    ctx.beginPath();
    ctx.arc(p.x, p.y, 3, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = node.color;
    ctx.font = "11px Consolas";
    ctx.fillText(node.name.replace(/-.*/, ""), p.x + 15, p.y + 4);
    if (state.hoverNode === node.name) {
      drawTooltip(ctx, p.x, p.y, node);
    }
  });
}

function drawTooltip(ctx, x, y, node) {
  const lines = [node.name, `link ${node.link_margin.toFixed(0)}%  risk ${node.risk.toFixed(0)}%`, `${node.satellites} sats  ${node.hdop.toFixed(2)} hdop`];
  const width = 188;
  const height = 62;
  ctx.fillStyle = "rgba(2, 6, 7, 0.94)";
  ctx.strokeStyle = "rgba(92, 231, 255, 0.5)";
  ctx.fillRect(x + 16, y - height - 10, width, height);
  ctx.strokeRect(x + 16, y - height - 10, width, height);
  ctx.font = "12px Consolas";
  lines.forEach((line, index) => {
    ctx.fillStyle = index === 0 ? node.color : "#c9d5dc";
    ctx.fillText(line, x + 26, y - height + 10 + index * 17);
  });
}

function setupMapEvents() {
  const canvas = $("mapCanvas");
  canvas.addEventListener("wheel", (event) => {
    event.preventDefault();
    const delta = event.deltaY < 0 ? 1.12 : 0.88;
    state.map.targetZoom = Math.max(0.65, Math.min(5.0, state.map.targetZoom * delta));
  }, { passive: false });
  canvas.addEventListener("mousemove", (event) => {
    const rect = canvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    if (state.map.dragging && state.map.dragStart) {
      state.map.targetPanX = state.map.dragStart.panX + (x - state.map.dragStart.x);
      state.map.targetPanY = state.map.dragStart.panY + (y - state.map.dragStart.y);
    }
    const hit = (state.nodeHit || []).find((item) => Math.hypot(item.x - x, item.y - y) < item.radius);
    state.hoverNode = hit ? hit.node.name : null;
  });
  canvas.addEventListener("mousedown", (event) => {
    const rect = canvas.getBoundingClientRect();
    state.map.dragging = true;
    state.map.dragStart = {
      x: event.clientX - rect.left,
      y: event.clientY - rect.top,
      panX: state.map.targetPanX,
      panY: state.map.targetPanY,
    };
  });
  window.addEventListener("mouseup", () => {
    state.map.dragging = false;
    state.map.dragStart = null;
  });
  canvas.addEventListener("click", (event) => {
    const rect = canvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    const hit = (state.nodeHit || []).find((item) => Math.hypot(item.x - x, item.y - y) < item.radius);
    if (hit) runCommand(`use ${hit.node.name}`);
  });
  canvas.addEventListener("dblclick", (event) => {
    event.preventDefault();
    const rect = canvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const y = event.clientY - rect.top;
    const hit = (state.nodeHit || []).find((item) => Math.hypot(item.x - x, item.y - y) < item.radius);
    if (!hit) {
      state.map.targetZoom = 1.0;
      state.map.targetPanX = 0;
      state.map.targetPanY = 0;
      return;
    }
    if (state.map.lastClickNode === hit.node.name && state.map.targetZoom > 2.2) {
      state.map.targetZoom = 1.0;
      state.map.targetPanX = 0;
      state.map.targetPanY = 0;
      state.map.lastClickNode = null;
    } else {
      state.map.lastClickNode = hit.node.name;
      state.map.targetZoom = 3.0;
      state.map.targetPanX += rect.width / 2 - x;
      state.map.targetPanY += rect.height / 2 - y;
      runCommand(`use ${hit.node.name}`);
    }
  });
}

function bindUi() {
  const initialView = location.hash.replace("#", "");
  if (initialView) selectView(initialView);
  document.querySelectorAll(".rail-btn").forEach((btn) => btn.addEventListener("click", () => selectView(btn.dataset.view)));
  document.querySelectorAll("[data-command]").forEach((btn) => btn.addEventListener("click", () => handleCommandButton(btn)));
  $("packetDrawerClose")?.addEventListener("click", closePacketDrawer);
  $("commandComposer")?.addEventListener("submit", (event) => {
    event.preventDefault();
    submitComposer();
  });
  ["composerTarget", "composerCommand", "composerArg", "composerAuth"].forEach((id) => {
    $(id)?.addEventListener("change", updateComposerPreview);
  });
  $("mapFocus").addEventListener("click", focusSelectedOnMap);
  $("mapReset").addEventListener("click", resetMapView);
  $("mapLayer").addEventListener("click", () => setBaseLayer(state.leaflet.layer === "dark" ? "street" : "dark"));
  $("graphZoomIn").addEventListener("click", () => {
    state.graph.window = Math.max(state.graph.min, Math.round(state.graph.window * 0.72));
    renderCharts(selectedNode());
  });
  $("graphZoomOut").addEventListener("click", () => {
    state.graph.window = Math.min(state.graph.max, Math.round(state.graph.window * 1.35));
    renderCharts(selectedNode());
  });
  bindTerminal("commandForm", "commandInput");
  bindTerminal("commandFormMain", "commandInputMain");
  bindTerminal("commandFormFull", "commandInputFull");
  $("addNodeForm").addEventListener("submit", (event) => {
    event.preventDefault();
    const name = $("addNodeName").value.trim();
    const role = $("addNodeRole").value.trim() || "sensor-node";
    const lat = $("addNodeLat").value.trim();
    const lon = $("addNodeLon").value.trim();
    const radio = $("addNodeRadio").value.trim();
    const session = $("addNodeSession").value.trim();
    if (!name) return toast("node name required");
    const args = [`addnode ${name} ${role}`];
    if (lat) args.push(`lat=${lat}`);
    if (lon) args.push(`lon=${lon}`);
    if (radio) args.push(`radio=${radio}`);
    if (session) args.push(`session=${session}`);
    ["addNodeName", "addNodeRole", "addNodeLat", "addNodeLon", "addNodeRadio", "addNodeSession"].forEach((id) => {
      $(id).value = "";
    });
    runCommand(args.join(" "));
  });
  $("deleteSelected").addEventListener("click", () => {
    const node = selectedNode();
    if (node && window.confirm(`Delete ${node.name} from the groundstation registry?`)) runCommand(`delnode ${node.name}`);
  });
  if (!initLeafletMap()) {
    setupMapEvents();
    requestAnimationFrame(drawMap);
  }
  document.addEventListener("keydown", (event) => {
    if (event.ctrlKey && event.key === "`") {
      event.preventDefault();
      selectView("terminal");
      setTimeout(() => $("commandInputFull")?.focus(), 60);
    }
  });
}

function bindTerminal(formId, inputId) {
  const form = $(formId);
  const input = $(inputId);
  if (!form || !input) return;
  form.addEventListener("submit", (event) => {
    event.preventDefault();
    const command = input.value;
    input.value = "";
    runCommand(command);
  });
  input.addEventListener("keydown", (event) => {
    if (event.key === "ArrowUp") {
      event.preventDefault();
      if (!state.terminal.history.length) return;
      state.terminal.historyIndex = Math.max(0, state.terminal.historyIndex - 1);
      input.value = state.terminal.history[state.terminal.historyIndex] || "";
      queueMicrotask(() => input.setSelectionRange(input.value.length, input.value.length));
    } else if (event.key === "ArrowDown") {
      event.preventDefault();
      if (!state.terminal.history.length) return;
      state.terminal.historyIndex = Math.min(state.terminal.history.length, state.terminal.historyIndex + 1);
      input.value = state.terminal.history[state.terminal.historyIndex] || "";
      queueMicrotask(() => input.setSelectionRange(input.value.length, input.value.length));
    } else if (event.key === "Tab") {
      event.preventDefault();
      const current = input.value.toLowerCase();
      const match = state.terminal.completions.find((item) => item.startsWith(current));
      if (match) input.value = match;
    }
  });
}

bindUi();
fetchState().catch((error) => toast(`state error: ${error}`));
setInterval(() => fetchState().catch((error) => toast(`state error: ${error}`)), 900);
