"use strict";
const TEXT = {
  es: {
    drop: "Arrastra videos o carpetas", addFiles: "Videos", addFolder: "Carpeta", add: "Agregar",
    convert: "Convertir", send: "Enviar a la Vita", cancel: "Cancelar", port: "Puerto",
    test: "Conectar", options: "Opciones", output: "Copias convertidas", keep: "Conservar copias",
    analysing: "Analizando", ready: "Listo", encode_audio: "Audio", encode_video: "Video", remux: "Formato",
    unsupported: "No compatible", converting: "Convirtiendo", sending: "Enviando", done: "Convertido",
    sent: "En la Vita ✓", failed: "Error", connected: "Conectado", noHost: "Falta la IP",
    noFfmpeg: "Falta ffmpeg", finished: "Listo", cancelled: "Cancelado", empty: "Sin carpetas", subs: "SRT",
    count: (n, c, s) => `${n} ${n === 1 ? "video" : "videos"} · ${c} a convertir · ${s}`, nothing: "",
  },
  en: {
    drop: "Drop videos or folders", addFiles: "Videos", addFolder: "Folder", add: "Add",
    convert: "Convert", send: "Send to Vita", cancel: "Cancel", port: "Port",
    test: "Connect", options: "Options", output: "Converted copies", keep: "Keep copies",
    analysing: "Analysing", ready: "Ready", encode_audio: "Audio", encode_video: "Video", remux: "Format",
    unsupported: "Unsupported", converting: "Converting", sending: "Sending", done: "Converted",
    sent: "On Vita ✓", failed: "Error", connected: "Connected", noHost: "IP missing",
    noFfmpeg: "ffmpeg missing", finished: "Done", cancelled: "Cancelled", empty: "No folders", subs: "SRT",
    count: (n, c, s) => `${n} ${n === 1 ? "video" : "videos"} · ${c} to convert · ${s}`, nothing: "",
  },
};
const $ = (s) => document.querySelector(s);
const state = { settings: { lang: "es" }, items: new Map(), busy: false, ffmpeg: true };
let api = null;
const t = (k) => TEXT[state.settings.lang || "es"][k];

function size(bytes) {
  return bytes > 1e9 ? (bytes / 1e9).toFixed(1) + " GB" : (bytes / 1e6).toFixed(0) + " MB";
}
function clock(sec) {
  sec = Math.round(sec || 0);
  const h = Math.floor(sec / 3600), m = Math.floor(sec / 60) % 60, s = sec % 60;
  return (h ? h + ":" + String(m).padStart(2, "0") : String(m).padStart(2, "0")) + ":" + String(s).padStart(2, "0");
}
function notice(text, bad) {
  const n = $("#notice");
  n.textContent = text; n.hidden = !text; n.classList.toggle("bad", !!bad);
  clearTimeout(notice.timer);
  if (text) notice.timer = setTimeout(() => (n.hidden = true), 6000);
}

function translate() {
  document.documentElement.lang = state.settings.lang;
  document.querySelectorAll("[data-t]").forEach((el) => (el.textContent = t(el.dataset.t)));
  document.querySelectorAll("[data-lang]").forEach((b) => b.classList.toggle("on", b.dataset.lang === state.settings.lang));
  render();
}

function stateLabel(item) {
  const pct = Math.round((item.progress || 0) * 100);
  if (item.status === "converting" || item.status === "sending") return [`${t(item.status)} ${pct}%`, ""];
  if (item.status === "sent" || item.status === "ready") return [t(item.status), "good"];
  if (item.status === "failed" || item.status === "unsupported") return [t(item.status), "bad"];
  return [t(item.status) || item.status, ""];
}

function render() {
  const items = [...state.items.values()];
  $("#drop").hidden = items.length > 0;
  $("#list").hidden = items.length === 0;
  const groups = new Map();
  items.forEach((i) => { if (!groups.has(i.folder)) groups.set(i.folder, []); groups.get(i.folder).push(i); });
  const list = $("#list");
  list.replaceChildren();
  for (const [folder, members] of groups) {
    const group = document.createElement("div");
    group.className = "group";
    const head = document.createElement("div");
    head.className = "group-head";
    head.innerHTML = `<img src="img/folder.png" alt=""><input spellcheck="false">`;
    const input = head.querySelector("input");
    input.value = folder;
    input.disabled = state.busy;
    input.addEventListener("change", async () => {
      const name = api ? await api.rename_folder(folder, input.value) : input.value;
      members.forEach((m) => (m.folder = name));
      render();
    });
    group.append(head);
    for (const item of members) {
      const row = document.createElement("div");
      row.className = "item";
      const [label, tone] = stateLabel(item);
      const info = [];
      if (item.video) info.push(`${item.video.toUpperCase()} ${item.width}×${item.height}`);
      if (item.audio) info.push(item.audio.toUpperCase());
      if (item.duration) info.push(clock(item.duration));
      info.push(size(item.size));
      if (item.subtitles) info.push(t("subs"));
      if (item.detail && item.status === "failed") info.push(item.detail);
      row.innerHTML = `<img alt=""><div><div class="name"></div><div class="info"></div></div><div class="state"></div><button class="remove" title="×">×</button><div class="track"><div></div></div>`;
      row.querySelector("img").src = item.status === "sent" ? "img/check.png" : item.status === "unsupported" || item.status === "failed" ? "img/back.png" : "img/play.png";
      row.querySelector(".name").textContent = item.name;
      row.querySelector(".info").textContent = info.join(" · ");
      const st = row.querySelector(".state");
      st.textContent = label; if (tone) st.classList.add(tone);
      row.querySelector(".track > div").style.width = Math.round((item.progress || 0) * 100) + "%";
      row.querySelector(".track").style.visibility = ["converting", "sending", "done", "sent"].includes(item.status) ? "visible" : "hidden";
      row.querySelector(".remove").disabled = state.busy;
      row.querySelector(".remove").addEventListener("click", () => { if (api) api.remove(item.id); state.items.delete(item.id); render(); });
      row.dataset.id = item.id;
      group.append(row);
    }
    list.append(group);
  }
  const convert = items.filter((i) => ["encode_audio", "encode_video", "remux"].includes(i.status)).length;
  $("#summary").textContent = items.length ? t("count")(items.length, convert, size(items.reduce((a, i) => a + i.size, 0))) : t("nothing");
  document.querySelector('[data-action="cancel"]').hidden = !state.busy;
  document.querySelectorAll('[data-action="convert"],[data-action="send"],[data-action="clear"],[data-action="files"],[data-action="folder"]')
    .forEach((b) => (b.disabled = state.busy || (["convert", "send"].includes(b.dataset.action) && !items.length)));
}

function progressOnly(data) {
  const item = state.items.get(data.id);
  if (!item) return;
  item.progress = data.progress; item.status = data.status;
  const row = document.querySelector(`.item[data-id="${data.id}"]`);
  if (!row) return render();
  row.querySelector(".track").style.visibility = "visible";
  row.querySelector(".track > div").style.width = Math.round(data.progress * 100) + "%";
  row.querySelector(".state").textContent = stateLabel(item)[0];
}

window.kirv = {
  on(event, data) {
    if (event === "item") { state.items.set(data.id, data); render(); }
    else if (event === "progress") progressOnly(data);
    else if (event === "error") notice(data, true);
    else if (event === "finished") { state.busy = false; render(); notice(data.cancelled ? t("cancelled") : t("finished")); }
  },
};

async function saveSettings() {
  const values = { host: $("#host").value.trim(), port: parseInt($("#port").value, 10) || 1337,
    output: $("#output").value.trim(), keep: $("#keep").checked, lang: state.settings.lang };
  state.settings = api ? await api.save_settings(values) : Object.assign(state.settings, values);
}

async function testVita() {
  await saveSettings();
  const status = $("#vitaStatus"), folders = $("#vitaFolders");
  folders.replaceChildren();
  if (!state.settings.host) { status.textContent = t("noHost"); status.className = "small"; return; }
  status.textContent = "…";
  const result = api ? await api.vita_folders() : { ok: true, folders: ["Mirai Nikki", "Pruebas"] };
  status.className = "small " + (result.ok ? "" : "bad");
  status.textContent = result.ok ? t("connected") : result.error;
  status.style.color = result.ok ? "var(--good)" : "var(--bad)";
  (result.folders || []).forEach((f) => {
    const li = document.createElement("li");
    li.innerHTML = `<img src="img/folder.png" alt=""><span></span>`;
    li.querySelector("span").textContent = f;
    folders.append(li);
  });
  if (result.ok && !(result.folders || []).length) folders.textContent = t("empty");
  return result.ok;
}

async function start(send) {
  await saveSettings();
  if (!state.ffmpeg) return notice(t("noFfmpeg"), true);
  if (send && !(await testVita())) return;
  state.busy = api ? await api.start(send) : false;
  render();
}

document.addEventListener("click", async (e) => {
  const b = e.target.closest("[data-action],[data-lang]");
  if (!b || b.disabled) return;
  if (b.dataset.lang) { state.settings.lang = b.dataset.lang; await saveSettings(); return translate(); }
  const a = b.dataset.action;
  if (a === "files" && api) api.pick(false);
  if (a === "folder" && api) api.pick(true);
  if (a === "clear" && api) { await api.clear(); state.items = new Map((await api.state()).items.map((i) => [i.id, i])); render(); }
  if (a === "test") testVita();
  if (a === "convert") start(false);
  if (a === "send") start(true);
  if (a === "cancel" && api) api.cancel();
  if (a === "studio" && api) api.open_studio();
});
["host", "port", "output", "keep"].forEach((id) => $("#" + id).addEventListener("change", saveSettings));
["dragenter", "dragover"].forEach((ev) => document.addEventListener(ev, (e) => { e.preventDefault(); $("#drop").classList.add("hover"); $("#list").classList.add("hover"); }));
["dragleave", "drop"].forEach((ev) => document.addEventListener(ev, () => { $("#drop").classList.remove("hover"); $("#list").classList.remove("hover"); }));

function tick() { const d = new Date(); $("#clock").textContent = String(d.getHours()).padStart(2, "0") + ":" + String(d.getMinutes()).padStart(2, "0"); }
setInterval(tick, 10000); tick();

async function init() {
  if (window.pywebview && window.pywebview.api && window.pywebview.api.state) api = window.pywebview.api;
  const s = api ? await api.state() : demo();
  state.items.clear();
  state.settings = s.settings; state.ffmpeg = s.ffmpeg; state.busy = s.busy;
  s.items.forEach((i) => state.items.set(i.id, i));
  $("#host").value = s.settings.host || ""; $("#port").value = s.settings.port || 1337;
  $("#output").value = s.settings.output || ""; $("#keep").checked = !!s.settings.keep;
  translate();
  if (!s.ffmpeg) notice(t("noFfmpeg"), true);
}

/* Preview in a normal browser, without the Python side. */
function demo() {
  const base = { size: 94e6, duration: 1422, width: 848, height: 480, video: "h264", folder: "Golden Time" };
  return { settings: { lang: "es", host: "192.168.1.99", port: 1337, output: "C:\\Users\\Kirv\\Videos\\Kirv Videoteca", keep: true },
    ffmpeg: true, busy: false, items: [
      Object.assign({}, base, { id: 1, name: "Golden Time 01.mp4", audio: "aac HE-AAC", status: "sent", progress: 1 }),
      Object.assign({}, base, { id: 2, name: "Golden Time 02.mp4", audio: "aac HE-AAC", status: "sending", progress: .42 }),
      Object.assign({}, base, { id: 3, name: "Golden Time 03.mp4", audio: "aac HE-AAC", status: "encode_audio" }),
      Object.assign({}, base, { id: 4, name: "Mirai Nikki 05.mkv", folder: "Mirai Nikki", width: 1920, height: 1080, video: "hevc", audio: "opus", status: "encode_video", subtitles: true }),
      Object.assign({}, base, { id: 5, name: "Piloto.mp4", folder: "Mirai Nikki", width: 1280, height: 720, audio: "aac LC", status: "ready" }),
    ] };
}

/* Wait for the Python bridge; the sample list is only for previews opened with ?demo. */
function boot() {
  if ((window.pywebview && window.pywebview.api && window.pywebview.api.state) || location.search.includes("demo")) init();
  else setTimeout(boot, 100);
}
boot();
