// Wi-Fi is saved with Save badge: no separate Wi-Fi requests, a client-side
// reset to the event network, and no automatic connection.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const source = fs.readFileSync(process.argv[2], 'utf8');
const elements = new Map();
function element(id) {
  if (!elements.has(id)) elements.set(id, {
    value: '', disabled: false, hidden: false, checked: false, files: [], textContent: '', listeners: {},
    addEventListener(name, listener) { this.listeners[name] = listener; },
  });
  return elements.get(id);
}
const calls = [];
const context = vm.createContext({ Date, Math, Error, Intl, setTimeout, clearTimeout, AbortController,
  document: { getElementById: element },
  fetch: async (path, options) => {
    calls.push({ path, headers: options.headers, body: JSON.parse(options.body) });
    return { ok: true, json: async () => ({ ok: true, valid: true, message: 'Saved.' }) };
  },
});
const settle = () => new Promise(resolve => setImmediate(resolve));
(async () => {
  vm.runInContext(source, context); await settle();
  assert.deepEqual(calls.map(c => c.path), ['/clock']);
  element('wifiSsid').value = 'Other network'; element('wifiPassword').value = 'temporary-pass';
  element('wifiForget').listeners.click();
  assert.equal(element('wifiSsid').value, 'init() attendee', 'Use event Wi-Fi restores the built-in network');
  assert.equal(element('wifiPassword').value, '');
  assert.deepEqual(calls.map(c => c.path), ['/clock'], 'Choosing a network never contacts the badge');
  element('wifiSsid').value = 'Home network'; element('wifiPassword').value = 'temporary-pass';
  element('name').value = 'Synthetic Attendee';
  await element('form').listeners.submit({ preventDefault() {} }); await settle();
  const save = calls.at(-1);
  assert.equal(save.path, '/save');
  assert.equal(save.body.ssid, 'Home network'); assert.equal(save.body.password, 'temporary-pass');
  assert(!calls.some(c => c.path.startsWith('/wifi/')), 'No separate Wi-Fi endpoints remain');
  assert(calls.every(c => c.headers['X-Conference-Nonce'] === '0123456789abcdef0123456789abcdef'));
  console.log('Portal Wi-Fi: saved with Save badge, client-side event reset, no separate requests or automatic connection passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
