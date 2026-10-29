import { test, expect } from "@playwright/test";

const journey = "EMBERWARD 1\n1 11 14 0 60 5 2 0 12345 0 0 0\n" +
  "1 0 42 8\n1 0 32 10\n1 0 25 20\n1 0 30 22\n";

async function play(page) {
  await page.goto("index.html");
  await page.getByRole("button", { name: "Play", exact: true }).click();
  await expect(page.locator("#play-status")).toHaveText("Playing");
}

async function confirm(page) {
  await page.getByRole("button", { name: "Confirm", exact: true }).click();
}

async function importJourney(page, text = journey) {
  await page.locator("#save-file").setInputFiles({
    name: "journey.sav", mimeType: "text/plain", buffer: Buffer.from(text)
  });
  await expect(page.locator("#status")).toContainText("Journey imported");
  await confirm(page);
  await expect(page.locator("#save")).toBeEnabled();
}

async function exportJourney(page) {
  const download = page.waitForEvent("download");
  await page.locator("#export").click();
  const file = await download;
  const stream = await file.createReadStream();
  const chunks = [];
  for await (const chunk of stream) chunks.push(chunk);
  return Buffer.concat(chunks).toString("utf8");
}

async function snapshotJourney(page) {
  return page.evaluate(() => {
    if (!Module._web_command(4)) throw new Error("Cannot snapshot this screen");
    const text = Module.FS.readFile("/export.sav", { encoding: "utf8" });
    Module.FS.unlink("/export.sav");
    return text;
  });
}

async function audioPeak(page) {
  return page.evaluate(async () => {
    const audio = Module.SDL3;
    const analyser = audio.audioContext.createAnalyser();
    audio.audio_playback.scriptProcessorNode.connect(analyser);
    await new Promise(resolve => setTimeout(resolve, 300));
    const samples = new Float32Array(analyser.fftSize);
    analyser.getFloatTimeDomainData(samples);
    audio.audio_playback.scriptProcessorNode.disconnect(analyser);
    return Math.max(...samples.map(Math.abs));
  });
}

test("renders 12 distinct scenes without lifecycle warnings", async ({ page }) => {
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.setViewportSize({ width: 360, height: 740 });
  await page.goto("index.html?smoke");
  await page.getByRole("button", { name: "Run scene test" }).click();
  await expect(page.locator("#log")).toContainText("120 frames, 12 distinct scenes");
  await expect(page.locator("#status")).toHaveText("Game finished. Reload to play again.");
  await expect(page.locator("#log")).not.toContainText("main loop does not exist");
  await expect(page.locator("#log")).not.toContainText("not supported");
  await expect(page.locator("#error")).toBeEmpty();
  expect(errors).toEqual([]);
});

test("imports, exports, saves durably, and loads after reload", async ({ page }) => {
  await play(page);
  await importJourney(page);
  expect(await exportJourney(page)).toBe(journey);
  await page.locator("#canvas").focus();
  await page.keyboard.press("F5");
  await expect(page.locator("#storage-status")).toHaveText("Journey saved in this browser.");
  await page.reload();
  await page.getByRole("button", { name: "Play", exact: true }).click();
  await page.locator("#canvas").focus();
  await page.keyboard.press("ArrowDown");
  await page.keyboard.press("Enter");
  await confirm(page);
  expect(await exportJourney(page)).toBe(journey);
  await page.locator("#canvas").focus();
  await page.keyboard.press("c");
  await expect.poll(() => page.evaluate(() => state.screen)).toBe(3);
  await page.locator("#mute").click();
  await expect(page.locator("#mute")).toHaveText("Unmute");
  await page.locator("#mute").click();
  await expect(page.locator("#mute")).toHaveText("Mute");
  await page.locator("#canvas").focus();
  await page.keyboard.press("Tab");
  await expect(page.locator("#canvas")).not.toBeFocused();
  await expect.poll(() => page.evaluate(() => state.screen)).toBe(3);
});

test("rejects malformed imports without replacing the journey", async ({ page }) => {
  await play(page);
  await importJourney(page);
  await page.locator("#save-file").setInputFiles({
    name: "bad.sav", mimeType: "text/plain", buffer: Buffer.from("EMBERWARD 999\nbad")
  });
  await expect(page.locator("#error")).toContainText("Invalid save file");
  await confirm(page);
  expect(await exportJourney(page)).toBe(journey);
});

test("reports restore failure and supports portable exports without persistence", async ({ page }) => {
  await page.addInitScript(() => {
    IDBFactory.prototype.open = function () { throw new Error("Storage blocked for test"); };
  });
  await page.goto("index.html");
  await page.getByRole("button", { name: "Play without browser saves" }).click();
  await expect(page.locator("#error")).toContainText("Storage blocked");
  await importJourney(page);
  await page.locator("#save").click();
  await expect(page.locator("#log")).toContainText("BROWSER STORAGE IS UNAVAILABLE");
  await expect(page.locator("#storage-status")).toContainText("Browser saves unavailable");
  await confirm(page);
  expect(await exportJourney(page)).toBe(journey);
});

test("does not claim save success before flush and reports persistence errors", async ({ page }) => {
  await play(page);
  await importJourney(page);
  await page.evaluate(() => {
    const original = Module.FS.syncfs;
    Module.FS.syncfs = (populate, callback) => {
      globalThis.finishTestSave = () => {
        Module.FS.syncfs = original;
        callback(new Error("Quota exceeded for test"));
      };
    };
  });
  await page.locator("#save").click();
  await expect(page.locator("#storage-status")).toContainText("Do not close");
  await expect(page.locator("#load")).toBeDisabled();
  await expect(page.locator("#import")).toBeDisabled();
  await page.evaluate(() => globalThis.finishTestSave());
  await expect(page.locator("#error")).toContainText("Quota exceeded");
  await expect(page.locator("#storage-status")).toContainText("Save failed");
  await confirm(page);
  expect(await exportJourney(page)).toBe(journey);
  await page.locator("#save").click();
  await expect(page.locator("#storage-status")).toHaveText("Journey saved in this browser.");
  await expect(page.locator("#error")).toBeEmpty();
});

test("handles synchronous save completion and synchronous flush errors", async ({ page }) => {
  await play(page);
  await importJourney(page);
  await page.evaluate(() => {
    globalThis.originalSyncfs = Module.FS.syncfs;
    Module.FS.syncfs = (_populate, callback) => {
      if (Module._web_command(0) !== 0) throw new Error("Save was not marked pending before commit");
      callback(null);
    };
  });
  await page.locator("#save").click();
  await expect.poll(() => page.evaluate(() => state.saving)).toBe(false);
  await expect(page.locator("#storage-status")).toHaveText("Journey saved in this browser.");
  await confirm(page);
  await page.evaluate(() => Module._web_save_complete(1));
  await expect.poll(() => page.evaluate(() => state.screen)).toBe(1);
  await page.evaluate(() => {
    Module.FS.syncfs = () => { throw new Error("Immediate flush failure for test"); };
  });
  await page.locator("#save").click();
  await expect(page.locator("#error")).toContainText("Immediate flush failure");
  await expect.poll(() => page.evaluate(() => state.saving)).toBe(false);
  await confirm(page);
  expect(await exportJourney(page)).toBe(journey);
  await page.evaluate(() => { Module.FS.syncfs = globalThis.originalSyncfs; });
  await page.locator("#save").click();
  await expect(page.locator("#storage-status")).toHaveText("Journey saved in this browser.");
});

test("touch hold, pointer cancellation, pause, and small-screen layout", async ({ page }) => {
  await play(page);
  if (await page.evaluate(() => navigator.maxTouchPoints > 0)) {
    for (const screen of [2, 2, 1]) {
      await page.getByRole("button", { name: "Confirm", exact: true }).tap();
      await expect.poll(() => page.evaluate(() => state.screen)).toBe(screen);
    }
  }
  await importJourney(page);
  await page.setViewportSize({ width: 360, height: 740 });
  await expect.poll(() => page.evaluate(() => document.documentElement.scrollWidth)).toBeLessThanOrEqual(360);
  const down = page.getByRole("button", { name: "Move down", exact: true });
  await down.scrollIntoViewIfNeeded();
  const box = await down.boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.waitForTimeout(500);
  await down.dispatchEvent("pointercancel", { pointerId: 1 });
  await page.mouse.up();
  const moved = await exportJourney(page);
  expect(moved).not.toBe(journey);
  await page.waitForTimeout(450);
  expect(await exportJourney(page)).toBe(moved);
  await page.evaluate(() => {
    Module._web_input(1, 1);
    Object.defineProperty(document, "hidden", { configurable: true, value: true });
    document.dispatchEvent(new Event("visibilitychange"));
  });
  await expect(page.locator("#play-status")).toContainText("Paused");
  const paused = await snapshotJourney(page);
  expect(await audioPeak(page)).toBeLessThan(0.0001);
  await page.waitForTimeout(450);
  expect(await snapshotJourney(page)).toBe(paused);
  await page.evaluate(() => {
    delete document.hidden;
    document.dispatchEvent(new Event("visibilitychange"));
  });
  await expect(page.locator("#play-status")).toHaveText("Playing");
  await page.waitForTimeout(450);
  expect(await exportJourney(page)).toBe(paused);
});

test("audio runs after user activation", async ({ page, browserName }) => {
  test.skip(browserName === "firefox", "Firefox headless keeps Web Audio suspended in CI");
  await play(page);
  await expect.poll(() => page.evaluate(() => Module.SDL3.audioContext.state)).toBe("running");
  expect(await audioPeak(page)).toBeGreaterThan(0.001);
});

test("fullscreen failure remains playable", async ({ page }) => {
  await play(page);
  await page.evaluate(() => {
    document.getElementById("player").requestFullscreen = () => Promise.reject(new Error("Fullscreen denied for test"));
  });
  await page.locator("#fullscreen").click();
  await expect(page.locator("#error")).toContainText("Fullscreen denied");
  await expect(page.locator("#play-status")).toHaveText("Playing");
});

test("fullscreen preserves aspect ratio when supported", async ({ page }) => {
  await play(page);
  const supported = await page.evaluate(() => document.fullscreenEnabled);
  test.skip(!supported, "This browser does not support element fullscreen");
  await page.locator("#fullscreen").click();
  await expect.poll(() => page.evaluate(() => document.fullscreenElement?.id)).toBe("player");
  const dimensions = await page.locator("#canvas").boundingBox();
  expect(dimensions.width / dimensions.height).toBeCloseTo(4 / 3, 1);
  await page.locator("#fullscreen").click();
  await expect.poll(() => page.evaluate(() => document.fullscreenElement)).toBeNull();
});
