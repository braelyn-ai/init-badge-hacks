import test from "node:test";
import assert from "node:assert/strict";
import { setImmediate as immediate } from "node:timers/promises";
import {
  MIN_EPOCH, MAX_EPOCH, nonce, validateClock, validateReadiness,
  safeDiagnostics, ClockConnection,
} from "../src/protocol.js";

const BUILD = "v1.2.0";
const NOW = 1893456000;
const clock = (epoch = NOW, offset = -480) => ({ protocol: 1, ok: true, valid: true, source: "computer", epoch, rtc_epoch: epoch, offset_minutes: offset });
const ready = () => ({ build: BUILD, framework: "factory", board: 30, flash_bytes: 16777216, psram_bytes: 8388608, store_ready: true, clock_valid: true, rtc: true, setup: false, wifi_mode: 0, bluetooth: 0 });

class FakePort {
  constructor(onWrite = () => {}) {
    this.writes = []; this.closed = false; this.cancelled = false; this.onWrite = onWrite;
    this.readable = new ReadableStream({ start: controller => { this.controller = controller; }, cancel: () => { this.cancelled = true; } });
    this.writable = new WritableStream({ write: async bytes => {
      const text = new TextDecoder().decode(bytes); assert.ok(text.endsWith("\n"));
      const value = JSON.parse(text); this.writes.push(value); await this.onWrite(value, this);
    } });
  }
  async open(options) { this.options = options; }
  async close() {
    assert.equal(this.readable.locked, false, "release reader before closing port");
    assert.equal(this.writable.locked, false, "release writer before closing port");
    this.closed = true;
  }
  push(value) { this.controller.enqueue(new TextEncoder().encode(value)); }
  reply(prefix, request, value) { this.push(`${prefix} ${JSON.stringify({ ...value, nonce: request.nonce })}\n`); }
}

async function connection(t, onWrite) {
  const port = new FakePort(onWrite), conn = new ClockConnection(port);
  await conn.open(); t.after(() => conn.close()); return { port, conn };
}

function fakeTime(t, maximum = 40000) {
  t.mock.timers.enable({ apis: ["setTimeout", "Date"], now: NOW * 1000 });
  t.mock.method(performance, "now", () => Date.now() - NOW * 1000);
  return async promise => {
    let done = false;
    // Attach both handlers immediately so simulated rejections stay handled.
    promise.then(() => { done = true; }, () => { done = true; });
    for (let elapsed = 0; !done && elapsed <= maximum; elapsed += 25) {
      await immediate(); t.mock.timers.tick(25);
    }
    assert.ok(done, `bounded protocol operation settled within simulated ${maximum / 1000} seconds`);
    return promise;
  };
}

function provisioningReplies(status = ready, initialize = () => {}) {
  let offset;
  return (request, port) => {
    if (request.op === "clock_set") { offset = request.offset_minutes; port.reply("CLOCK_ACK", request, clock(request.epoch, offset)); }
    if (request.op === "clock_status") port.reply("CLOCK_STATUS", request, { ...clock(Math.floor(Date.now() / 1000), offset), source: "rtc" });
    if (request.op === "status") port.reply("CONFERENCE_STATUS", request, status());
    if (request.op === "initialize_conference_storage") initialize(request, port);
  };
}

test("clock validation enforces verified RTC, supported range, offset, and drift", () => {
  for (const source of ["computer", "rtc"]) for (const offset of [-480, 0, 330, 840])
    assert.doesNotThrow(() => validateClock({ ...clock(NOW, offset), source }, NOW, offset));
  for (const change of [
    { protocol: "1" }, { ok: 1 }, { valid: false }, { source: "build" },
    { offset_minutes: "-480" }, { offset_minutes: 480 }, { epoch: NOW + 4 },
    { rtc_epoch: NOW - 4 }, { epoch: NOW + 2 }, { epoch: NOW + 0.5 },
    { epoch: MIN_EPOCH - 1 }, { epoch: MAX_EPOCH }, { rtc_epoch: null },
  ]) assert.throws(() => validateClock({ ...clock(), ...change }, NOW, -480));
  assert.doesNotThrow(() => validateClock(clock(MIN_EPOCH), MIN_EPOCH, -480));
  assert.doesNotThrow(() => validateClock(clock(MAX_EPOCH - 1), MAX_EPOCH - 1, -480));
  assert.doesNotThrow(() => validateClock({ ...clock(NOW + 2), rtc_epoch: NOW + 3 }, NOW, -480));
});

test("readiness requires this build, board, mounted storage, RTC, and radios off", () => {
  assert.doesNotThrow(() => validateReadiness(ready(), BUILD));
  for (const change of [
    { build: "old-build" }, { board: "30" }, { board: 29 }, { flash_bytes: 8388608 },
    { psram_bytes: 0 }, { store_ready: false }, { store_ready: "true" },
    { clock_valid: false }, { rtc: false }, { setup: true }, { wifi_mode: 1 },
    { bluetooth: 1 }, { wifi_mode: "0" },
  ]) assert.throws(() => validateReadiness({ ...ready(), ...change }, BUILD));
  for (const key of Object.keys(ready()).filter(key => key !== "framework")) {
    const value = ready(); delete value[key]; assert.throws(() => validateReadiness(value, BUILD), key);
  }
  assert.doesNotThrow(() => validateReadiness({ ...ready(), store_ready: false }, BUILD, false));
  assert.throws(() => validateReadiness({ ...ready(), store_ready: undefined }, BUILD, false));
});

test("diagnostic export excludes account details, passwords, identifiers, and raw logs", () => {
  const value = { ...ready(), name: "Private person", profile: { url: "https://private.test" },
    password: "SECRET", ssid: "PRIVATE", mac: "aa:bb:cc:dd:ee:ff", nonce: "private-nonce", raw: "PRIVATE LOG" };
  const result = safeDiagnostics(value, BUILD);
  assert.deepEqual(result, { release: BUILD, ...ready() });
  assert.doesNotMatch(JSON.stringify(result), /SECRET|PRIVATE|private|aa:bb|Private/);
  const nested = safeDiagnostics({ ...ready(), framework: { secret: "nested" } }, BUILD);
  assert.equal("framework" in nested, false);
  assert.equal(Object.hasOwn(result, "nonce"), false);
});

test("requests generate fresh nonces and cannot be overridden by extra fields", async t => {
  const { port, conn } = await connection(t, (request, port) => port.reply("CLOCK_ACK", request, clock()));
  for (let i = 0; i < 2; i++) await conn.request("clock_set", "CLOCK_ACK", { op: "erase", nonce: "stale", epoch: NOW });
  assert.deepEqual(port.options, { baudRate: 115200 });
  assert.ok(port.writes.every(request => request.op === "clock_set" && /^[a-f0-9]{24}$/.test(request.nonce)));
  assert.notEqual(port.writes[0].nonce, port.writes[1].nonce);
  assert.match(nonce(), /^[a-f0-9]{24}$/);
  await conn.close(); assert.equal(port.closed, true); assert.equal(port.cancelled, true);
});

test("fragmented replies ignore raw logs, malformed JSON, wrong prefixes, and stale nonces", async t => {
  const { conn } = await connection(t, (request, port) => {
    port.push('Private profile password=SECRET\nCLOCK_ACK {bad}\nCLOCK_ACK null\n');
    port.reply("CONFERENCE_STATUS", request, ready());
    port.reply("CLOCK_ACK", { nonce: "old-nonce" }, clock());
    const valid = `CLOCK_ACK ${JSON.stringify({ ...clock(), nonce: request.nonce })}\r\n`;
    for (const character of valid) port.push(character);
  });
  const reply = await conn.request("clock_set", "CLOCK_ACK", {}, 100);
  assert.equal(reply.epoch, NOW); assert.equal(conn.pending, "");
  assert.doesNotMatch(JSON.stringify(conn.lines), /SECRET|Private/);
});

test("reader bounds oversized records and queued replies, then resynchronizes", async t => {
  const { port, conn } = await connection(t);
  port.push("CLOCK_ACK " + "x".repeat(8000)); await immediate();
  assert.equal(conn.pending, ""); assert.equal(conn.lines.length, 0);
  port.push("more ignored bytes\n");
  for (let i = 0; i < 40; i++) port.push(`CLOCK_ACK {"nonce":"old-${i}"}\n`);
  await immediate(); assert.equal(conn.lines.length, 24);
  assert.equal(JSON.parse(conn.lines[0].slice(10)).nonce, "old-16");
  port.onWrite = (request, port) => port.reply("CLOCK_ACK", request, clock());
  assert.equal((await conn.request("clock_set", "CLOCK_ACK", {}, 100)).valid, true);
});

test("silent or stale devices time out without accepting another request's reply", async t => {
  const drive = fakeTime(t);
  const { conn } = await connection(t, (request, port) => port.reply("CLOCK_ACK", { nonce: "stale" }, clock()));
  await assert.rejects(drive(conn.request("clock_set", "CLOCK_ACK", {}, 50)), /did not reply in time/);
});

test("read errors and EOF become private, bounded connection failures", async t => {
  for (const fail of [port => port.controller.error(Error("SECRET PROFILE AND PASSWORD")), port => port.controller.close()]) {
    const { port, conn } = await connection(t);
    fail(port); await immediate();
    await assert.rejects(conn.request("status", "CONFERENCE_STATUS", {}, 50), error => {
      assert.match(error.message, /USB/); assert.doesNotMatch(error.message, /SECRET|PROFILE|PASSWORD/); return true;
    });
    await conn.close();
  }
});

test("opening cleans up if USB disappears after its reader is acquired", async t => {
  const port = new FakePort();
  port.writable = null;
  port.close = async function () {
    assert.equal(this.readable.locked, false);
    this.closed = true;
  };
  const conn = new ClockConnection(port);
  t.after(() => conn.close());
  await assert.rejects(conn.open());
  assert.equal(port.readable.locked, false, "failed open must release its reader before retry");
  assert.equal(port.closed, true, "failed open must close the partially opened USB port");
});

test("close is bounded even when USB reader cancellation and port close stall", async t => {
  const drive = fakeTime(t);
  const port = new FakePort();
  port.readable = new ReadableStream({ cancel: () => new Promise(() => {}) });
  port.close = () => new Promise(() => {});
  const conn = new ClockConnection(port);
  await conn.open();
  await drive(conn.close());
  assert.equal(port.readable.locked, false);
  assert.equal(port.writable.locked, false);
});

test("provision verifies a fresh clock, advancing RTC, and readiness in order", async t => {
  const drive = fakeTime(t);
  let offset;
  const { port, conn } = await connection(t, (request, port) => {
    if (request.op === "clock_set") { offset = request.offset_minutes; port.reply("CLOCK_ACK", request, clock(request.epoch, offset)); }
    if (request.op === "clock_status") port.reply("CLOCK_STATUS", request, { ...clock(Math.floor(Date.now() / 1000), offset), source: "rtc" });
    if (request.op === "status") port.reply("CONFERENCE_STATUS", request, ready());
  });
  assert.deepEqual(await drive(conn.provision(BUILD)), { ...ready(), nonce: port.writes[2].nonce });
  assert.deepEqual(port.writes.map(request => request.op), ["clock_set", "clock_status", "status"]);
  assert.equal(new Set(port.writes.map(request => request.nonce)).size, 3);
  // JSON serializes negative zero as zero on UTC machines.
  assert.equal(port.writes[0].offset_minutes, -new Date().getTimezoneOffset() || 0);
});

test("first install accepts the released firmware's exact unversioned storage acknowledgment, then verifies fresh status", async t => {
  const drive = fakeTime(t);
  let mounted = false;
  const { port, conn } = await connection(t, provisioningReplies(
    () => ({ ...ready(), store_ready: mounted }),
    // firmware/factory_badge/main/main.cpp emits these fields plus the nonce;
    // its reply() helper does not add a protocol version.
    (request, port) => { mounted = true; port.reply("STORAGE_ACK", request, { ok: true, store_ready: true }); },
  ));
  assert.deepEqual(await drive(conn.provision(BUILD, { initializeStorage: true })), { ...ready(), nonce: port.writes[4].nonce });
  assert.deepEqual(port.writes.map(request => request.op), ["clock_set", "clock_status", "status", "initialize_conference_storage", "status"]);
  assert.deepEqual(port.writes[3], { op: "initialize_conference_storage", confirm: "ERASE_FFAT_FOR_CONFERENCE", nonce: port.writes[3].nonce });
  assert.ok(port.writes.every(request => /^[a-f0-9]{24}$/.test(request.nonce)));
  assert.equal(new Set(port.writes.map(request => request.nonce)).size, 5);
});

test("clock-only and update provisioning never format; initialization requires literal true", async t => {
  for (const options of [undefined, {}, { initializeStorage: false }, { initializeStorage: 1 }, { initializeStorage: "true" }, { initializeStorage: null }]) {
    await t.test(JSON.stringify(options) || "default", async t => {
      const drive = fakeTime(t);
      const { port, conn } = await connection(t, provisioningReplies(() => ({ ...ready(), store_ready: false })));
      await assert.rejects(drive(conn.provision(BUILD, options)), /storage needs preparation/);
      assert.deepEqual(port.writes.map(request => request.op), ["clock_set", "clock_status", "status"]);
    });
  }
});

test("explicit first install preserves an already mounted profile store", async t => {
  const drive = fakeTime(t);
  const { port, conn } = await connection(t, provisioningReplies());
  await drive(conn.provision(BUILD, { initializeStorage: true }));
  assert.deepEqual(port.writes.map(request => request.op), ["clock_set", "clock_status", "status"]);
});

test("first install checks build, hardware, clock, and offline state before any format", async t => {
  for (const invalid of [
    { build: "old-build" }, { board: 29 }, { flash_bytes: 8388608 }, { psram_bytes: 0 },
    { store_ready: "false" }, { clock_valid: false }, { rtc: false },
    { setup: true }, { wifi_mode: 1 }, { bluetooth: 1 },
  ]) {
    await t.test(JSON.stringify(invalid), async t => {
      const drive = fakeTime(t);
      const { port, conn } = await connection(t, provisioningReplies(() => ({ ...ready(), store_ready: false, ...invalid })));
      await assert.rejects(drive(conn.provision(BUILD, { initializeStorage: true })));
      assert.deepEqual(port.writes.map(request => request.op), ["clock_set", "clock_status", "status"]);
    });
  }
});

test("storage preparation rejects failed acknowledgment fields and any conflicting explicit protocol version without retry", async t => {
  for (const ack of [
    {}, { ok: false, store_ready: false },
    { ok: 1, store_ready: true }, { ok: true, store_ready: "true" },
    { ok: true },
    ...[0, 2, "1", null, true].map(protocol => ({ protocol, ok: true, store_ready: true })),
  ]) {
    await t.test(JSON.stringify(ack), async t => {
      const drive = fakeTime(t);
      const { port, conn } = await connection(t, provisioningReplies(
        () => ({ ...ready(), store_ready: false }),
        (request, port) => port.reply("STORAGE_ACK", request, ack),
      ));
      await assert.rejects(drive(conn.provision(BUILD, { initializeStorage: true })), /Storage preparation was not confirmed/);
      assert.deepEqual(port.writes.map(request => request.op), ["clock_set", "clock_status", "status", "initialize_conference_storage"]);
    });
  }
});

test("storage formatting can take longer than an ordinary request and still requires a fresh matching nonce", async t => {
  const drive = fakeTime(t, 70000);
  let mounted = false;
  const { port, conn } = await connection(t, provisioningReplies(
    () => ({ ...ready(), store_ready: mounted }),
    (request, port) => {
      port.reply("STORAGE_ACK", { nonce: "old-request" }, { ok: true, store_ready: true });
      setTimeout(() => { mounted = true; port.reply("STORAGE_ACK", request, { ok: true, store_ready: true }); }, 45000);
    },
  ));
  await drive(conn.provision(BUILD, { initializeStorage: true }));
  assert.ok(Date.now() >= (NOW + 45) * 1000);
  assert.equal(port.writes.filter(request => request.op === "initialize_conference_storage").length, 1);
});

test("missing storage acknowledgment times out without retry and clock-only recovery only checks current state", async t => {
  const drive = fakeTime(t, 70000);
  let mounted = false;
  const { port, conn } = await connection(t, provisioningReplies(
    () => ({ ...ready(), store_ready: mounted }),
    (request, port) => {
      // Device completed the operation, but the matching acknowledgment was lost.
      mounted = true;
      port.push('STORAGE_ACK {bad}\n');
      port.reply("STORAGE_ACK", { nonce: "stale" }, { ok: true, store_ready: true });
    },
  ));
  await assert.rejects(drive(conn.provision(BUILD, { initializeStorage: true })), /Reconnect the badge and retry clock setup/);
  assert.ok(Date.now() >= (NOW + 60) * 1000);
  assert.deepEqual(port.writes.map(request => request.op), ["clock_set", "clock_status", "status", "initialize_conference_storage"]);
  await drive(conn.provision(BUILD));
  assert.deepEqual(port.writes.slice(4).map(request => request.op), ["clock_set", "clock_status", "status"]);
  assert.equal(port.writes.filter(request => request.op === "initialize_conference_storage").length, 1);
});

test("a positive storage acknowledgment alone cannot mark a unit ready", async t => {
  for (const after of [{ store_ready: false }, { build: "wrong-build" }, { rtc: false }, { wifi_mode: 1 }]) {
    await t.test(JSON.stringify(after), async t => {
      const drive = fakeTime(t);
      let initialized = false;
      const { port, conn } = await connection(t, provisioningReplies(
        () => ({ ...ready(), ...(initialized ? after : { store_ready: false }) }),
        (request, port) => { initialized = true; port.reply("STORAGE_ACK", request, { ok: true, store_ready: true }); },
      ));
      await assert.rejects(drive(conn.provision(BUILD, { initializeStorage: true })));
      assert.deepEqual(port.writes.map(request => request.op), ["clock_set", "clock_status", "status", "initialize_conference_storage", "status"]);
    });
  }
});

test("startup retries refresh browser time and nonce instead of replaying old clock data", async t => {
  const drive = fakeTime(t);
  let sets = 0, offset;
  const { port, conn } = await connection(t, (request, port) => {
    if (request.op === "clock_set") {
      offset = request.offset_minutes;
      if (++sets > 1) port.reply("CLOCK_ACK", request, clock(request.epoch, offset));
    }
    if (request.op === "clock_status") port.reply("CLOCK_STATUS", request, clock(Math.floor(Date.now() / 1000), offset));
    if (request.op === "status") port.reply("CONFERENCE_STATUS", request, ready());
  });
  await drive(conn.provision(BUILD));
  assert.equal(sets, 2); assert.ok(port.writes[1].epoch > port.writes[0].epoch);
  assert.notEqual(port.writes[0].nonce, port.writes[1].nonce);
});

test("provision cannot report ready when the RTC is frozen", async t => {
  const drive = fakeTime(t);
  let initial;
  const { port, conn } = await connection(t, (request, port) => {
    if (request.op === "clock_set") { initial = clock(request.epoch, request.offset_minutes); port.reply("CLOCK_ACK", request, initial); }
    if (request.op === "clock_status") port.reply("CLOCK_STATUS", request, initial);
  });
  await assert.rejects(drive(conn.provision(BUILD)), /not advancing/);
  assert.deepEqual(port.writes.map(request => request.op), ["clock_set", "clock_status"]);
});

test("an invalid clock acknowledgment stops provisioning without retry or readiness success", async t => {
  const drive = fakeTime(t);
  const { port, conn } = await connection(t, (request, port) => {
    port.reply("CLOCK_ACK", request, { ...clock(request.epoch, request.offset_minutes), source: "build" });
  });
  await assert.rejects(drive(conn.provision(BUILD)), /did not verify its clock/);
  assert.deepEqual(port.writes.map(request => request.op), ["clock_set"]);
});

test("provision never starts when computer time is outside supported years", async t => {
  const drive = fakeTime(t); t.mock.timers.setTime((MIN_EPOCH - 1) * 1000);
  const { port, conn } = await connection(t);
  await assert.rejects(drive(conn.provision(BUILD)), /computer's date/); assert.equal(port.writes.length, 0);
});

test("request deadline also bounds a stalled USB write", async t => {
  let finishWrite;
  const { conn } = await connection(t, () => new Promise(resolve => { finishWrite = resolve; }));
  const request = conn.request("clock_set", "CLOCK_ACK", {}, 20);
  const outcome = request.then(() => "success", error => error);
  try {
    const result = await Promise.race([outcome, new Promise(resolve => setTimeout(() => resolve("still pending"), 100))]);
    assert.notEqual(result, "still pending", "USB write must not bypass the request timeout");
    assert.ok(result instanceof Error);
  } finally { finishWrite?.(); await outcome; }
});
