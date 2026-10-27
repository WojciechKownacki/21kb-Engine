// Browser presentation belongs to the embedding page. Requests are never
// deferred to a later gesture: callers receive the actual browser outcome.
Module['kbPresentation'] = (() => {
    const canvas = Module['canvas'];
    const modes = {
        pointerLock: {element: 'pointerLockElement', request: 'requestPointerLock', exit: 'exitPointerLock', event: 'pointerlock'},
        fullscreen: {element: 'fullscreenElement', request: 'requestFullscreen', exit: 'exitFullscreen', event: 'fullscreen'}
    };
    const pending = new Map();
    const errors = {pointerLock: '', fullscreen: ''};
    const cancelled = {pointerLock: false, fullscreen: false};
    let suspended = false;
    function active(mode) { return document[modes[mode].element] === canvas; }
    function supported(mode) {
        return typeof canvas?.[modes[mode].request] === 'function' &&
            (mode !== 'fullscreen' || document.fullscreenEnabled === true);
    }
    function state() {
        return Object.fromEntries(Object.keys(modes).map(mode => [mode, {
            supported: supported(mode), active: active(mode),
            pending: pending.has(mode), error: errors[mode]
        }]));
    }
    function changed() {
        canvas.dispatchEvent(new CustomEvent('kb-presentation-change', {detail: state()}));
    }
    function finish(mode, error = '') {
        const operation = pending.get(mode);
        if (!operation) return;
        clearTimeout(operation.timer);
        pending.delete(mode);
        errors[mode] = error;
        if (error && operation.enabled) cancelled[mode] = true;
        operation.resolve({ok: !error, error, active: active(mode)});
        changed();
    }
    function request(mode, enabled = true) {
        if (!Object.hasOwn(modes, mode)) return Promise.resolve({ok: false, error: 'invalid-mode', active: false});
        if (pending.has(mode)) return Promise.resolve({ok: false, error: 'busy', active: active(mode)});
        if (active(mode) === enabled) return Promise.resolve({ok: true, error: '', active: enabled});
        let error = '';
        if (!supported(mode)) error = 'unsupported';
        else if (enabled && (suspended || !document.hasFocus() || document.hidden)) error = 'unfocused';
        else if (enabled && navigator.userActivation && !navigator.userActivation.isActive) error = 'user-activation-required';
        if (error) {
            errors[mode] = error;
            changed();
            return Promise.resolve({ok: false, error, active: active(mode)});
        }
        errors[mode] = '';
        cancelled[mode] = false;
        return new Promise(resolve => {
            const operation = {resolve, enabled, timer: setTimeout(() => finish(mode, 'timeout'), 5000)};
            pending.set(mode, operation);
            changed();
            try {
                const result = enabled ? canvas[modes[mode].request]() : document[modes[mode].exit]();
                // Older pointer lock implementations return void and signal
                // completion only through document events.
                result?.catch(error => {
                    if (pending.get(mode) === operation) finish(mode, error?.name || 'denied');
                });
            } catch (error) {
                finish(mode, error?.name || 'denied');
            }
        });
    }
    function release() {
        suspended = true;
        for (const mode of Object.keys(modes)) {
            cancelled[mode] = true;
            finish(mode, 'cancelled');
            if (active(mode)) {
                try { document[modes[mode].exit]()?.catch(() => {}); } catch (_) {}
            }
        }
    }
    for (const [mode, configuration] of Object.entries(modes)) {
        document.addEventListener(configuration.event + 'change', () => {
            if (active(mode) && (suspended || cancelled[mode])) {
                try { document[configuration.exit]()?.catch(() => {}); } catch (_) {}
            }
            const operation = pending.get(mode);
            if (operation && active(mode) === operation.enabled) finish(mode);
            changed();
        });
        document.addEventListener(configuration.event + 'error', () => finish(mode, 'denied'));
    }
    window.addEventListener('blur', release);
    document.addEventListener('visibilitychange', () => {
        if (document.hidden) release();
        else if (document.hasFocus()) suspended = false;
    });
    window.addEventListener('focus', () => { suspended = false; });
    // Escape remains a browser escape gesture even when gameplay consumes keys.
    document.addEventListener('keydown', event => {
        if (event.code === 'Escape') {
            release();
            suspended = !document.hasFocus() || document.hidden;
        }
    }, true);
    canvas.addEventListener('webglcontextlost', release);
    canvas.addEventListener('webglcontextrestored', () => { suspended = !document.hasFocus() || document.hidden; });
    return Object.freeze({state, request});
})();
