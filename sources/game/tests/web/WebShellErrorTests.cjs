const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const html = fs.readFileSync(path.join(__dirname, '../../src/web/WebGameShell.html'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
for (const fallback of [false, true]) {
  const dataset = { kbError: 'graphics-device-lost', ...(fallback ? { kbFallback: 'webgl' } : {}) };
  const elements = { '#status': {}, '#output': { value: '' }, '#canvas': { setAttribute() {} } };
  const writes = [], reported = [];
  const location = {};
  Object.defineProperty(location, 'hash', { set(value) { writes.push(value); } });
  const context = { document: { documentElement: { dataset }, querySelector: name => elements[name] },
    MutationObserver: class { observe() {} }, location, console,
    kbReportRuntimeError: (...args) => reported.push(args) };
  vm.runInNewContext(script, context);
  context.Module.onAbort('device unavailable');
  assert.equal(dataset.kbError, 'graphics-device-lost', 'abort must retain the original graphics diagnostic');
  assert.deepEqual(writes, fallback ? [] : ['kb-error-graphics-device-lost'],
    'abort must not change the URL after fallback navigation has started');
  assert.equal(elements['#status'].hidden, false);
  assert.deepEqual(reported, [['abort', 'device unavailable']]);
}
console.log('Browser shell preserves graphics failure and fallback navigation');
