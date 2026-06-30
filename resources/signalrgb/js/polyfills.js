// ─── SignalRGB Bridge: Runtime Polyfills ────────────────────────
// Injected into every QuickJS device context before the device
// script is evaluated. Provides global objects/functions that
// SignalRGB scripts expect in their runtime environment.
//
// NOTE: `_log` and `_pause` are Lua callbacks bound before this
//       file is evaluated.
// ────────────────────────────────────────────────────────────────

// ─── Console ───────────────────────────────────────────────────
var console = (function() {
    function fmt() {
        var a = [];
        for (var i = 0; i < arguments.length; i++) {
            a.push(typeof arguments[i] === 'string'
                ? arguments[i]
                : JSON.stringify(arguments[i]));
        }
        return a.join(' ');
    }
    return {
        log:   function() { _log(fmt.apply(null, arguments)); },
        info:  function() { _log(fmt.apply(null, arguments)); },
        warn:  function() { _log('[WARN] '  + fmt.apply(null, arguments)); },
        error: function() { _log('[ERROR] ' + fmt.apply(null, arguments)); },
    };
})();

// ─── Utility Functions ─────────────────────────────────────────

function hexToRgb(hex) {
    if (typeof hex !== 'string') return [0, 0, 0];
    var m = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
    return m
        ? [parseInt(m[1], 16), parseInt(m[2], 16), parseInt(m[3], 16)]
        : [0, 0, 0];
}

// ─── @SignalRGB/Errors ─────────────────────────────────────────

function ContextError(message) {
    this.message = message || '';
    this.name = 'ContextError';
}
ContextError.prototype = Object.create(Error.prototype);
ContextError.prototype.constructor = ContextError;

var Assert = {
    isOk:          function(v, m) { if (!v) throw new ContextError(m || "Assertion failed"); },
    fail:          function(m)    { throw new ContextError(m || "Assertion failed"); },
    unreachable:   function(m)    { throw new ContextError(m || "Unreachable"); },
    isEqual:       function(a,b,m){ if (a !== b) throw new ContextError(m || a+" !== "+b); },
    softIsDefined: function(v)    { return v !== undefined && v !== null; },
};

var globalContext = {
    set: function() {},
};

// ─── @SignalRGB/DeviceDiscovery ────────────────────────────────

var DeviceDiscovery = {
    foundVirtualDevice: function() {},
};

// ─── @SignalRGB/permissions ────────────────────────────────────

var permissions = {
    permissions: function() { return []; },
    setCallback: function() {},
};

// ─── Battery ───────────────────────────────────────────────────

var battery = {
    setBatteryLevel: function() {},
    setBatteryState: function() {},
};

// ─── LCD ───────────────────────────────────────────────────────

var LCD = {
    initialize: function() {},
    getFrame:   function() { return []; },
};

// ─── systeminfo ────────────────────────────────────────────────

var systeminfo = {
    GetMotherboardInfo: function() { return { model: "", manufacturer: "", product: "", vendor: "" }; },
    GetBiosInfo:        function() { return { vendor: "", version: "", date: "" }; },
    GetRamInfo:         function() { return { totalMemory: 0, modules: [] }; },
};

// ─── Input device stubs ────────────────────────────────────────

var keyboard = {
    sendEvent: function() {},
    sendHid:   function() {},
};

var mouse = {
    sendEvent: function() {},
};

// ─── Global variables (expected by many scripts) ──────────────

var LightingMode = "Canvas";
var forcedColor  = "#000000";

// ─── Frame buffer globals (set from Lua before each Render) ───

var __frame_buf = null;
var __frame_w   = 1;
