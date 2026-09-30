const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const source = fs.readFileSync(process.argv[2], 'utf8');
const elements = new Map();
function element(id) {
  if (!elements.has(id)) elements.set(id, {
    value: '', disabled: false, textContent: '', listeners: {},
    addEventListener(name, listener) { this.listeners[name] = listener; },
  });
  return elements.get(id);
}
const calls = [];
const context = vm.createContext({ Date, Math, Error, Intl, setTimeout, clearTimeout, AbortController,
  document: { getElementById: element },
  fetch: async (path, options) => {
    calls.push({ path, headers: options.headers, body: JSON.parse(options.body) });
    return { ok: true, json: async () => ({ ok: true, valid: true,
      message: path === '/wifi/test' ? 'Connected to the event Wi-Fi and received an address. Disconnected again.' : 'Saved.' }) };
  },
});
const settle = () => new Promise(resolve => setImmediate(resolve));
(async () => {
  vm.runInContext(source, context); await settle();
  assert.deepEqual(calls.map(c => c.path), ['/clock']);
  element('wifiSsid').value = 'init() attendee';
  element('wifiPassword').value = '';
  await element('wifiForm').listeners.submit({ preventDefault() {} }); await settle();
  assert.equal(calls.at(-1).path, '/wifi/save');
  assert.deepEqual(calls.at(-1).body, { ssid: 'init() attendee', password: '' });
  assert.equal(element('wifiPassword').value, '');
  await element('wifiTest').listeners.click(); await settle();
  assert.equal(calls.at(-1).path, '/wifi/test');
  assert.match(element('wifiStatus').textContent, /Disconnected again/);
  element('wifiSsid').value = 'Other network';
  await element('wifiForget').listeners.click(); await settle();
  assert.equal(calls.at(-1).path, '/wifi/forget');
  assert.equal(element('wifiSsid').value, 'init() attendee', 'Forgetting restores the built-in event network');
  assert.equal(calls.filter(c => c.path === '/save').length, 0, 'Wi-Fi edits cannot save the profile');
  assert(calls.every(c => c.headers['X-Conference-Nonce'] === '0123456789abcdef0123456789abcdef'));
  console.log('Portal Wi-Fi form: explicit save, test, and forget with nonce; no automatic connection passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
