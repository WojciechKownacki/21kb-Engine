// Use the public instantiation hook so the network deadline starts before WASM runs.
const kbWasmScriptDirectory = new URL('.', document.currentScript?.src || document.baseURI).href;
Module['instantiateWasm'] ??= function(imports, receiveInstance) {
    const idleMilliseconds = 30000;
    const controller = new AbortController();
    let stopped = false;
    let timer;
    let lastProgress = performance.now();

    function cancelTimer() {
        if (timer !== undefined) clearTimeout(timer);
        timer = undefined;
    }
    function fail(code, message) {
        if (stopped) return;
        stopped = true;
        cancelTimer();
        controller.abort();
        document.documentElement.dataset.kbError = code;
        location.hash = 'kb-error-' + code;
        console.error('kb_game_web: ' + message);
    }
    function checkProgress() {
        const remaining = idleMilliseconds - (performance.now() - lastProgress);
        if (remaining <= 0) {
            fail('wasm-timeout', 'WASM download made no progress for 30 seconds; reload to retry');
        } else {
            timer = setTimeout(checkProgress, Math.ceil(remaining));
        }
    }

    (async () => {
        if (Module['kbPrepareGraphics'] && !await Module['kbPrepareGraphics']()) return;
        let result;
        try {
            if (Module['wasmBinary']) {
                result = await WebAssembly.instantiate(Module['wasmBinary'], imports);
            } else {
                timer = setTimeout(checkProgress, idleMilliseconds);
                const prefix = kbWasmScriptDirectory;
                const url = Module['locateFile'] ? Module['locateFile']('Game.wasm', prefix) : prefix + 'Game.wasm';
                const response = await fetch(url, { signal:controller.signal });
                if (stopped) return;
                if (!response.ok) {
                    fail('wasm-http', 'WASM request failed: HTTP ' + response.status);
                    return;
                }
                if (!response.body) {
                    fail('wasm-fetch', 'WASM response has no readable body');
                    return;
                }
                const stream = response.body.pipeThrough(new TransformStream({
                    transform(chunk, output) {
                        if (chunk.byteLength) lastProgress = performance.now();
                        output.enqueue(chunk);
                    },
                    flush() {
                        // Compilation and runtime initialization are not network idle time.
                        cancelTimer();
                    }
                }));
                const observed = new Response(stream, { headers:response.headers });
                const mime = (response.headers.get('Content-Type') || '').split(';')[0].trim().toLowerCase();
                result = mime === 'application/wasm'
                    ? await WebAssembly.instantiateStreaming(observed, imports)
                    : await WebAssembly.instantiate(await observed.arrayBuffer(), imports);
            }
        } catch (error) {
            fail(error instanceof WebAssembly.CompileError || error instanceof WebAssembly.LinkError
                ? 'wasm-init' : 'wasm-fetch', 'WASM startup failed: ' + String(error));
            return;
        }
        if (stopped) return;
        cancelTimer();
        stopped = true;
        // Native startup exceptions retain their own runtime diagnostics.
        receiveInstance(result.instance, result.module);
    })();
    return {};
};
