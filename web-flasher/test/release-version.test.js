import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

// The installer, its manifest guard and the firmware must agree on one release.
test("installer and guard pins match firmware version.txt", () => {
  const version = readFileSync(new URL("../../firmware/factory_badge/version.txt", import.meta.url), "utf8").trim();
  const expected = `"v${version}"`;
  assert.match(readFileSync(new URL("../src/installer.js", import.meta.url), "utf8"), new RegExp(`const BUILD = ${expected};`));
  assert.match(readFileSync(new URL("../src/guards.js", import.meta.url), "utf8"), new RegExp(`build_id !== ${expected}`));
});
