const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

async function main() {
  let clock = 0, focus = true;
  const pads = [], calls = [], events = {};
  const document = { hasFocus: () => focus, visibilityState: 'visible',
    addEventListener: (name, fn) => { events[name] = fn; } };
  const context = { Module: {}, document, navigator: { getGamepads: () => pads },
    performance: { now: () => clock }, Promise,
    window: { addEventListener: (name, fn) => { events[name] = fn; } } };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../../src/web/WebGamepadHaptics.js'), 'utf8'), context);
  const backend = context.Module.kbGamepadHaptics;
  function pad(index, options = {}) {
    return { id: 'Controller ' + index, connected: true, vibrationActuator: {
      effects: ['dual-rumble'], playEffect: (type, params) => {
        calls.push({ index, type, ...params }); return Promise.resolve('complete');
      }, reset: () => { calls.push({ index, reset: true }); return Promise.resolve('complete'); }, ...options
    } };
  }
  assert.equal(backend.capability(0), 0);
  assert.equal(backend.set(0, 1, 1), false);
  pads[0] = pad(0); pads[3] = pad(3);
  assert.equal(backend.capability(0), 3);
  assert.equal(backend.set(0, .75, .25), true);
  assert.equal(backend.set(0, .75, .25), true);
  assert.equal(calls.length, 1, 'identical commands must not flood the actuator');
  assert.deepEqual(calls[0], { index: 0, type: 'dual-rumble', duration: 250, startDelay: 0, strongMagnitude: .75, weakMagnitude: .25 });
  clock = 160; backend.update(); assert.equal(calls.length, 2, 'continuous requests must refresh bounded effects');
  assert.equal(backend.set(3, -1, 2), true);
  assert.equal(calls.at(-1).strongMagnitude, 0); assert.equal(calls.at(-1).weakMagnitude, 1);
  for (const [slot, low, high] of [[4, 1, 1], [-1, 1, 1], [0, NaN, 0], [0, 0, Infinity]])
    assert.equal(backend.set(slot, low, high), false);
  assert.equal(backend.set(0, 0, 0), true); assert.equal(calls.at(-1).reset, true);
  const before = calls.length; clock = 400; backend.update();
  assert.equal(calls.slice(before).filter(x => x.index === 0).length, 0, 'stopped effects must not refresh');
  pads[3] = null; backend.update(); assert.deepEqual(calls.at(-1), { index: 3, reset: true });
  pads[0].vibrationActuator.effects = ['trigger-rumble'];
  assert.equal(backend.capability(0), 1); assert.equal(backend.set(0, 1, 1), false);
  pads[0] = pad(0); backend.set(0, 1, 1);
  focus = false; events.blur(); assert.equal(calls.at(-1).reset, true);
  assert.equal(backend.set(0, 1, 1), false);
  focus = true; const stopped = calls.length; clock += 1000; backend.update();
  assert.equal(calls.length, stopped, 'focus recovery must not revive old effects');
  let rejectOld;
  pads[0] = pad(0, { playEffect: () => new Promise((resolve, reject) => { rejectOld = reject; }) });
  backend.set(0, .1, .2); const old = rejectOld;
  backend.set(0, .3, .4); old(new Error('old request'));
  await Promise.resolve(); assert.equal(backend.capability(0), 3, 'stale rejection must not disable a newer request');
  rejectOld(new Error('current request'));
  await Promise.resolve(); assert.equal(backend.capability(0), 1);
  pads[0] = pad(0); assert.equal(backend.capability(0), 3);
  backend.set(0, 1, 1); document.visibilityState = 'hidden'; events.visibilitychange();
  assert.equal(calls.at(-1).reset, true);
  document.visibilityState = 'visible'; backend.set(0, 1, 1); events.pagehide();
  assert.equal(calls.at(-1).reset, true);
  pads[0] = pad(0, { playEffect: () => { throw new Error('denied'); }, reset: () => Promise.reject(new Error('gone')) });
  assert.equal(backend.set(0, 1, 1), false);
  await Promise.resolve();
  console.log('Browser haptics state and failure tests passed');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
