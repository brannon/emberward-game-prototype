"use strict";

const canvas = document.getElementById("canvas");
const start = document.getElementById("start");
const status = document.getElementById("status");
const storageStatus = document.getElementById("storage-status");
const errorBox = document.getElementById("error");
const log = document.getElementById("log");
const player = document.getElementById("player");
const pointers = new Map();
let running = false;
let importing = false;
let state = null;
const smoke = new URLSearchParams(location.search).has("smoke");

function reportError(message) {
  errorBox.textContent = message;
  console.error(message);
}

function appendLog(args) {
  log.textContent = (log.textContent + args.join(" ") + "\n").slice(-16000);
}

function updateControls() {
  const ready = running && state !== null;
  const busy = !ready || state.saving || importing;
  const canSave = ready && (state.screen === 1 || state.screen === 3);
  const canLoad = ready && (state.screen === 0 || canSave);
  document.getElementById("save").disabled = busy || !canSave;
  document.getElementById("load").disabled = busy || !canLoad;
  document.getElementById("export").disabled = busy || !canSave;
  document.getElementById("import").disabled = busy || !canLoad;
  document.getElementById("mute").disabled = busy;
  document.getElementById("fullscreen").disabled = !ready;
  for (const button of document.querySelectorAll("[data-input]")) button.disabled = busy;
  if (ready) {
    const mute = document.getElementById("mute");
    mute.textContent = state.muted ? "Unmute" : "Mute";
    mute.setAttribute("aria-pressed", String(state.muted));
    document.getElementById("play-status").textContent = state.saving ? "Saving. Please wait..." :
      state.focused ? "Playing" : "Paused - return to the game to resume.";
  } else {
    document.getElementById("play-status").textContent = running ? "Starting..." : "Not running.";
  }
}

function clearPointers() {
  for (const input of pointers.values()) if (running) Module._web_input(input, 0);
  pointers.clear();
}

function resumeAudio() {
  const audio = Module.SDL3 && Module.SDL3.audioContext;
  if (audio && audio.state === "suspended" && !document.hidden)
    audio.resume().catch(error => reportError("Could not resume sound: " + error.message));
}

function focusGame() {
  canvas.focus({ preventScroll: true });
  if (running) Module._web_focus(document.hasFocus() && !document.hidden ? 1 : 0);
  resumeAudio();
}

var Module = {
  canvas,
  noInitialRun: true,
  storageReady: false,
  pendingSave: false,
  preRun: [() => {
    Module.addRunDependency("restore-saves");
    const finish = (error) => {
      Module.storageReady = !error;
      storageStatus.textContent = error ?
        "Browser saves unavailable. You can play, but export a save file to keep your progress." :
        "Browser save storage ready. Export a backup occasionally.";
      if (error) reportError("Could not restore browser saves: " + error.message);
      Module.removeRunDependency("restore-saves");
    };
    try {
      Module.FS.mkdir("/emberward-saves");
      Module.FS.mount(Module.IDBFS, {}, "/emberward-saves");
      Module.FS.syncfs(true, finish);
    } catch (error) {
      finish(error);
    }
  }],
  print: (...args) => { console.log(...args); appendLog(args); },
  printErr: (...args) => { appendLog(args); reportError(args.join(" ")); },
  onAbort: (reason) => {
    running = false;
    start.disabled = true;
    updateControls();
    reportError("Game runtime failed: " + reason + ". Reload to try again.");
  },
  onExit: (code) => {
    running = false;
    updateControls();
    status.textContent = code === 0 ? "Game finished. Reload to play again." : "Game exited with error " + code;
  },
  onRuntimeInitialized: () => {
    start.disabled = false;
    start.textContent = smoke ? "Run scene test" : Module.storageReady ? "Play" : "Play without browser saves";
    status.textContent = "Ready. Choose Play to enable sound and start.";
  },
  updateState: (screen, muted, focused, saving) => {
    if (state && state.screen === screen && state.muted === muted &&
        state.focused === focused && state.saving === saving) return;
    state = { screen, muted, focused, saving };
    updateControls();
  },
  persistSave: () => {
    clearPointers();
    Module.pendingSave = true;
    storageStatus.textContent = "Writing save to browser storage. Do not close this page yet.";
    const finish = (error) => {
      Module.pendingSave = false;
      storageStatus.textContent = error ? "Save failed. Export your journey or retry Save." :
        "Journey saved in this browser.";
      Module._web_save_complete(error ? 0 : 1);
      if (error) reportError("Could not persist browser save: " + error.message);
      else errorBox.textContent = "";
    };
    try {
      Module.FS.syncfs(false, finish);
    } catch (error) {
      finish(error);
    }
  }
};

start.addEventListener("click", () => {
  start.disabled = true;
  running = true;
  focusGame();
  status.textContent = smoke ? "Checking game scenes..." : "Click the game or use the controls below.";
  Module.callMain(smoke ? ["--smoke-test", "--mute"] : []);
  if (running) focusGame();
});

canvas.addEventListener("click", focusGame);
canvas.addEventListener("keydown", (event) => {
  if (event.code === "Tab") {
    event.stopImmediatePropagation();
    return;
  }
  if (event.code === "F11") {
    event.preventDefault();
    event.stopImmediatePropagation();
    if (!event.repeat) toggleFullscreen();
    return;
  }
  if (["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "Space", "Backspace", "F5", "F9"].includes(event.code))
    event.preventDefault();
}, true);

for (const button of document.querySelectorAll("[data-input]")) {
  const input = Number(button.dataset.input);
  button.addEventListener("pointerdown", (event) => {
    if (button.disabled || (event.pointerType === "mouse" && event.button !== 0)) return;
    event.preventDefault();
    focusGame();
    button.setPointerCapture(event.pointerId);
    pointers.set(event.pointerId, input);
    Module._web_input(input, 1);
  });
  const release = (event) => {
    if (!pointers.has(event.pointerId)) return;
    pointers.delete(event.pointerId);
    if (![...pointers.values()].includes(input)) Module._web_input(input, 0);
  };
  button.addEventListener("pointerup", release);
  button.addEventListener("pointercancel", release);
  button.addEventListener("lostpointercapture", release);
  button.addEventListener("click", (event) => {
    if (event.detail === 0 && running) {
      focusGame();
      Module._web_input(input, 1);
      Module._web_input(input, 0);
    }
  });
}

for (const button of document.querySelectorAll("[data-command]")) {
  button.addEventListener("click", () => {
    focusGame();
    Module._web_command(Number(button.dataset.command));
  });
}

async function toggleFullscreen() {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else if (player.requestFullscreen) await player.requestFullscreen();
    else throw new Error("Fullscreen is not supported here. Continue playing in the normal page.");
    focusGame();
  } catch (error) {
    reportError("Fullscreen: " + error.message);
  }
}
document.getElementById("fullscreen").addEventListener("click", toggleFullscreen);
document.addEventListener("fullscreenchange", () => {
  document.getElementById("fullscreen").textContent = document.fullscreenElement ? "Exit fullscreen" : "Fullscreen";
});

document.getElementById("export").addEventListener("click", () => {
  focusGame();
  if (!Module._web_command(4)) return;
  try {
    const data = Module.FS.readFile("/export.sav");
    const url = URL.createObjectURL(new Blob([data], { type: "text/plain" }));
    const link = document.createElement("a");
    link.href = url;
    link.download = "emberward-journey.sav";
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    status.textContent = "Save exported. Keep the downloaded file as a backup.";
  } catch (error) {
    reportError("Could not export save: " + error.message);
  } finally {
    Module.FS.unlink("/export.sav");
  }
});

const saveFile = document.getElementById("save-file");
document.getElementById("import").addEventListener("click", () => saveFile.click());
saveFile.addEventListener("change", async () => {
  const file = saveFile.files[0];
  if (!file) return;
  clearPointers();
  importing = true;
  updateControls();
  try {
    if (file.size > 4096) throw new Error("Save file is too large (maximum 4 KiB).");
    const bytes = new Uint8Array(await file.arrayBuffer());
    if (!running) throw new Error("The game has closed. Reload before importing.");
    Module.FS.writeFile("/import.sav", bytes);
    focusGame();
    if (!Module._web_command(3)) throw new Error("Invalid save file, or loading is not allowed on this screen.");
    status.textContent = "Journey imported. Choose Save to keep it in this browser.";
    errorBox.textContent = "";
  } catch (error) {
    reportError("Could not import save: " + error.message);
  } finally {
    if (Module.FS.analyzePath("/import.sav").exists) Module.FS.unlink("/import.sav");
    saveFile.value = "";
    importing = false;
    updateControls();
  }
});

function updateFocus() {
  const active = !document.hidden && document.hasFocus();
  if (!active) clearPointers();
  if (running) Module._web_focus(active ? 1 : 0);
  if (running && active) resumeAudio();
}
window.addEventListener("blur", updateFocus);
window.addEventListener("focus", updateFocus);
document.addEventListener("visibilitychange", updateFocus);
window.addEventListener("pagehide", clearPointers);
window.addEventListener("beforeunload", (event) => {
  if (Module.pendingSave) {
    event.preventDefault();
    event.returnValue = "";
  }
});
canvas.addEventListener("webglcontextlost", (event) => {
  event.preventDefault();
  clearPointers();
  if (running) Module._web_focus(0);
  running = false;
  updateControls();
  reportError("Graphics context lost. Reload the page to restart. Your last confirmed browser save is safe.");
});
window.addEventListener("error", (event) => {
  reportError("Page error: " + (event.message || "Could not load " + event.target.src));
}, true);
window.addEventListener("unhandledrejection", (event) => reportError("Page error: " + String(event.reason)));
