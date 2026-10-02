#!/usr/bin/env python3
"""Synthetic, device-free tests of the public preservation-release packager."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("web_release", ROOT / "scripts/package-web-release.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
BUILD = "conference-factory-fixture"


def partition_table(entries=None):
    entries = entries if entries is not None else package.layout.EXPECTED_PARTITIONS
    data = b"".join(struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size, name.encode(), flags)
                    for kind, subtype, offset, size, name, flags in entries)
    data += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(data).digest()
    return data.ljust(0xC00, b"\xff")


def image(app=False, build_id=BUILD, project="conference_badge", chip=9, mode=2, frequency_size=0x4F, version="abc1234-dirty"):
    header = bytearray(24)
    header[:4] = bytes([0xE9, 1, mode, frequency_size])
    struct.pack_into("<H", header, 12, chip)
    header[23] = 1
    payload = bytearray(256 if app else 64)
    if app:
        struct.pack_into("<I", payload, 0, 0xABCD5432)
        for offset, text in ((16, version), (48, project), (80, "12:34:56"), (96, "Sep 17 2026"), (112, "v5.5.4")):
            payload[offset:offset + len(text)] = text.encode()
        payload[144:176] = hashlib.sha256(b"synthetic ELF").digest()
        payload += build_id.encode() + b"\0"
    checksum = 0xEF
    for byte in payload:
        checksum ^= byte
    data = bytes(header) + struct.pack("<II", 0x3C000020, len(payload)) + payload
    data += b"\0" * (15 - len(data) % 16) + bytes([checksum])
    return data + hashlib.sha256(data).digest()


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name)
        self.artifacts = self.repo / ".build/factory"
        self.output = self.repo / ".build/web-release/releases"
        self.artifacts.mkdir(parents=True)
        (self.repo / ".gitignore").write_text(".build/\n")
        (self.repo / "README.md").write_text("Synthetic release fixture\n")
        self.git("init", "--initial-branch=fixture")
        self.git("add", ".gitignore", "README.md")
        self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "-c", "commit.gpgsign=false", "-c", "core.hooksPath=/dev/null", "commit", "-m", "fixture")
        self.write("firmware", image(app=True))
        self.write("bootloader", image())
        self.write("partition-table", partition_table())

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.repo), *args], check=True, capture_output=True).stdout

    def path(self, name):
        return self.artifacts / next(item[1] for item in package.ARTIFACTS if item[0] == name)

    def write(self, name, data):
        path = self.path(name)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def pack(self, **kwargs):
        return package.package_release(self.artifacts, self.output, self.repo, BUILD, **kwargs)

    def mutate_manifest(self, release, callback):
        path = release / "release.json"
        value = json.loads(path.read_text())
        callback(value)
        path.write_text(json.dumps(value))

    def test_exports_only_approved_images_and_exact_padded_sector_hash(self):
        for filename in ("ota_data_initial.bin", "nvs.bin", "filesystem.bin", "full-backup.bin"):
            (self.artifacts / filename).write_bytes(b"PRIVATE DEVICE DATA MUST NOT SHIP")
        release = self.pack()
        self.assertEqual({p.name for p in release.iterdir()}, {"release.json", "firmware.bin", "bootloader.bin", "partition-table.bin"})
        manifest = package.verify_release(release)
        self.assertEqual([item["offset"] for item in manifest["artifacts"]], [0, 0x8000, 0x10000])
        self.assertEqual(manifest["partition_table"]["sector_sha256"], hashlib.sha256(partition_table().ljust(4096, b"\xff")).hexdigest())
        self.assertFalse(manifest["flash"]["erase_all"])
        self.assertTrue(manifest["flash"]["requires_partition_match"])
        self.assertIn("otadata", manifest["flash"]["preserve_partitions"])
        self.assertEqual(manifest["embedded_build"]["project_version"], "abc1234-dirty")
        for path in release.iterdir():
            self.assertNotIn(b"PRIVATE DEVICE DATA", path.read_bytes())
            self.assertNotIn(str(self.repo).encode(), path.read_bytes())

    def test_source_metadata_is_honest_about_current_dirty_worktree(self):
        clean = package.source_metadata(self.repo)
        self.assertFalse(clean["dirty"])
        (self.repo / "README.md").write_text("Uncommitted source edit\n")
        (self.repo / "new-source.cpp").write_text("// untracked source\n")
        source = json.loads((self.pack() / "release.json").read_text())["source"]
        self.assertTrue(source["dirty"])
        self.assertFalse(source["artifact_source_verified"])
        self.assertEqual(source["commit"], clean["commit"])
        self.assertEqual(source["branch"], "fixture")
        self.assertNotEqual(source["status_sha256"], clean["status_sha256"])
        self.assertEqual(source["capture_scope"], "packaging_worktree")

    def test_releases_are_immutable(self):
        release = self.pack()
        before = (release / "release.json").read_bytes()
        with self.assertRaisesRegex(RuntimeError, "never overwritten"):
            self.pack()
        self.assertEqual((release / "release.json").read_bytes(), before)

    def test_source_hash_mismatch_rejected_without_partial_release(self):
        with self.assertRaisesRegex(RuntimeError, "expected SHA-256"):
            self.pack(expected_hashes={"firmware": "0" * 64})
        self.assertFalse(self.output.exists())

    def test_reviewed_source_hashes_accepted(self):
        expected = {name: hashlib.sha256(self.path(name).read_bytes()).hexdigest() for name, *_ in package.ARTIFACTS}
        package.verify_release(self.pack(expected_hashes=expected))

    def test_factory_or_changed_partition_map_is_rejected(self):
        entries = list(package.layout.EXPECTED_PARTITIONS)
        entries[0] = (1, 2, 0x9000, 0x4000, "nvs", 0)
        self.write("partition-table", partition_table(entries))
        with self.assertRaisesRegex(RuntimeError, "exact preservation layout"):
            self.pack()

    def test_partition_checksum_mismatch_rejected(self):
        data = bytearray(partition_table()); data[10] ^= 1
        self.write("partition-table", data)
        with self.assertRaisesRegex(RuntimeError, "checksum mismatch"):
            self.pack()

    def test_bootloader_cannot_overlap_partition_table(self):
        self.write("bootloader", b"x" * (0x8000 + 1))
        with self.assertRaisesRegex(RuntimeError, "flash bounds"):
            self.pack()

    def test_app_cannot_overrun_app0(self):
        self.write("firmware", b"x" * (0x300000 + 1))
        with self.assertRaisesRegex(RuntimeError, "flash bounds"):
            self.pack()

    def test_wrong_chip_or_flash_configuration_rejected(self):
        for data in (image(app=True, chip=0), image(app=True, mode=0), image(app=True, frequency_size=0x2F)):
            with self.subTest(header=data[:24]):
                self.write("firmware", data)
                with self.assertRaises(RuntimeError):
                    self.pack()

    def test_corrupt_or_truncated_image_rejected(self):
        original = image(app=True)
        corrupt_segment = bytearray(original); corrupt_segment[100] ^= 1
        corrupt_hash = bytearray(original); corrupt_hash[-1] ^= 1
        for data in (bytes(corrupt_segment), bytes(corrupt_hash), original[:-12], original + b"extra"):
            with self.subTest(length=len(data)):
                self.write("firmware", data)
                with self.assertRaises(RuntimeError):
                    self.pack()

    def test_wrong_build_or_project_rejected(self):
        for data in (image(app=True, build_id="another-build"), image(app=True, project="another_project")):
            self.write("firmware", data)
            with self.assertRaises(RuntimeError):
                self.pack()

    def test_semver_build_id_must_match_app_version(self):
        self.write("firmware", image(app=True, build_id="v1.0.0", version="abc1234-dirty"))
        with self.assertRaisesRegex(RuntimeError, "does not match the app version"):
            package.package_release(self.artifacts, self.output, self.repo, "v1.0.0")
        self.write("firmware", image(app=True, build_id="v1.0.0", version="1.0.0"))
        release = package.package_release(self.artifacts, self.output, self.repo, "v1.0.0")
        manifest = json.loads((release / "release.json").read_text())
        self.assertEqual((manifest["build_id"], manifest["channel"]), ("v1.0.0", "stable"))

    def test_input_symlink_rejected(self):
        path = self.path("firmware"); data = path.read_bytes(); path.unlink()
        alternate = self.repo / "private.bin"; alternate.write_bytes(data); path.symlink_to(alternate)
        with self.assertRaisesRegex(RuntimeError, "symlinked"):
            self.pack()

    def test_output_must_be_ignored_and_build_id_cannot_traverse(self):
        with self.assertRaisesRegex(RuntimeError, "ignored .build"):
            package.package_release(self.artifacts, self.repo / "public", self.repo, BUILD)
        for value in ("../escape", "", "A-Build", "hello/world", "a..b"):
            with self.assertRaisesRegex(RuntimeError, "Build ID"):
                package.package_release(self.artifacts, self.output, self.repo, value)

    def test_verifier_rejects_modified_artifact(self):
        release = self.pack(); path = release / "firmware.bin"; data = bytearray(path.read_bytes()); data[-1] ^= 1; path.write_bytes(data)
        with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
            package.verify_release(release)

    def test_verifier_rejects_unsafe_manifest_paths_offsets_and_erase(self):
        release = self.pack(); original = (release / "release.json").read_bytes()
        mutations = [
            lambda m: m["artifacts"][0].update(path="../private.bin"),
            lambda m: m["artifacts"][2].update(offset=0x9000),
            lambda m: m["flash"].update(erase_all=True),
            lambda m: m["partition_table"].update(sector_sha256="0" * 64),
            lambda m: m.update(limitations=[]),
            lambda m: m["source"].update(artifact_source_verified=True),
            lambda m: m["source"].update(dirty="false"),
            lambda m: m["flash"].update(erase_all=0),
            lambda m: m.update(schema_version=True),
            lambda m: m.update(build_id=123),
        ]
        for mutate in mutations:
            (release / "release.json").write_bytes(original)
            self.mutate_manifest(release, mutate)
            with self.assertRaises(RuntimeError):
                package.verify_release(release)

    def test_verifier_rejects_extra_private_file(self):
        release = self.pack(); (release / "nvs.bin").write_bytes(b"private")
        with self.assertRaisesRegex(RuntimeError, "unexpected files"):
            package.verify_release(release)

    def test_verifier_rejects_symlinked_artifact(self):
        release = self.pack(); path = release / "firmware.bin"; path.unlink(); path.symlink_to(self.path("firmware"))
        with self.assertRaisesRegex(RuntimeError, "invalid artifact file"):
            package.verify_release(release)


if __name__ == "__main__":
    unittest.main()
