// ─── SignalRGB Bridge: Scan-Phase Environment Stubs ─────────────
// Scanner contexts load this file before the canonical runtime
// `device.js`, so scan-time metadata extraction and runtime share the
// same device model instead of maintaining two divergent copies.
// ─────────────────────────────────────────────────────────────────

var console = {
    log: function() {},
    info: function() {},
    warn: function() {},
    error: function() {},
};

function hexToRgb(hex) {
    if (typeof hex !== "string") return [0, 0, 0];
    var match = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
    return match
        ? [parseInt(match[1], 16), parseInt(match[2], 16), parseInt(match[3], 16)]
        : [0, 0, 0];
}

function ContextError(message) {
    this.message = message || "";
    this.name = "ContextError";
}
ContextError.prototype = Object.create(Error.prototype);

var Assert = {
    isOk: function(value, message) {
        if (!value) throw new ContextError(message || "Assertion failed");
    },
    fail: function(message) {
        throw new ContextError(message || "Assertion failed");
    },
    unreachable: function(message) {
        throw new ContextError(message || "Unreachable");
    },
    isEqual: function() {},
    softIsDefined: function(value) {
        return value !== undefined && value !== null;
    },
};

var globalContext = { set: function() {} };

var DeviceDiscovery = {
    foundVirtualDevice: function() {},
};

var permissions = {
    permissions: function() { return []; },
    setCallback: function() {},
};

var battery = {
    setBatteryLevel: function() {},
    setBatteryState: function() {},
};

var LCD = {
    initialize: function() {},
    getFrame: function() { return []; },
};

var systeminfo = {
    GetMotherboardInfo: function() {
        return { model: "", manufacturer: "", product: "", vendor: "" };
    },
    GetBiosInfo: function() {
        return { vendor: "", version: "", date: "" };
    },
    GetRamInfo: function() {
        return { totalMemory: 0, modules: [] };
    },
};

var keyboard = { sendEvent: function() {}, sendHid: function() {} };
var mouse = { sendEvent: function() {} };
var LightingMode = "Canvas";
var forcedColor = "#000000";

function _hid_write() { return 0; }
function _hid_read() { return []; }
function _hid_send_report() { return 0; }
function _hid_get_report() { return []; }
function _hid_set_endpoint() { return false; }
function _hid_get_endpoints() { return []; }
function _hid_flush() { return 0; }
function _hid_get_last_read_size() { return 0; }
function _log() {}
function _pause() {}
