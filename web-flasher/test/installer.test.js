import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { runInNewContext } from "node:vm";
import { build } from "esbuild";
import { safeDiagnostics } from "../src/protocol.js";

// Exercise the real event handlers with the real page's element IDs. USB, local
// files, and release downloads are synthetic: these tests never access a badge
// or save a real recovery file. Detailed flash/protocol behavior has its own tests.
const html = await readFile(new URL("../index.html", import.meta.url), "utf8");
const modules = {
  "esptool-js": ["ESPLoader"], "./transport.js": ["SafeTransport"], "js-md5": ["md5"],
  "./protocol.js": ["ClockConnection", "delay", "safeDiagnostics"],
  "./guards.js": ["sha256", "verifiedRead", "verifySecurity", "verifyBootSelector", "validateArtifact", "validateManifest"],
  "./factory.js": ["inspectFactorySource", "createFactoryPlan", "backupFactoryFlash", "saveFactoryBackup", "revalidateFactoryBeforeWrite", "verifyFactoryAfterWrite"],
};
const bundled = await build({
  entryPoints: [fileURLToPath(new URL("../src/installer.js", import.meta.url))],
  bundle: true, write: false, format: "iife", platform: "browser",
  plugins: [{ name: "controller-boundaries", setup(builder) {
    builder.onResolve({ filter: /.*/ }, args => modules[args.path] ? { path: args.path, namespace: "fixture" } : undefined);
    builder.onLoad({ filter: /.*/, namespace: "fixture" }, args => ({ contents: modules[args.path]
      .map(name => `export const ${name} = globalThis.__mocks.${name};`).join("\n") }));
  } }],
});
const code = bundled.outputFiles[0].text;
const BUILD = "v1.2.0";
const PRIVATE = "PRIVATE_FACTORY_PROFILE_WIFI_PASSWORD";
const bytes = new TextEncoder().encode(PRIVATE);
const ready = { build: BUILD, framework: "factory", board: 30, flash_bytes: 16777216, psram_bytes: 8388608,
  store_ready: true, clock_valid: true, rtc: true, setup: false, wifi_mode: 0, bluetooth: 0 };
const abort = () => Object.assign(new Error("User cancelled"), { name: "AbortError" });

function fixture(options = {}) {
  const nodes = new Map([...html.matchAll(/<[^>]*\bid="([^"]+)"[^>]*>/g)].map(([tag, id]) => [id, {
    checked: false, disabled: /\bdisabled\b/.test(tag), hidden: /\bhidden\b/.test(tag), textContent: "", value: 0,
    dataset: {}, attributes: {}, listeners: new Map(),
    addEventListener(name, fn) { this.listeners.set(name, fn); },
    setAttribute(name, value) { this.attributes[name] = value; },
  }]));
  const get = id => { assert.ok(nodes.has(id), `controller element ${id} exists in index.html`); return nodes.get(id); };
  const events = [], flashes = [], provisions = [], requests = [], copied = [];
  const release = { artifacts: [{ offset: 0x10000, path: "firmware.bin" }], partition_table: { sector_sha256: "prepared" } };
  const backup = { bytes, sha256: "private-backup-digest", md5: "private-backup-md5" };
  const source = { id: "approved-factory" };
  const port = {};
  const fail = key => { if (options[key]) throw options[key] instanceof Error ? options[key] : new Error(key); };
  const mocks = {
    ESPLoader: class {
      constructor() { this.chip = { CHIP_NAME: "ESP32-S3" }; this.IS_STUB = true; }
      async detectChip() { events.push("connect"); }
      async checkCommand() { return new Uint8Array(); }
      async runStub() { events.push("stub"); }
      async detectFlashSize() { return "16MB"; }
      async after(mode) { events.push(mode); }
      async writeFlash(config) { events.push("flash"); flashes.push(config); fail("flashError"); }
    },
    SafeTransport: class {
      async setRTS() { events.push("rts"); }
      async disconnect() { events.push("disconnect"); }
    },
    ClockConnection: class {
      async open() { events.push("clock-open"); }
      async provision(build, config) {
        events.push("provision"); provisions.push({ build, ...config }); fail("provisionError");
        return { ...ready, recovery: backup, profile: PRIVATE, password: PRIVATE };
      }
      async close() { events.push("clock-close"); }
    },
    delay: async () => {}, safeDiagnostics, md5: () => "verified-md5",
    sha256: async value => {
      if (value === "saved-file") return options.savedHashMismatch ? "corrupt" : backup.sha256;
      return options.preparedMismatch ? "unknown-layout" : "prepared";
    },
    verifiedRead: async (_loader, _transport, address) => { events.push(`read:${address}`); return new Uint8Array(4096); },
    verifySecurity: () => { events.push("security"); fail("securityError"); },
    verifyBootSelector: () => { events.push("selector"); fail("selectorError"); },
    validateArtifact: async () => { events.push("artifact"); }, validateManifest: value => value,
    inspectFactorySource: async () => { events.push("inspect"); fail("unknownLayout"); return source; },
    createFactoryPlan: async () => {
      events.push("plan"); fail("planError");
      return { fileArray: [{ address: 0x9000, data: new Uint8Array(4096).fill(255) }] };
    },
    backupFactoryFlash: async () => { events.push("backup"); fail("backupError"); return backup; },
    saveFactoryBackup: async (value, save) => {
      assert.equal(value, backup); events.push("verify-backup");
      if (await save(value) !== true) throw new Error("Save and verify the recovery backup before replacing factory firmware.");
      events.push("saved-and-verified");
    },
    revalidateFactoryBeforeWrite: async () => { events.push("revalidate"); fail("changedDevice"); },
    verifyFactoryAfterWrite: async () => { events.push("verify-written"); fail("writeVerificationError"); },
  };
  const window = {
    isSecureContext: true, addEventListener() {},
    showSaveFilePicker() {
      events.push("file-picker"); fail("pickerError");
      return Promise.resolve({
        async createWritable() {
          events.push("file-open");
          return {
            async write(value) { assert.equal(value, bytes); events.push("file-write"); fail("saveError"); },
            async close() { events.push("file-close"); }, async abort() { events.push("file-abort"); },
          };
        },
        async getFile() {
          events.push("file-readback");
          return { size: options.savedSizeMismatch ? 1 : bytes.length, arrayBuffer: async () => "saved-file" };
        },
      });
    },
  };
  window.top = window.self = window;
  if (options.noSavePicker) delete window.showSaveFilePicker;
  const navigator = {
    serial: { requestPort() { events.push("port-picker"); fail("portError"); return Promise.resolve(port); } },
    clipboard: { async writeText(value) { copied.push(value); } },
  };
  runInNewContext(code, {
    __mocks: mocks, window, navigator, document: { getElementById: get }, URLSearchParams, Uint8Array,
    performance: { now: () => 0 },
    fetch: async (url, config) => {
      events.push("fetch"); requests.push({ url, ...config });
      return { ok: true, json: async () => release, arrayBuffer: async () => new Uint8Array([1, 2, 3]).buffer };
    },
  });
  const trigger = (id, event = "click") => {
    const handler = get(id).listeners.get(event); assert.ok(handler, `${id} handles ${event}`);
    return handler({});
  };
  const check = (id, value = true) => { get(id).checked = value; trigger(id, "change"); };
  check("hardware");
  return { get, trigger, check, events, flashes, provisions, requests, copied };
}

async function prepareAndSave(f) { await f.trigger("prepare"); await f.trigger("save-backup"); }

test("USB and file choosers run directly in their user gestures before asynchronous work", async () => {
  for (const id of ["install", "prepare", "clock", "finish-install"]) {
    const f = fixture(); if (id === "finish-install") f.check("finish-confirm");
    const operation = f.trigger(id);
    assert.deepEqual(f.events, ["port-picker"], `${id} invokes the picker before its first await`);
    await operation;
  }
  const f = fixture(); await f.trigger("prepare"); f.events.length = 0;
  const saving = f.trigger("save-backup");
  assert.deepEqual(f.events, ["file-picker"], "file picker precedes backup hashing and file writes");
  await saving;
});

test("existing badge updates and clock-only checks never request storage initialization", async () => {
  for (const id of ["install", "clock"]) {
    const f = fixture(); await f.trigger(id);
    assert.equal(f.provisions.length, 1); assert.equal(f.provisions[0].initializeStorage, false);
    assert.equal(f.flashes.length, id === "install" ? 1 : 0);
    assert.equal(f.events.includes("backup"), false);
    assert.equal(f.get("activity").dataset.result, "success");
    if (id === "install") {
      assert.equal(f.flashes[0].eraseAll, false);
      assert.deepEqual(Array.from(f.flashes[0].fileArray, file => file.address), [0x10000]);
    }
  }
});

test("first install requires a verified saved recovery file and a separate destructive confirmation", async () => {
  const f = fixture(); await f.trigger("prepare");
  assert.equal(f.flashes.length, 0); assert.equal(f.provisions.length, 0);
  assert.equal(f.get("factory-actions").hidden, false);
  assert.equal(f.get("replace-confirm").disabled, true);
  f.check("replace-confirm"); await f.trigger("factory-write");
  assert.equal(f.flashes.length, 0, "changing a disabled checkbox cannot bypass the save guard");
  f.check("replace-confirm", false); await f.trigger("save-backup");
  assert.equal(f.get("replace-confirm").disabled, false);
  assert.equal(f.get("factory-write").disabled, true);
  await f.trigger("factory-write"); assert.equal(f.flashes.length, 0);
  f.check("replace-confirm"); await f.trigger("factory-write");
  assert.equal(f.flashes.length, 1); assert.equal(f.flashes[0].eraseAll, false);
  assert.equal(f.provisions[0].initializeStorage, true);
  const sequence = ["backup", "file-picker", "saved-and-verified", "revalidate", "flash", "verify-written", "hard_reset", "provision"];
  assert.deepEqual(f.events.filter(event => sequence.includes(event)), sequence);
  assert.equal(f.get("factory-actions").hidden, true);
  assert.equal(f.get("replace-confirm").checked, false);
});

test("cancelling a prepared first install resets and releases USB without writing", async () => {
  const f = fixture(); await f.trigger("prepare"); await f.trigger("factory-cancel");
  assert.equal(f.flashes.length, 0); assert.equal(f.provisions.length, 0);
  assert.deepEqual(f.events.slice(-3), ["rts", "hard_reset", "disconnect"]);
  assert.equal(f.get("factory-actions").hidden, true);
  assert.equal(f.get("prepare").disabled, false); assert.equal(f.get("hardware").disabled, false);
  assert.match(f.get("status").textContent, /Nothing was written/);
  f.check("replace-confirm"); await f.trigger("factory-write"); assert.equal(f.flashes.length, 0);
});

test("unknown layouts, invalid plans, and failed backups cannot reach a write", async () => {
  for (const key of ["unknownLayout", "planError", "backupError"]) {
    const f = fixture({ [key]: true }); await f.trigger("prepare");
    f.check("replace-confirm"); await f.trigger("factory-write");
    assert.equal(f.flashes.length, 0, key); assert.equal(f.provisions.length, 0, key);
    assert.deepEqual(f.events.slice(-3), ["rts", "hard_reset", "disconnect"]);
    assert.equal(f.get("factory-actions").hidden, true); assert.equal(f.get("prepare").disabled, false);
  }
});

test("cancelled, failed, truncated, or corrupted recovery saves never enable first install", async () => {
  for (const options of [{ pickerError: abort() }, { saveError: true }, { savedSizeMismatch: true }, { savedHashMismatch: true }]) {
    const f = fixture(options); await prepareAndSave(f);
    assert.equal(f.get("replace-confirm").disabled, true);
    assert.equal(f.get("factory-write").disabled, true);
    f.check("replace-confirm"); await f.trigger("factory-write");
    assert.equal(f.flashes.length, 0); assert.equal(f.provisions.length, 0);
    if (options.saveError) assert.ok(f.events.includes("file-abort"));
    await f.trigger("factory-cancel");
  }
});

test("a badge changed since backup is rejected before write; post-write verification is required before storage init", async () => {
  for (const key of ["changedDevice", "writeVerificationError"]) {
    const f = fixture({ [key]: true }); await prepareAndSave(f); f.check("replace-confirm");
    await f.trigger("factory-write");
    assert.equal(f.flashes.length, key === "changedDevice" ? 0 : 1);
    assert.equal(f.provisions.length, 0);
    assert.ok(f.events.includes("disconnect"));
    assert.equal(f.get("activity").dataset.result, "error");
    assert.equal(f.get("factory-actions").hidden, true);
  }
});

test("finish first install requires hardware and erase confirmation plus the verified prepared layout", async () => {
  const f = fixture(); await f.trigger("finish-install"); assert.equal(f.events.length, 0);
  f.check("hardware", false); f.check("finish-confirm");
  await f.trigger("finish-install"); assert.equal(f.events.length, 0);
  f.check("hardware"); await f.trigger("finish-install");
  assert.equal(f.flashes.length, 0); assert.equal(f.provisions[0].initializeStorage, true);
  assert.ok(f.events.indexOf("selector") < f.events.indexOf("provision"));
  assert.equal(f.get("finish-confirm").checked, false);
  for (const options of [{ preparedMismatch: true }, { selectorError: true }, { securityError: true }]) {
    const refused = fixture(options); refused.check("finish-confirm"); await refused.trigger("finish-install");
    assert.equal(refused.flashes.length, 0); assert.equal(refused.provisions.length, 0);
    assert.equal(refused.get("finish-confirm").checked, false);
    assert.equal(refused.events.at(-1), "disconnect");
  }
});

test("first install stays unavailable without verifiable local file saving", async () => {
  const f = fixture({ noSavePicker: true });
  assert.equal(f.get("prepare").disabled, true); assert.equal(f.get("save-unsupported").hidden, false);
  await f.trigger("prepare"); assert.equal(f.events.length, 0);
  await f.trigger("install"); assert.equal(f.provisions[0].initializeStorage, false);
});

test("private recovery data never enters issue reports, copied diagnostics, or network requests", async () => {
  const f = fixture(); await prepareAndSave(f);
  await f.trigger("report");
  assert.doesNotMatch(f.get("report").href, /PRIVATE_FACTORY|private-backup/);
  f.check("replace-confirm"); await f.trigger("factory-write"); await f.trigger("copy"); await f.trigger("report");
  assert.equal(f.copied.length, 1);
  assert.deepEqual(JSON.parse(f.copied[0]), { release: BUILD, ...ready });
  assert.doesNotMatch(f.get("report").href, /PRIVATE_FACTORY|private-backup|recovery%22|profile%22|password%22/);
  for (const request of f.requests) {
    assert.equal(request.body, undefined); assert.equal(request.method, undefined);
    assert.ok(request.url.startsWith(`/stopwatch/install/releases/${BUILD}/`));
    assert.doesNotMatch(JSON.stringify(request), /PRIVATE_FACTORY|private-backup/);
  }
});
