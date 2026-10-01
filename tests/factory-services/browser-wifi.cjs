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
  assert.equal(element('wifiFields').hidden, true, 'Event Wi-Fi hides the custom fields');
  element('wifiChoice').value = 'other'; element('wifiChoice').listeners.change();
  assert.equal(element('wifiFields').hidden, false);
  assert.deepEqual(calls.map(c => c.path), ['/clock'], 'Choosing a network never contacts the badge');
  element('wifiSsid').value = 'Home network'; element('wifiPassword').value = 'temporary-pass';
  element('name').value = 'Synthetic Attendee'; element('photoSource').value = 'keep';
  await element('form').listeners.submit({ preventDefault() {} }); await settle();
  let save = calls.at(-1);
  assert.equal(save.path, '/save');
  assert.equal(save.body.ssid, 'Home network'); assert.equal(save.body.password, 'temporary-pass');
  element('form').hidden = false;
  vm.runInContext("lock(false)", context);
  element('wifiChoice').value = 'event'; element('wifiChoice').listeners.change();
  await element('form').listeners.submit({ preventDefault() {} }); await settle();
  save = calls.at(-1);
  assert.equal(save.body.ssid, 'init() attendee', 'Event Wi-Fi ignores the hidden custom fields');
  assert.equal(save.body.password, '');
  assert(!calls.some(c => c.path.startsWith('/wifi/')), 'No separate Wi-Fi endpoints remain');
  assert(calls.every(c => c.headers['X-Conference-Nonce'] === '0123456789abcdef0123456789abcdef'));
  console.log('Portal Wi-Fi: event/other choice saved with Save badge, no separate requests or automatic connection passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
