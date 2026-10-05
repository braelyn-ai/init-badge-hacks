import { ESPLoader } from "esptool-js";
import { SafeTransport } from "./transport.js";
import { md5 } from "js-md5";
import { ClockConnection, delay, safeDiagnostics } from "./protocol.js";
import { sha256, verifiedRead, verifySecurity, verifyBootSelector, validateArtifact, validateManifest } from "./guards.js";
import { inspectFactorySource, createFactoryPlan, backupFactoryFlash, saveFactoryBackup,
  revalidateFactoryBeforeWrite, verifyFactoryAfterWrite } from "./factory.js";

// Keep equal to firmware/factory_badge/version.txt ("v" + version); test/release-version.test.js checks it.
const BUILD = "v1.2.0";
const RELEASE = `/stopwatch/install/releases/${BUILD}/`;
const $ = id => document.getElementById(id);
const supported = window.isSecureContext && "serial" in navigator && window.top === window.self;
const canSave = supported && typeof window.showSaveFilePicker === "function";
let busy = false, factorySession, diagnostics;

function status(message, progress = 0) {
  $("status").textContent = message;
  $("progress").value = progress;
}
function controls(active = busy) {
  busy = active;
  const locked = busy || !!factorySession || !supported;
  $("install").disabled = locked || !$("hardware").checked;
  $("prepare").disabled = locked || !canSave || !$("hardware").checked;
  $("clock").disabled = locked;
  $("finish-install").disabled = locked || !$("hardware").checked || !$("finish-confirm").checked;
  $("hardware").disabled = locked;
  $("finish-confirm").disabled = locked;
  $("factory-actions").hidden = !factorySession?.backup;
  $("save-backup").disabled = busy || !factorySession?.backup;
  $("replace-confirm").disabled = busy || !factorySession?.saved;
  $("factory-write").disabled = busy || !factorySession?.saved || !$("replace-confirm").checked;
  $("factory-cancel").disabled = busy;
  $("activity").setAttribute("aria-busy", String(busy));
}
function begin() {
  diagnostics = undefined;
  $("copy").disabled = true; $("copy").textContent = "Copy build details";
  $("details").textContent = ""; $("details").hidden = true;
  $("activity").dataset.result = "working";
  controls(true);
}
function failure(error, wrote = false, factory = false) {
  $("activity").dataset.result = "error";
  if (error.name === "NotFoundError" && !wrote) status("Cancelled. Nothing was written to the badge.");
  else status((wrote ? "Installation needs attention. " : "Installation stopped. ") +
    (wrote ? error.message.replaceAll("Nothing was written.", "") + (factory ? " Keep your recovery copy. See Finish an interrupted first install below." : " Keep USB connected and retry Set clock & check badge. If the firmware did not finish, retry Update badge.") : error.message));
}
function requestPort() {
  // Keep the USB chooser directly in the click gesture, before any await.
  return navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a, usbProductId: 0x1001 }] });
}
async function getRelease() {
  const response = await fetch(RELEASE + "release.json", { credentials: "omit", cache: "no-cache" });
  if (!response.ok) throw new Error("The release is unavailable. Please try again later.");
  return validateManifest(await response.json());
}
async function components(release) {
  return Promise.all(release.artifacts.map(async artifact => {
    const response = await fetch(RELEASE + artifact.path, { credentials: "omit" });
    if (!response.ok) throw new Error("A firmware component could not be downloaded.");
    const bytes = new Uint8Array(await response.arrayBuffer());
    await validateArtifact(artifact, bytes);
    return { data: bytes, address: artifact.offset };
  }));
}
async function connect(session) {
  status("Connecting to your StopWatch…", 10);
  session.transport = new SafeTransport(session.port, false);
  // Library connection logs contain a MAC; never surface or retain them.
  const loader = session.loader = new ESPLoader({ transport: session.transport, baudrate: 115200,
    terminal: { clean() {}, write() {}, writeLine() {} }, debugLogging: false });
  await loader.detectChip("default_reset");
  if (loader.chip.CHIP_NAME !== "ESP32-S3") throw new Error("This release is for M5Stack StopWatch only. Nothing was written.");
  verifySecurity(await loader.checkCommand("read security info", 0x14, new Uint8Array(), 0, 20, 3000));
  if (loader.chip.postConnect) await loader.chip.postConnect(loader);
  await loader.runStub();
  if (!loader.IS_STUB || await loader.detectFlashSize() !== "16MB")
    throw new Error("Could not verify the StopWatch's 16 MB flash. Nothing was written.");
}
async function checkPrepared(session) {
  const sector = await verifiedRead(session.loader, session.transport, 0x8000, 4096);
  if (await sha256(sector) !== session.release.partition_table.sector_sha256)
    throw new Error("This badge has factory firmware or a different storage layout. Choose First install for a new StopWatch. Nothing was written.");
  verifyBootSelector(await verifiedRead(session.loader, session.transport, 0xe000, 8192));
}
async function restart(session) {
  await session.transport.setRTS(true); await delay(100); await session.loader.after("hard_reset");
  session.reset = true;
  await session.transport.disconnect(); session.transport = undefined;
  await delay(1200);
}
async function close(session) {
  if (!session?.transport) return;
  if (session.loader && !session.reset) {
    try { await session.transport.setRTS(true); await delay(100); await session.loader.after("hard_reset"); } catch {}
  }
  try { await session.transport.disconnect(); } catch {}
  session.transport = undefined;
}
async function syncClock(port, initializeStorage = false) {
  const deadline = performance.now() + 15000;
  let connection;
  while (performance.now() < deadline) {
    connection = new ClockConnection(port);
    try { await connection.open(); break; }
    catch { await delay(400); connection = undefined; }
  }
  if (!connection) throw new Error("The badge is restarting. Keep it plugged in, then choose Set clock & check badge.");
  try {
    const ready = await connection.provision(BUILD, { initializeStorage });
    diagnostics = safeDiagnostics(ready, BUILD);
    $("copy").disabled = false;
    status("Ready. Firmware, storage, offline mode, and the advancing clock are verified.", 100);
    $("activity").dataset.result = "success";
  } finally { await connection.close(); }
}
async function write(session, files) {
  await session.loader.writeFlash({
    fileArray: files, eraseAll: false, compress: true,
    flashSize: "keep", flashMode: "keep", flashFreq: "keep",
    calculateMD5Hash: bytes => md5(bytes),
    reportProgress(index, written, total) {
      status("Installing the badge. Keep USB connected…", 20 + (index + written / total) / files.length * 65);
    }
  });
}
async function install() {
  if (busy || factorySession || !$("hardware").checked) return;
  const session = {}; let wrote = false;
  begin();
  try {
    session.port = await requestPort();
    status("Downloading and verifying the release…", 5);
    session.release = await getRelease();
    const files = await components(session.release);
    await connect(session);
    status("Checking the existing storage layout…", 15);
    await checkPrepared(session);
    wrote = true;
    await write(session, files);
    status("Firmware verified. Restarting and setting your local time…", 90);
    await restart(session);
    await syncClock(session.port);
  } catch (error) { failure(error, wrote); }
  finally { await close(session); controls(false); }
}
async function prepareFactory() {
  if (busy || factorySession || !canSave || !$("hardware").checked) return;
  const session = {}; begin();
  try {
    session.port = await requestPort();
    status("Downloading and verifying the release…", 5);
    session.release = await getRelease();
    session.files = await components(session.release);
    await connect(session);
    const sector = await verifiedRead(session.loader, session.transport, 0x8000, 4096);
    session.source = await inspectFactorySource(sector, session.release);
    // Validate the entire plan before asking the user to keep a recovery file.
    await createFactoryPlan(session.release, session.files, session.source);
    session.backup = await backupFactoryFlash(session.loader, session.transport, session.source, {
      reportProgress(value) { status("Reading your factory recovery copy. This can take several minutes. Keep USB connected…", 15 + value * 70); }
    });
    factorySession = session;
    $("replace-confirm").checked = false;
    $("backup-state").textContent = "Recovery copy verified. Save it to your computer before continuing. It stays private and is never uploaded.";
    status("Nothing has been changed. Save your recovery copy to continue.", 0);
  } catch (error) { failure(error); await close(session); }
  finally { controls(false); }
}
async function saveBackup() {
  const session = factorySession;
  if (busy || !session?.backup) return;
  controls(true);
  try {
    // Open the picker before hashing or other awaits consume user activation.
    const handlePromise = window.showSaveFilePicker({
      suggestedName: `stopwatch-factory-recovery-${session.backup.sha256.slice(0, 12)}.bin`,
      types: [{ description: "StopWatch recovery backup", accept: { "application/octet-stream": [".bin"] } }]
    });
    const handle = await handlePromise;
    status("Saving and checking your recovery copy…", 0);
    await saveFactoryBackup(session.backup, async backup => {
      const writable = await handle.createWritable();
      try { await writable.write(backup.bytes); await writable.close(); }
      catch (error) { try { await writable.abort(); } catch {} throw error; }
      const saved = await handle.getFile();
      return saved.size === backup.bytes.length && await sha256(await saved.arrayBuffer()) === backup.sha256;
    });
    session.saved = true;
    $("backup-state").textContent = "Recovery copy saved and checked. Keep this file somewhere safe; it contains this badge’s original software and any saved data.";
    status("Recovery copy saved. Confirm below to replace the factory software and storage.");
  } catch (error) {
    if (error.name === "AbortError") status("Save cancelled. Nothing was changed. Save the recovery copy to continue.");
    else failure(error);
  } finally { controls(false); }
}
async function installFactory() {
  const session = factorySession;
  if (busy || !session?.saved || !$("replace-confirm").checked) return;
  begin(); let wrote = false;
  try {
    status("Rechecking the badge against your recovery copy…", 5);
    await revalidateFactoryBeforeWrite(session.loader, session.transport, session.backup, session.source);
    const plan = await createFactoryPlan(session.release, session.files, session.source);
    wrote = true;
    await write(session, plan.fileArray);
    await verifyFactoryAfterWrite(session.loader, session.transport, session.release);
    status("Firmware verified. Preparing storage and setting your local time…", 90);
    await restart(session);
    await syncClock(session.port, true);
  } catch (error) { failure(error, wrote, true); }
  finally { await close(session); factorySession = undefined; $("replace-confirm").checked = false; controls(false); }
}
async function finishFactory() {
  if (busy || factorySession || !$("hardware").checked || !$("finish-confirm").checked) return;
  const session = {}; begin(); let prepared = false;
  try {
    session.port = await requestPort();
    session.release = await getRelease();
    await connect(session);
    await checkPrepared(session);
    await restart(session);
    prepared = true;
    status("Checking the installed firmware, preparing storage if needed, and setting your local time…", 90);
    await syncClock(session.port, true);
  } catch (error) { failure(error, prepared, true); }
  finally { await close(session); $("finish-confirm").checked = false; controls(false); }
}

$("install").addEventListener("click", install);
$("prepare").addEventListener("click", prepareFactory);
$("save-backup").addEventListener("click", saveBackup);
$("factory-write").addEventListener("click", installFactory);
$("finish-install").addEventListener("click", finishFactory);
$("factory-cancel").addEventListener("click", async () => {
  if (busy || !factorySession) return;
  controls(true); await close(factorySession); factorySession = undefined;
  $("replace-confirm").checked = false;
  status("First install cancelled. Nothing was written to the badge."); controls(false);
});
$("clock").addEventListener("click", async () => {
  if (busy || factorySession) return;
  begin();
  try {
    const port = await requestPort();
    status("Setting your local time and checking the badge…", 90);
    await syncClock(port);
  } catch (error) { failure(error); }
  finally { controls(false); }
});
for (const id of ["hardware", "replace-confirm", "finish-confirm"]) $(id).addEventListener("change", () => controls());
$("copy").addEventListener("click", async () => {
  if (!diagnostics) return;
  try { await navigator.clipboard.writeText(JSON.stringify(diagnostics, null, 2)); $("copy").textContent = "Copied"; }
  catch { $("details").textContent = JSON.stringify(diagnostics, null, 2); $("details").hidden = false; }
});
$("report").addEventListener("click", () => {
  const body = "## What happened?\n\n\n## What did you expect?\n\n\n## Steps to reproduce\n\n\n## Badge details\n\n```json\n" + JSON.stringify(diagnostics || { release: BUILD, status: "Not checked in this browser" }, null, 2) + "\n```\n\nBrowser / operating system:\nOrientation:\n\nPlease omit recovery backups, Wi-Fi passwords, personal photos, and raw device logs.";
  $("report").href = "https://github.com/chantastic/stopwatch/issues/new?" + new URLSearchParams({ title: "[Badge preview] ", body });
});
$("unsupported").hidden = supported;
$("save-unsupported").hidden = !supported || canSave;
controls(false);
window.addEventListener("beforeunload", event => { if (busy || factorySession) { event.preventDefault(); event.returnValue = ""; } });
