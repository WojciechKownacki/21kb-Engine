(function () {
  'use strict';
  Module['preRun'] = Module['preRun'] || [];
  Module['preRun'].push(function () {
    // Some emdawnwebgpu versions forward the C API's undefined query index
    // as a numeric JavaScript index instead of omitting the optional field.
    if (typeof WebGPU === 'undefined' || !WebGPU.makePassTimestampWrites) return;
    const convert = WebGPU.makePassTimestampWrites;
    WebGPU.makePassTimestampWrites = function (ptr) {
      const writes = convert.call(this, ptr);
      if (writes) {
        for (const field of ['beginningOfPassWriteIndex', 'endOfPassWriteIndex']) {
          if (writes[field] === 0xffffffff) delete writes[field];
        }
      }
      return writes;
    };
  });
  let failed = false;
  Module['kbGraphicsFailure'] = function (code, message) {
    if (failed) return;
    failed = true;
    document.documentElement.dataset.kbError = code;
    location.hash = 'kb-error-' + code;
    console.error('kb_game_web: ' + message);
    const fallback = Module['kbWebGlFallback'] ||
      document.querySelector('meta[name="kb-webgl-fallback"]')?.content;
    if (typeof fallback !== 'string' || !fallback) return;
    try {
      const url = new URL(fallback, document.baseURI);
      if (url.origin !== location.origin || !/^https?:$/.test(url.protocol) ||
          url.pathname === location.pathname) return;
      url.searchParams.set('kb-fallback', code);
      document.documentElement.dataset.kbFallback = 'webgl';
      location.replace(url.href);
    } catch (_) { /* Keep the visible failure if the configured fallback is invalid. */ }
  };
  Module['kbPrepareGraphics'] = async function () {
    if (!globalThis.isSecureContext || !navigator.gpu || typeof navigator.gpu.requestAdapter !== 'function') {
      Module['kbGraphicsFailure']('webgpu-unavailable', 'WebGPU is unavailable in this browser context');
      return false;
    }
    let timer;
    try {
      const adapter = await Promise.race([
        navigator.gpu.requestAdapter({powerPreference: 'high-performance'}),
        new Promise((_, reject) => {
          timer = setTimeout(() => reject(new Error('WebGPU adapter request timed out')), 15000);
        })
      ]);
      if (!adapter) {
        Module['kbGraphicsFailure']('webgpu-adapter', 'No compatible WebGPU adapter is available');
        return false;
      }
      return true;
    } catch (error) {
      Module['kbGraphicsFailure']('webgpu-adapter', String(error));
      return false;
    } finally {
      if (timer !== undefined) clearTimeout(timer);
    }
  };
})();
