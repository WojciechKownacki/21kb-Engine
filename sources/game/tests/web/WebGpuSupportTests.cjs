const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../../src/web/WebGpuSupport.js'), 'utf8');

function host(gpu, fallback, secure = true) {
  const state = { replacements: [], cleared: 0 };
  const context = {
    WebGPU: { makePassTimestampWrites: value => value },
    Module: { kbWebGlFallback: fallback }, isSecureContext: secure,
    navigator: { gpu }, URL, console: { error() {} },
    document: { baseURI: 'https://sample.test/export/Game.html', documentElement: { dataset: {} },
      querySelector: () => null },
    location: { origin: 'https://sample.test', pathname: '/export/Game.html',
      replace: value => state.replacements.push(value) },
    setTimeout: callback => { state.timeout = callback; return 1; },
    clearTimeout: () => ++state.cleared
  };
  vm.runInNewContext(source, context);
  return { ...context, state };
}

async function main() {
  let test = host(undefined);
  test.Module.preRun.forEach(callback => callback());
  const querySet = {};
  assert.equal(test.WebGPU.makePassTimestampWrites(undefined), undefined);
  assert.deepEqual(test.WebGPU.makePassTimestampWrites({querySet,
    beginningOfPassWriteIndex: 3, endOfPassWriteIndex: 0xffffffff}),
    {querySet, beginningOfPassWriteIndex: 3});
  assert.deepEqual(test.WebGPU.makePassTimestampWrites({querySet,
    beginningOfPassWriteIndex: 0xffffffff, endOfPassWriteIndex: 0}),
    {querySet, endOfPassWriteIndex: 0});
  assert.deepEqual(test.WebGPU.makePassTimestampWrites({querySet,
    beginningOfPassWriteIndex: 2, endOfPassWriteIndex: 4}),
    {querySet, beginningOfPassWriteIndex: 2, endOfPassWriteIndex: 4});
  assert.equal(await test.Module.kbPrepareGraphics(), false);
  assert.equal(test.document.documentElement.dataset.kbError, 'webgpu-unavailable');
  assert.equal(test.state.replacements.length, 0);
  test = host(undefined);
  test.document.querySelector = selector => {
    assert.equal(selector, 'meta[name="kb-webgl-fallback"]');
    return {content: 'webgl/Game.html'};
  };
  assert.equal(await test.Module.kbPrepareGraphics(), false);
  assert.equal(test.state.replacements[0], 'https://sample.test/export/webgl/Game.html?kb-fallback=webgpu-unavailable');
  test = host(undefined, 'webgl/Game.html');
  assert.equal(await test.Module.kbPrepareGraphics(), false);
  assert.equal(test.state.replacements[0], 'https://sample.test/export/webgl/Game.html?kb-fallback=webgpu-unavailable');
  test.Module.kbGraphicsFailure('again', 'duplicate');
  assert.equal(test.state.replacements.length, 1);
  for (const fallback of ['https://elsewhere.test/Game.html', 'javascript:void(0)', 'Game.html']) {
    test = host(undefined, fallback);
    await test.Module.kbPrepareGraphics();
    assert.equal(test.state.replacements.length, 0);
  }
  test = host({ requestAdapter: async () => null });
  assert.equal(await test.Module.kbPrepareGraphics(), false);
  assert.equal(test.document.documentElement.dataset.kbError, 'webgpu-adapter');
  test = host({ requestAdapter: async () => { throw new Error('denied'); } });
  assert.equal(await test.Module.kbPrepareGraphics(), false);
  test = host({ requestAdapter: () => new Promise(() => {}) });
  const pending = test.Module.kbPrepareGraphics();
  test.state.timeout();
  assert.equal(await pending, false);
  assert.equal(test.state.cleared, 1);
  let options;
  test = host({ requestAdapter: async value => { options = value; return {}; } });
  assert.equal(await test.Module.kbPrepareGraphics(), true);
  assert.equal(options.powerPreference, 'high-performance');
  assert.equal(test.state.cleared, 1);
  assert.equal(test.document.documentElement.dataset.kbError, undefined);
  test.Module.kbGraphicsFailure('graphics-device-lost', 'lost');
  assert.equal(test.document.documentElement.dataset.kbError, 'graphics-device-lost');
  test = host({ requestAdapter: () => { throw new Error('must not probe'); } }, '', false);
  assert.equal(await test.Module.kbPrepareGraphics(), false);
  console.log('WebGPU feature detection and failure routing passed');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
