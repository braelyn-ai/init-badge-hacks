import test from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { inflateSync } from "node:zlib";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import {
  FLASH_BYTES, BACKUP_CHUNK_BYTES, APPROVED_FACTORY_SOURCE, TARGET_PARTITIONS,
  TARGET_SECTOR_SHA256, FACTORY_ERASED_RANGES, parsePartitionSector,
  inspectFactorySource, createFactoryPlan, backupFactoryFlash, saveFactoryBackup,
  revalidateFactoryBeforeWrite, verifyFactoryAfterWrite,
} from "../src/factory.js";

// The pinned browser package has extensionless imports; use the same resolver
// as the production bundle instead of replacing its implementation in tests.
const bundled = await build({ stdin: { contents: 'export { ESPLoader } from "esptool-js";',
  resolveDir: fileURLToPath(new URL("..", import.meta.url)) },
  bundle: true, write: false, format: "esm", platform: "browser", target: "chrome120" });
const bundleSource = `${bundled.outputFiles[0].text}\n//# sourceURL=esptool-test-bundle.js\n`;
const { ESPLoader } = await import(`data:text/javascript;base64,${Buffer.from(bundleSource).toString("base64")}`);

const hash = bytes => createHash("sha256").update(bytes).digest("hex");
const md5 = bytes => createHash("md5").update(bytes).digest("hex");
const digest = bytes => new Uint8Array(createHash("md5").update(bytes).digest());

// Synthetic metadata-only sectors. No bytes from a device backup are fixtures.
function table(entries, length = 4096) {
  const bytes = new Uint8Array(length).fill(255);
  const view = new DataView(bytes.buffer);
  entries.forEach((entry, index) => {
    const at = index * 32;
    view.setUint16(at, 0x50aa, true);
    bytes[at + 2] = entry.type; bytes[at + 3] = entry.subtype;
    view.setUint32(at + 4, entry.offset, true);
    view.setUint32(at + 8, entry.size_bytes, true);
    bytes.fill(0, at + 12, at + 28);
    bytes.set(new TextEncoder().encode(entry.name), at + 12);
    view.setUint32(at + 28, entry.flags, true);
  });
  const marker = entries.length * 32;
  bytes[marker] = 0xeb; bytes[marker + 1] = 0xeb;
  bytes.set(digest(bytes.subarray(0, marker)), marker + 16);
  return bytes;
}
const sourceSector = () => table(APPROVED_FACTORY_SOURCE.partitions);
const targetSector = () => table(TARGET_PARTITIONS);

function fixture() {
  const files = [
    { address: 0, data: new Uint8Array([0xe9, 1, 2, 3]) },
    { address: 0x8000, data: table(TARGET_PARTITIONS, 3072) },
    { address: 0x10000, data: new Uint8Array(0x8001).fill(0xa5) },
  ];
  const release = {
    schema_version: 1, build_id: "v1.2.0",
    hardware: { chip: "esp32s3", flash_bytes: FLASH_BYTES, psram_bytes: 8388608 },
    flash: { erase_all: false, requires_partition_match: true },
    partition_table: { offset: 0x8000, sector_size: 4096, padding_byte: 255,
      sector_sha256: TARGET_SECTOR_SHA256, layout: structuredClone(TARGET_PARTITIONS) },
    artifacts: files.map((file, index) => ({ offset: file.address, size_bytes: file.data.length,
      path: ["bootloader.bin", "partition-table.bin", "firmware.bin"][index], sha256: hash(file.data) })),
  };
  return { files, release };
}

function device({ sector = sourceSector(), transformRead, trailer, fullDigest } = {}) {
  const flash = new Uint8Array(FLASH_BYTES).fill(0x5a);
  flash.set(sector, 0x8000);
  const events = [];
  let lastBytes;
  return {
    flash, events,
    loader: {
      async readFlash(offset, size) {
        events.push(["read", offset, size]);
        lastBytes = flash.slice(offset, offset + size);
        if (transformRead) lastBytes = transformRead(lastBytes, offset);
        return lastBytes;
      },
      async flashMd5sum(offset, size) {
        events.push(["md5", offset, size]);
        return fullDigest ?? md5(flash.subarray(offset, offset + size));
      },
      async writeFlash() { throw new Error("Policy/backup helpers must never write flash"); },
    },
    transport: { async read() { events.push(["trailer"]); return trailer === undefined ? digest(lastBytes) : trailer; } },
  };
}

test("approved factory metadata recreates exactly the reviewed sector, including MD5 and padding", async () => {
  assert.equal(hash(sourceSector()), APPROVED_FACTORY_SOURCE.sectorSha256);
  assert.equal(hash(targetSector()), TARGET_SECTOR_SHA256);
  assert.deepEqual(parsePartitionSector(sourceSector()), APPROVED_FACTORY_SOURCE.partitions);
  assert.deepEqual(parsePartitionSector(targetSector()), TARGET_PARTITIONS);
  assert.equal(await inspectFactorySource(sourceSector(), fixture().release), APPROVED_FACTORY_SOURCE);
  assert.equal(APPROVED_FACTORY_SOURCE.partitions.length, 7);
  assert.equal(Object.isFrozen(APPROVED_FACTORY_SOURCE.partitions[0]), true);
});

test("first install rejects prepared, unknown, corrupt, padded differently, and incomplete layouts", async () => {
  const release = fixture().release;
  await assert.rejects(inspectFactorySource(targetSector(), release), /already prepared.*Update badge/);
  for (const at of [0, 3, 7, 12, 28, 224, 226, 240, 255, 256, 3072, 4095]) {
    const altered = sourceSector(); altered[at] ^= 1;
    await assert.rejects(inspectFactorySource(altered, release), /not recognized/);
  }
  const unknown = structuredClone(APPROVED_FACTORY_SOURCE.partitions);
  unknown[0].name = "settings";
  await assert.rejects(inspectFactorySource(table(unknown), release), /not recognized/);
  for (const bytes of [null, [], new Uint8Array(3072), new Uint8Array(4095), new Uint8Array(4097)])
    await assert.rejects(inspectFactorySource(bytes, release), /Incomplete/);
});

test("parser rejects bad checksum, checksum marker, padding, alignment, overlap, and labels", () => {
  for (const at of [224 + 2, 224 + 16, 256, 4095]) {
    const altered = sourceSector(); altered[at] ^= 1;
    assert.throws(() => parsePartitionSector(altered), /checksum|padding/);
  }
  for (const mutate of [
    rows => rows[0].offset++, rows => rows[0].size_bytes++,
    rows => rows[1].offset = rows[0].offset, rows => rows[0].size_bytes = 0,
    rows => rows[6].size_bytes = FLASH_BYTES, rows => rows[0].name = "",
  ]) {
    const entries = structuredClone(APPROVED_FACTORY_SOURCE.partitions); mutate(entries);
    assert.throws(() => parsePartitionSector(table(entries)), /boundaries|label/);
  }
  const viewOffset = new Uint8Array(4103); viewOffset.set(sourceSector(), 7);
  assert.deepEqual(parsePartitionSector(viewOffset.subarray(7)), APPROVED_FACTORY_SOURCE.partitions);
});

test("factory plan verifies downloads and contains only the exact approved write/erase ranges", async () => {
  const { files, release } = fixture();
  const plan = await createFactoryPlan(release, files, APPROVED_FACTORY_SOURCE);
  assert.equal(plan.eraseAll, false);
  assert.equal(plan.sourceSectorSha256, APPROVED_FACTORY_SOURCE.sectorSha256);
  assert.equal(plan.targetSectorSha256, TARGET_SECTOR_SHA256);
  assert.deepEqual(plan.erasedRanges, FACTORY_ERASED_RANGES);
  assert.equal(plan.fileArray.length, 56, "three artifacts, settings wipe, and 52 bounded data erases");
  for (const file of files) {
    const destination = plan.fileArray.find(item => item.address === file.address);
    if (file.address === 0x8000) {
      assert.equal(destination.data.length, 4096);
      assert.equal(hash(destination.data), TARGET_SECTOR_SHA256);
    } else assert.deepEqual(destination.data, file.data);
    assert.notEqual(destination.data, file.data, "copy approved bytes so a download buffer cannot mutate the plan");
  }
  const erased = plan.fileArray.filter(file => !files.some(original => original.address === file.address));
  assert.equal(erased.reduce((sum, file) => sum + file.data.length, 0), 0xcf7000);
  let end = 0;
  for (const file of plan.fileArray) {
    assert.equal(file.address % 4096, 0);
    assert.ok(file.address >= end);
    end = file.address + Math.ceil(file.data.length / 4096) * 4096;
    assert.ok(end <= FLASH_BYTES);
  }
  for (const file of erased) {
    assert.ok(file.data.every(value => value === 255));
    assert.ok(file.data.length <= BACKUP_CHUNK_BYTES);
    assert.equal(file.data.length % 4096, 0);
  }
  assert.deepEqual(erased[0], { address: 0x9000, data: new Uint8Array(0x7000).fill(255) });
  let expected = 0x310000;
  for (const file of erased.slice(1)) { assert.equal(file.address, expected); expected += file.data.length; }
  assert.equal(expected, FLASH_BYTES);
});

test("plan fails closed on changed manifests, incomplete downloads, bad hashes, duplicates, or unapproved source", async () => {
  for (const mutate of [
    f => f.files.pop(), f => f.files.push(f.files[0]),
    f => f.files[0].data[0] ^= 1, f => f.files[0].address = 0x9000,
    f => f.files[1].data = f.files[1].data.subarray(0, 1024),
    f => f.release.partition_table.layout[0].size_bytes = 0x4000,
    f => f.release.partition_table.sector_sha256 = "a".repeat(64),
    f => f.release.partition_table.padding_byte = 0,
    f => f.release.flash.erase_all = true,
    f => f.release.build_id = "conference-factory-4",
  ]) {
    const f = fixture(); mutate(f);
    await assert.rejects(createFactoryPlan(f.release, f.files, APPROVED_FACTORY_SOURCE));
  }
  const f = fixture();
  for (const source of [null, {}, { ...APPROVED_FACTORY_SOURCE }])
    await assert.rejects(createFactoryPlan(f.release, f.files, source), /approved factory layout/);
});

test("pinned esptool compressed writer keeps factory erases bounded and verifies every planned component", async () => {
  const { files, release } = fixture();
  const plan = await createFactoryPlan(release, files, APPROVED_FACTORY_SOURCE);
  const flash = new Uint8Array(FLASH_BYTES).fill(0x33);
  const loader = new ESPLoader({ transport: { getInfo: () => "simulated USB" }, baudrate: 115200,
    terminal: { clean() {}, write() {}, writeLine() {} } });
  loader.IS_STUB = true;
  loader.chip = { CHIP_NAME: "ESP32-S3", BOOTLOADER_FLASH_OFFSET: 0 };
  let current; const written = []; const checks = [];
  // Exercise the real pinned writer, compression, padding, command encoding,
  // and MD5 verification. Only the USB command boundary is a simulated device.
  loader.checkCommand = async (_description, command, packet, _checksum, _length, timeout) => {
    const view = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
    if (command === loader.ESP_FLASH_DEFL_BEGIN) {
      assert.equal(current, undefined);
      current = { size: view.getUint32(0, true), address: view.getUint32(12, true), packets: [] };
      assert.equal(current.address % 4096, 0);
      return 0;
    }
    if (command === loader.ESP_FLASH_DEFL_DATA) {
      assert.equal(view.getUint32(4, true), current.packets.length);
      current.packets.push(packet.slice(16));
      return 0;
    }
    if (command === loader.ESP_FLASH_DEFL_END) {
      assert.equal(view.getUint32(0, true), 1, "do not reset between components");
      const bytes = inflateSync(Buffer.concat(current.packets));
      assert.equal(bytes.length, current.size);
      if (current.address >= 0x310000 || current.address === 0x9000) {
        assert.ok(current.size <= BACKUP_CHUNK_BYTES);
        assert.ok(timeout <= 11000, "no single long data erase blocks progress for minutes");
      }
      const end = current.address + Math.ceil(bytes.length / 4096) * 4096;
      assert.ok(end <= FLASH_BYTES);
      flash.fill(255, current.address, end);
      flash.set(bytes, current.address);
      written.push({ address: current.address, size: bytes.length }); current = undefined;
      return 0;
    }
    if (command === loader.ESP_SPI_FLASH_MD5) {
      const address = view.getUint32(0, true), size = view.getUint32(4, true);
      checks.push({ address, size });
      return digest(flash.subarray(address, address + size));
    }
    assert.fail(`Unexpected command ${command}; no chip erase, security, or uncompressed writes permitted`);
  };
  await loader.writeFlash({ fileArray: plan.fileArray, eraseAll: false, compress: true,
    flashSize: "keep", flashMode: "keep", flashFreq: "keep", calculateMD5Hash: md5 });
  assert.equal(written.length, 56); assert.deepEqual(checks, written);
  for (const file of plan.fileArray)
    assert.deepEqual(flash.subarray(file.address, file.address + file.data.length), file.data);
  assert.ok(flash.subarray(0x6000, 0x8000).every(value => value === 0x33), "bootloader padding never crosses into table");
  assert.ok(flash.subarray(0xe000, 0x10000).every(value => value === 255), "new OTA selector is entirely blank");
  assert.equal(flash[0x10000], 0xa5, "settings wipe does not extend into app0");
  assert.ok(flash.subarray(0x20000, 0x310000).every(value => value === 0x33), "no unlisted whole-chip erase");
});

test("backup consumes each stub digest, reads all 16 MiB in bounded chunks, then independently verifies the full flash", async () => {
  const d = device(); const progress = [];
  const backup = await backupFactoryFlash(d.loader, d.transport, APPROVED_FACTORY_SOURCE, { reportProgress: n => progress.push(n) });
  assert.equal(backup.bytes.length, FLASH_BYTES);
  assert.equal(backup.sha256, hash(d.flash)); assert.equal(backup.md5, md5(d.flash));
  assert.deepEqual(backup.bytes, d.flash);
  assert.equal(progress[0], 0); assert.equal(progress.at(-1), 1); assert.equal(progress.length, 65);
  for (let index = 0; index < 64; index++) {
    assert.deepEqual(d.events[index * 2], ["read", index * BACKUP_CHUNK_BYTES, BACKUP_CHUNK_BYTES]);
    assert.deepEqual(d.events[index * 2 + 1], ["trailer"]);
  }
  assert.deepEqual(d.events.at(-1), ["md5", 0, FLASH_BYTES]);
  assert.equal(Object.isFrozen(backup), true);
});

test("backup rejects truncated reads, bad or missing trailers, unknown source sectors, and mismatched second checksum", async () => {
  for (const options of [
    { transformRead: bytes => bytes.subarray(1) },
    { trailer: new Uint8Array(16) }, { trailer: null },
    { sector: targetSector() }, { fullDigest: "0".repeat(32) },
  ]) {
    const d = device(options);
    await assert.rejects(backupFactoryFlash(d.loader, d.transport, APPROVED_FACTORY_SOURCE));
  }
  const d = device();
  await assert.rejects(backupFactoryFlash(d.loader, d.transport, {}), /approved factory layout/);
  assert.equal(d.events.length, 0);
});

test("backup honours cancellation and the total deadline between ordered USB operations", async () => {
  const before = device(), cancelled = new AbortController(); cancelled.abort();
  await assert.rejects(backupFactoryFlash(before.loader, before.transport, APPROVED_FACTORY_SOURCE,
    { signal: cancelled.signal }), /cancelled.*Nothing was written/);
  assert.equal(before.events.length, 0);
  for (const reason of ["cancel", "timeout"]) {
    const d = device(), controller = new AbortController(); let clock = 0;
    await assert.rejects(backupFactoryFlash(d.loader, d.transport, APPROVED_FACTORY_SOURCE, {
      signal: controller.signal, now: () => clock, timeoutMs: 100,
      reportProgress(fraction) {
        if (fraction > 0) { if (reason === "cancel") controller.abort(); else clock = 100; }
      },
    }), reason === "cancel" ? /cancelled/ : /timed out/);
    assert.deepEqual(d.events, [["read", 0, BACKUP_CHUNK_BYTES], ["trailer"]], "drain each read before stopping; no overlapping commands");
  }
});

test("first write requires a verified saved backup and rejects save failure, fake receipts, or changed recovery bytes", async () => {
  const d = device(); const source = APPROVED_FACTORY_SOURCE;
  const backup = await backupFactoryFlash(d.loader, d.transport, source);
  await assert.rejects(revalidateFactoryBeforeWrite(d.loader, d.transport, backup, source), /not been saved/);
  await assert.rejects(saveFactoryBackup(backup, async () => { throw new Error("Disk full"); }), /Disk full/);
  await assert.rejects(saveFactoryBackup(backup, async () => false), /Save and verify/);
  await assert.rejects(saveFactoryBackup({ ...backup }, async () => true), /missing or changed/);
  await assert.rejects(revalidateFactoryBeforeWrite(d.loader, d.transport, backup, source), /not been saved/);
  assert.equal(await saveFactoryBackup(backup, async received => received === backup), backup);
  await assert.doesNotReject(revalidateFactoryBeforeWrite(d.loader, d.transport, backup, source));
  backup.bytes[0] ^= 1;
  await assert.rejects(revalidateFactoryBeforeWrite(d.loader, d.transport, backup, source), /missing or changed/);
});

test("prewrite check detects a changed device outside the table and a different source table", async () => {
  const d = device(); const source = APPROVED_FACTORY_SOURCE;
  const backup = await backupFactoryFlash(d.loader, d.transport, source);
  await saveFactoryBackup(backup, async () => true);
  d.flash[0x9000] ^= 1;
  await assert.rejects(revalidateFactoryBeforeWrite(d.loader, d.transport, backup, source), /device changed/);
  d.flash[0x9000] ^= 1;
  d.flash.set(targetSector(), 0x8000);
  await assert.rejects(revalidateFactoryBeforeWrite(d.loader, d.transport, backup, source), /not approved/);
});

test("postwrite check requires the exact target table and completely blank boot selector before first boot", async () => {
  const d = device({ sector: targetSector() });
  d.flash.fill(255, 0xe000, 0x10000);
  await assert.doesNotReject(verifyFactoryAfterWrite(d.loader, d.transport, fixture().release));
  d.flash[0xe000] = 1;
  await assert.rejects(verifyFactoryAfterWrite(d.loader, d.transport, fixture().release), /boot selection.*recovery backup/);
  d.flash.fill(255, 0xe000, 0x10000); d.flash[0x8fff] = 0;
  await assert.rejects(verifyFactoryAfterWrite(d.loader, d.transport, fixture().release), error => {
    assert.match(error.message, /recovery backup/);
    assert.doesNotMatch(error.message, /Nothing was written/);
    return true;
  });
});
