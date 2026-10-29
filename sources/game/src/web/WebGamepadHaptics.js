(function () {
  'use strict';
  const slots = new Map();
  const focused = () => document.hasFocus() && document.visibilityState !== 'hidden';
  function pads() {
    try { return navigator.getGamepads ? navigator.getGamepads() : []; }
    catch (_) { return []; }
  }
  function actuator(pad) {
    const value = pad && pad.connected && pad.vibrationActuator;
    if (!value || typeof value.playEffect !== 'function' || typeof value.reset !== 'function') return null;
    const supported = value.effects ? Array.from(value.effects).includes('dual-rumble') : value.type === 'dual-rumble';
    return supported ? value : null;
  }
  function stop(entry) {
    ++entry.serial;
    const active = entry.active;
    entry.active = false;
    if (active) {
      try { Promise.resolve(entry.actuator.reset()).catch(() => {}); }
      catch (_) {} // A disconnected/hidden actuator can refuse reset; effects expire within 250 ms.
    }
  }
  function find(index) {
    if (!Number.isInteger(index) || index < 0 || index >= 4) return { connected: false, supported: false };
    const pad = pads()[index];
    const current = actuator(pad);
    let entry = slots.get(index);
    if (entry && (!current || entry.actuator !== current || entry.id !== pad.id)) {
      stop(entry);
      slots.delete(index);
      entry = null;
    }
    if (current && !entry) {
      entry = { actuator: current, id: pad.id, active: false, failed: false, serial: 0, next: 0, low: 0, high: 0 };
      slots.set(index, entry);
    }
    return { connected: !!(pad && pad.connected), supported: !!entry && !entry.failed && focused(), entry };
  }
  function play(entry) {
    const serial = ++entry.serial;
    entry.next = performance.now() + 150;
    const fail = () => {
      if (entry.serial === serial) { entry.failed = true; stop(entry); }
    };
    try {
      Promise.resolve(entry.actuator.playEffect('dual-rumble', {
        duration: 250, startDelay: 0, strongMagnitude: entry.low, weakMagnitude: entry.high
      })).then(result => { if (result !== 'complete' && result !== 'preempted') fail(); }, fail);
      return true; // Request accepted; asynchronous rejection is reported by the next capability query.
    } catch (_) { fail(); return false; }
  }
  function stopAll() { for (const entry of slots.values()) stop(entry); }
  Module['kbGamepadHaptics'] = {
    capability(index) { const value = find(index); return (value.connected ? 1 : 0) | (value.supported ? 2 : 0); },
    set(index, low, high) {
      if (!Number.isFinite(low) || !Number.isFinite(high)) return false;
      const value = find(index);
      if (!value.supported) return false;
      const entry = value.entry;
      low = Math.max(0, Math.min(1, low)); high = Math.max(0, Math.min(1, high));
      if (low === 0 && high === 0) { stop(entry); return true; }
      if (entry.active && entry.low === low && entry.high === high) return true;
      entry.active = true; entry.low = low; entry.high = high;
      return play(entry);
    },
    update() {
      if (!focused()) { stopAll(); return; }
      for (const index of Array.from(slots.keys())) {
        const value = find(index);
        if (value.supported && value.entry.active && performance.now() >= value.entry.next) play(value.entry);
      }
    },
    stopAll
  };
  window.addEventListener('blur', stopAll);
  window.addEventListener('pagehide', stopAll);
  document.addEventListener('visibilitychange', () => { if (!focused()) stopAll(); });
})();
