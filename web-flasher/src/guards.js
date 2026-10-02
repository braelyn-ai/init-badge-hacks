import { md5 } from "js-md5";

export const hex = bytes => Array.from(bytes, b => b.toString(16).padStart(2, "0")).join("");
export const sha256 = async bytes => hex(new Uint8Array(await crypto.subtle.digest("SHA-256", bytes)));

export function verifySecurity(info) {
  if (!(info instanceof Uint8Array) || info.length !== 20) throw new Error("Could not verify chip security settings. Nothing was written.");
  const fields = new DataView(info.buffer, info.byteOffset, info.byteLength);
  const flags = fields.getUint32(0, true);
  const encryption = info[4].toString(2).replaceAll("0", "").length % 2 === 1;
  if (fields.getUint32(12, true) !== 9 || (flags & 5) || encryption)
    throw new Error("This installer supports standard ESP32-S3 devices without secure boot or flash encryption. Nothing was written.");
}

export function verifyBootSelector(bytes) {
  if (bytes.length !== 8192) throw new Error("Incomplete boot-selection read. Nothing was written.");
  if (bytes.every(b => b === 255)) return;
  // Pinned IDF bootloader's first-boot app0 selector. Reject all other OTA states
  // instead of overwriting a user's selector or guessing the active partition.
  const expected = new Uint8Array(8192).fill(255);
  const view = new DataView(expected.buffer);
  view.setUint32(0, 1, true);
  view.setUint32(24, 2, true);
  view.setUint32(28, 0x4743989a, true);
  if (bytes.every((b, i) => b === expected[i])) return;
  // Exact Arduino-ESP32 3.3.10 tools/partitions/boot_app0.bin, also written by
  // the established Arduino CLI upload wrapper for our native firmware.
  // Both entries are UNDEFINED with valid CRCs; IDF chooses the larger sequence
  // (1, rather than 0), which maps to app0. Keep every padding byte constrained.
  // Upstream SHA-256: f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0
  view.setUint32(24, 0xffffffff, true);
  view.setUint32(4096, 0, true);
  if (bytes.every((b, i) => b === expected[i])) return;
  throw new Error("This badge uses a different boot-selection state. Use the project tools for this update. Nothing was written.");
}

export async function verifiedRead(loader, transport, offset, size) {
  const bytes = await loader.readFlash(offset, size);
  // esptool-js 0.6.1 leaves this trailing stub digest unread. Consume and verify
  // it before issuing another command; otherwise commands become misaligned.
  const digest = await transport.read(3000);
  if (!(digest instanceof Uint8Array) || digest.length !== 16 || bytes.length !== size || hex(digest) !== md5(bytes))
    throw new Error("USB read verification failed. Nothing was written.");
  return bytes;
}

export function validateManifest(release) {
  if (release.schema_version !== 1 || release.build_id !== "v1.0.0" ||
      release.hardware?.chip !== "esp32s3" || release.hardware?.flash_bytes !== 16777216 ||
      release.hardware?.psram_bytes !== 8388608 || release.flash?.erase_all !== false ||
      release.flash?.requires_partition_match !== true || release.partition_table?.offset !== 0x8000 ||
      release.partition_table?.sector_size !== 4096 || !/^[a-f0-9]{64}$/.test(release.partition_table?.sector_sha256))
    throw new Error("The release manifest is not compatible with this installer.");
  const allowed = new Map([[0, 0x8000], [0x8000, 0x1000], [0x10000, 0x300000]]);
  if (!Array.isArray(release.artifacts) || release.artifacts.length !== 3) throw new Error("Incomplete release.");
  for (const artifact of release.artifacts) {
    if (!allowed.has(artifact.offset) || !Number.isInteger(artifact.size_bytes) || artifact.size_bytes <= 0 ||
        artifact.size_bytes > allowed.get(artifact.offset) || !/^[a-f0-9]{64}$/.test(artifact.sha256) ||
        !/^[a-z0-9-]+\.bin$/.test(artifact.path)) throw new Error("Unexpected firmware component.");
    allowed.delete(artifact.offset);
  }
  return release;
}

export async function validateArtifact(artifact, bytes) {
  if (bytes.length !== artifact.size_bytes || await sha256(bytes) !== artifact.sha256)
    throw new Error("A firmware download did not pass verification. Reload and try again.");
}
