export function Name() { return "Keychron K5 Max ANSI QMK Keyboard (Keychron RGB)"; }
export function Version() { return "1.0.0"; }
export function VendorId() { return 0x3434; }
export function ProductId() { return 0x0A50; }
export function Publisher() { return "SignalBridge"; }
export function Documentation() { return "OpenRGB/Controllers/QMKController/QMKKeychronController"; }
export function DeviceType() { return "keyboard"; }
export function Size() { return [21, 6]; }
export function DefaultPosition() { return [10, 100]; }
export function DefaultScale() { return 8.0; }

/* global
shutdownMode:readonly
shutdownColor:readonly
LightingMode:readonly
forcedColor:readonly
rgbOrder:readonly
*/
export function ControllableParameters() {
    return [
        { "property": "shutdownMode", "group": "lighting", "label": "Shutdown Mode", "type": "combobox", "values": ["SignalRGB", "Hardware"], "default": "SignalRGB" },
        { "property": "shutdownColor", "group": "lighting", "label": "Shutdown Color", "min": "0", "max": "360", "type": "color", "default": "#000000" },
        { "property": "LightingMode", "group": "lighting", "label": "Lighting Mode", "type": "combobox", "values": ["Canvas", "Forced"], "default": "Canvas" },
        { "property": "forcedColor", "group": "lighting", "label": "Forced Color", "min": "0", "max": "360", "type": "color", "default": "#009bde" },
        { "property": "rgbOrder", "group": "lighting", "label": "RGB Order", "type": "combobox", "values": ["RGB", "RBG", "GRB", "GBR", "BRG", "BGR"], "default": "RGB" },
    ];
}

const vKeys = [
      0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,       13,  14,  15,  16,  17,  18,  19,
     20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,
     41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,
     62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,       74,                 75,  76,  77,
     78,       79,  80,  81,  82,  83,  84,  85,  86,  87,  88,       89,       90,       91,  92,  93,  94,
     95,  96,  97,                 98,                 99, 100, 101, 102, 103, 104, 105, 106,      107
];

const vKeyNames = [
    "Esc", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "Task", "Mic", "Light", "Circle", "Triangle", "Square", "X",
    "`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "+", "Backspace", "Insert", "Home", "Page Up", "NumLock", "Num /", "Num *", "Num -",
    "Tab", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "\\", "Del", "End", "Page Down", "Num 7", "Num 8", "Num 9", "Num +",
    "CapsLock", "A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'", "Enter", "Num 4", "Num 5", "Num 6",
    "Left Shift", "Z", "X", "C", "V", "B", "N", "M", ",", ".", "/", "Right Shift", "Up Arrow", "Num 1", "Num 2", "Num 3", "Num Enter",
    "Left Ctrl", "Left Win", "Left Alt", "Space", "Right Alt", "Fn", "Menu", "Right Ctrl", "Left Arrow", "Down Arrow", "Right Arrow", "Num 0", "Num .",
];

const vKeyPositions = [
    [0, 0], [1, 0], [2, 0], [3, 0], [4, 0], [5, 0], [6, 0], [7, 0], [8, 0], [9, 0], [10, 0], [11, 0], [12, 0],          [14, 0], [15, 0], [16, 0], [17, 0], [18, 0], [19, 0], [20, 0],
    [0, 1], [1, 1], [2, 1], [3, 1], [4, 1], [5, 1], [6, 1], [7, 1], [8, 1], [9, 1], [10, 1], [11, 1], [12, 1], [13, 1], [14, 1], [15, 1], [16, 1], [17, 1], [18, 1], [19, 1], [20, 1],
    [0, 2], [1, 2], [2, 2], [3, 2], [4, 2], [5, 2], [6, 2], [7, 2], [8, 2], [9, 2], [10, 2], [11, 2], [12, 2], [13, 2], [14, 2], [15, 2], [16, 2], [17, 2], [18, 2], [19, 2], [20, 2],
    [0, 3], [1, 3], [2, 3], [3, 3], [4, 3], [5, 3], [6, 3], [7, 3], [8, 3], [9, 3], [10, 3], [11, 3],          [13, 3],                              [17, 3], [18, 3], [19, 3],
    [0, 4],         [2, 4], [3, 4], [4, 4], [5, 4], [6, 4], [7, 4], [8, 4], [9, 4], [10, 4], [11, 4],          [13, 4],             [15, 4],          [17, 4], [18, 4], [19, 4], [20, 4],
    [0, 5], [1, 5], [2, 5],                         [6, 5],                         [10, 5], [11, 5], [12, 5], [13, 5], [14, 5], [15, 5], [16, 5], [17, 5], [18, 5],
];

const KEYCHRON_QMK_USAGE_PAGE = 0xFF60;
const KEYCHRON_QMK_USAGE = 0x61;
const KEYCHRON_HID_PACKET_SIZE = 33;
const KEYCHRON_RAW_READ_SIZE = 32;

const QMK_VIA_CMD_GET_PROTOCOL_VERSION = 0x01;
const QMK_VIA_CMD_CUSTOM_SET_VALUE = 0x07;
const QMK_VIA_RGB_MATRIX_CHANNEL = 0x03;
const QMK_VIA_RGB_MATRIX_EFFECT = 0x02;

const KC_GET_PROTOCOL_VERSION = 0xA0;
const KC_GET_FIRMWARE_VERSION = 0xA1;
const KC_GET_SUPPORT_FEATURE = 0xA2;
const KC_KEYCHRON_RGB = 0xA8;
const KC_FEATURE_KEYCHRON_RGB = 1 << 7;

const KEYCHRON_RGB_PROTOCOL_VER = 0x01;
const KEYCHRON_RGB_SAVE = 0x02;
const KEYCHRON_RGB_LED_COUNT = 0x05;
const KEYCHRON_RGB_PER_KEY_SET_TYPE = 0x08;
const KEYCHRON_RGB_PER_KEY_SET_COLOR = 0x0A;

const KEYCHRON_PER_KEY_RGB_SOLID = 0;
const KEYCHRON_QHE_PER_KEY_RGB_EFFECT = 23;

let LEDCount = vKeys.length;
let IsKeychronRgbSupported = true;

export function LedNames() {
    return vKeyNames;
}

export function LedPositions() {
    return vKeyPositions;
}

export function Initialize() {
    device.flush();

    const viaProtocol = readU16BE(viaSendCommand(QMK_VIA_CMD_GET_PROTOCOL_VERSION, [], 2, 250));
    if (viaProtocol >= 0) {
        device.log("VIA Protocol Version: " + viaProtocol);
    }

    const keychronProtocol = readByte(viaSendCommand(KC_GET_PROTOCOL_VERSION, [], 1, 250));
    if (keychronProtocol >= 0) {
        device.log("Keychron Protocol Version: " + keychronProtocol);
    }

    const firmwareVersion = readString(viaSendCommand(KC_GET_FIRMWARE_VERSION, [], 30, 250));
    if (firmwareVersion) {
        device.log("Keychron Firmware Version: " + firmwareVersion);
    }

    const supportedFeatures = readU16BE(viaSendCommand(KC_GET_SUPPORT_FEATURE, [], 2, 250));
    if (supportedFeatures >= 0) {
        IsKeychronRgbSupported = (supportedFeatures & KC_FEATURE_KEYCHRON_RGB) !== 0;
        device.log("Keychron Supported Features: 0x" + supportedFeatures.toString(16));
    }

    if (!IsKeychronRgbSupported) {
        device.notify("Unsupported Firmware", "This Keychron firmware did not report Keychron RGB support.", 3, "Documentation");
        return;
    }

    const rgbProtocol = readU16BE(viaSendCommandSub(KC_KEYCHRON_RGB, KEYCHRON_RGB_PROTOCOL_VER, [], 2, 250));
    if (rgbProtocol >= 0) {
        device.log("Keychron RGB Protocol Version: " + rgbProtocol);
    }

    const reportedLedCount = readU16BE(viaSendCommandSub(KC_KEYCHRON_RGB, KEYCHRON_RGB_LED_COUNT, [], 2, 250));
    if (reportedLedCount > 0 && reportedLedCount <= 255) {
        LEDCount = reportedLedCount;
    }
    device.log("Device Total LED Count: " + LEDCount);

    enterDirectMode();
}

export function Render() {
    if (IsKeychronRgbSupported) {
        sendColors();
    }
}

export function Shutdown(SystemSuspending) {
    if (SystemSuspending) {
        sendColors("#000000");
    } else if (shutdownMode === "SignalRGB") {
        sendColors(shutdownColor);
    } else {
        saveMode();
    }
}

function enterDirectMode() {
    // Matches OpenRGB QMKKeychronController::SetMode(0xFFFF).
    viaSendCommand(QMK_VIA_CMD_CUSTOM_SET_VALUE, [
        QMK_VIA_RGB_MATRIX_CHANNEL,
        QMK_VIA_RGB_MATRIX_EFFECT,
        KEYCHRON_QHE_PER_KEY_RGB_EFFECT,
    ], 0, 250);

    viaSendCommandSub(KC_KEYCHRON_RGB, KEYCHRON_RGB_PER_KEY_SET_TYPE, [KEYCHRON_PER_KEY_RGB_SOLID], 0, 250);
    device.flush();
}

function saveMode() {
    viaSendCommandSub(KC_KEYCHRON_RGB, KEYCHRON_RGB_SAVE, [], 0, 250);
}

function createSolidColorArray(color) {
    const rgbdata = new Array(LEDCount * 3).fill(0);
    const count = Math.min(vKeys.length, LEDCount);

    for (let iIdx = 0; iIdx < count; iIdx++) {
        const ledIdx = vKeys[iIdx];
        if (ledIdx >= LEDCount) {
            continue;
        }

        const offset = ledIdx * 3;
        writeOrderedColor(rgbdata, offset, color);
    }

    return rgbdata;
}

function grabColors(overrideColor) {
    if (overrideColor) {
        return createSolidColorArray(hexToRgbLocal(overrideColor));
    }
    if (LightingMode === "Forced") {
        return createSolidColorArray(hexToRgbLocal(forcedColor));
    }

    const rgbdata = new Array(LEDCount * 3).fill(0);
    const count = Math.min(vKeys.length, vKeyPositions.length, LEDCount);

    for (let iIdx = 0; iIdx < count; iIdx++) {
        const ledIdx = vKeys[iIdx];
        if (ledIdx >= LEDCount) {
            continue;
        }

        const x = vKeyPositions[iIdx][0];
        const y = vKeyPositions[iIdx][1];
        const color = device.color(x, y);
        const offset = ledIdx * 3;
        writeOrderedColor(rgbdata, offset, color);
    }

    return rgbdata;
}

function sendColors(overrideColor) {
    const rgbdata = grabColors(overrideColor);
    const ledsPerPacket = 9;

    for (let startLed = 0; startLed < LEDCount; startLed += ledsPerPacket) {
        const packetLedCount = Math.min(ledsPerPacket, LEDCount - startLed);
        const startByte = startLed * 3;
        const endByte = startByte + (packetLedCount * 3);
        streamLightingData(startLed, rgbdata.slice(startByte, endByte));
    }
}

function streamLightingData(startLedIdx, rgbData) {
    const ledCount = Math.floor(rgbData.length / 3);
    const payload = [startLedIdx & 0xFF, ledCount & 0xFF];

    for (let idx = 0; idx < ledCount; idx++) {
        const offset = idx * 3;
        const hsv = rgbToKeychronHsv(rgbData[offset], rgbData[offset + 1], rgbData[offset + 2]);
        payload.push(hsv[0], hsv[1], hsv[2]);
    }

    writeSubCommand(KC_KEYCHRON_RGB, KEYCHRON_RGB_PER_KEY_SET_COLOR, payload);
    readResponse(KC_KEYCHRON_RGB, KEYCHRON_RGB_PER_KEY_SET_COLOR, 1);
}

function viaSendCommand(command, dataIn, dataOutSize, timeoutMs) {
    const packet = [0x00, command].concat(dataIn || []);
    if (device.write(packet, KEYCHRON_HID_PACKET_SIZE) <= 0) {
        return null;
    }

    if (!dataOutSize) {
        readResponse(command, null, timeoutMs);
        return [];
    }

    const response = readResponse(command, null, timeoutMs);
    return response ? response.slice(1, 1 + dataOutSize) : null;
}

function viaSendCommandSub(command, subCommand, dataIn, dataOutSize, timeoutMs) {
    writeSubCommand(command, subCommand, dataIn || []);

    if (!dataOutSize) {
        readResponse(command, subCommand, timeoutMs);
        return [];
    }

    const response = readResponse(command, subCommand, timeoutMs);
    return response ? response.slice(2, 2 + dataOutSize) : null;
}

function writeSubCommand(command, subCommand, dataIn) {
    const packet = [0x00, command, subCommand].concat(dataIn || []);
    return device.write(packet, KEYCHRON_HID_PACKET_SIZE);
}

function readResponse(command, subCommand, timeoutMs) {
    const response = device.read([command, subCommand == null ? 0 : subCommand], KEYCHRON_RAW_READ_SIZE, timeoutMs == null ? 100 : timeoutMs);
    if (device.getLastReadSize() <= 0) {
        return null;
    }
    if (response[0] !== command) {
        return null;
    }
    if (subCommand != null && response[1] !== subCommand) {
        return null;
    }
    return response;
}

function readByte(bytes) {
    return Array.isArray(bytes) && bytes.length >= 1 ? bytes[0] : -1;
}

function readU16BE(bytes) {
    return Array.isArray(bytes) && bytes.length >= 2 ? ((bytes[0] << 8) | bytes[1]) : -1;
}

function readString(bytes) {
    if (!Array.isArray(bytes)) {
        return "";
    }

    let text = "";
    for (let idx = 0; idx < bytes.length; idx++) {
        if (bytes[idx] === 0) {
            break;
        }
        text += String.fromCharCode(bytes[idx]);
    }
    return text;
}

function rgbToKeychronHsv(r, g, b) {
    r = clampByte(r) / 255;
    g = clampByte(g) / 255;
    b = clampByte(b) / 255;

    const max = Math.max(r, g, b);
    const min = Math.min(r, g, b);
    const delta = max - min;

    let hue = 0;
    if (delta !== 0) {
        if (max === r) {
            hue = 60 * (((g - b) / delta) % 6);
        } else if (max === g) {
            hue = 60 * (((b - r) / delta) + 2);
        } else {
            hue = 60 * (((r - g) / delta) + 4);
        }
    }
    if (hue < 0) {
        hue += 360;
    }

    const saturation = max === 0 ? 0 : delta / max;
    return [
        Math.floor((hue * 256) / 360) & 0xFF,
        clampByte(Math.round(saturation * 255)),
        clampByte(Math.round(max * 255)),
    ];
}

function clampByte(value) {
    const number = Number(value) || 0;
    if (number < 0) {
        return 0;
    }
    if (number > 255) {
        return 255;
    }
    return number & 0xFF;
}

function writeOrderedColor(rgbdata, offset, color) {
    const ordered = applyRgbOrder(color);
    rgbdata[offset] = ordered[0];
    rgbdata[offset + 1] = ordered[1];
    rgbdata[offset + 2] = ordered[2];
}

function applyRgbOrder(color) {
    const order = /^(RGB|RBG|GRB|GBR|BRG|BGR)$/.test(rgbOrder || "") ? rgbOrder : "RGB";
    const channels = { R: color[0], G: color[1], B: color[2] };
    return [
        clampByte(channels[order[0]]),
        clampByte(channels[order[1]]),
        clampByte(channels[order[2]]),
    ];
}

function hexToRgbLocal(hex) {
    const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex || "");
    if (!result) {
        return [0, 0, 0];
    }

    return [
        parseInt(result[1], 16),
        parseInt(result[2], 16),
        parseInt(result[3], 16),
    ];
}

export function Validate(endpoint) {
    return endpoint.interface === 1 &&
        endpoint.usage === KEYCHRON_QMK_USAGE &&
        (endpoint.usage_page === KEYCHRON_QMK_USAGE_PAGE || endpoint.usage_page >= 0xFF00);
}

export function Image() {
    return "";
}
