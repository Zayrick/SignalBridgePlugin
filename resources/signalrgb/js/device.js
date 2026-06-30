// ─── SignalRGB Bridge: Runtime Device Object ────────────────────
// The `device` global that SignalRGB scripts interact with.
//
// HID I/O methods delegate to `_hid_*` Lua callbacks.
// Frame data is injected from Lua before each Render() call and is
// copied into the runtime channel/subdevice/main buffers here.
// ─────────────────────────────────────────────────────────────────

var device = (function() {

    function preparePacket(data, length) {
        if (!data) return null;
        var arr = Array.isArray(data) ? data : Array.from(data);
        var len = (length !== undefined) ? length : arr.length;
        var pkt = [];
        for (var i = 0; i < len; i++) {
            pkt[i] = (i < arr.length && arr[i] != null) ? (arr[i] & 0xFF) : 0;
        }
        return pkt;
    }

    function toInt(value, fallback) {
        var num = Number(value);
        if (!isFinite(num)) return fallback;
        return num | 0;
    }

    function clampByte(value) {
        var num = toInt(value, 0);
        if (num < 0) return 0;
        if (num > 255) return 255;
        return num;
    }

    function cloneStringArray(values) {
        if (!Array.isArray(values)) return [];
        var out = [];
        for (var i = 0; i < values.length; i++) {
            out.push(values[i] != null ? String(values[i]) : "");
        }
        return out;
    }

    function clonePositionArray(values) {
        if (!Array.isArray(values)) return [];
        var out = [];
        for (var i = 0; i < values.length; i++) {
            var pos = values[i];
            if (Array.isArray(pos) && pos.length >= 2) {
                out.push([toInt(pos[0], 0), toInt(pos[1], 0)]);
            }
        }
        return out;
    }

    function stringArrayEquals(lhs, rhs) {
        lhs = Array.isArray(lhs) ? lhs : [];
        rhs = Array.isArray(rhs) ? rhs : [];
        if (lhs.length !== rhs.length) return false;
        for (var i = 0; i < lhs.length; i++) {
            if (String(lhs[i]) !== String(rhs[i])) return false;
        }
        return true;
    }

    function positionArrayEquals(lhs, rhs) {
        lhs = Array.isArray(lhs) ? lhs : [];
        rhs = Array.isArray(rhs) ? rhs : [];
        if (lhs.length !== rhs.length) return false;
        for (var i = 0; i < lhs.length; i++) {
            var left = lhs[i];
            var right = rhs[i];
            if (!Array.isArray(left) || !Array.isArray(right)) return false;
            if (toInt(left[0], 0) !== toInt(right[0], 0)) return false;
            if (toInt(left[1], 0) !== toInt(right[1], 0)) return false;
        }
        return true;
    }

    function cloneFlatColors(values, expectedLedCount) {
        var out = [];
        if (Array.isArray(values)) {
            for (var i = 0; i < values.length; i++) {
                out.push(clampByte(values[i]));
            }
        }
        var targetLength = Math.max(0, toInt(expectedLedCount, 0)) * 3;
        if (targetLength > 0) {
            while (out.length < targetLength) out.push(0);
            if (out.length > targetLength) out.length = targetLength;
        }
        return out;
    }

    function fillFlatColors(dest, values, expectedLedCount) {
        if (!dest) dest = [];
        var targetLength = Math.max(0, toInt(expectedLedCount, 0)) * 3;
        var srcLen = Array.isArray(values) ? values.length : 0;
        var copyLen = targetLength > 0 ? Math.min(srcLen, targetLength) : srcLen;
        for (var i = 0; i < copyLen; i++) {
            dest[i] = clampByte(values[i]);
        }
        // Zero-pad up to targetLength
        if (targetLength > 0) {
            for (var j = copyLen; j < targetLength; j++) {
                dest[j] = 0;
            }
            // Trim excess
            if (dest.length > targetLength) dest.length = targetLength;
        } else if (dest.length > srcLen) {
            dest.length = srcLen;
        }
        return dest;
    }

    function buildSeededPacket(seed, length) {
        var packet = [];
        length = Math.max(0, toInt(length, 0));
        for (var i = 0; i < length; i++) {
            packet[i] = 0;
        }

        if (Array.isArray(seed)) {
            var copyLen = Math.min(length, seed.length);
            for (var j = 0; j < copyLen; j++) {
                packet[j] = clampByte(seed[j]);
            }
        }

        return packet;
    }

    function mergeReadResult(seed, raw, length) {
        var packet = buildSeededPacket(seed, length);
        if (Array.isArray(raw)) {
            var copyLen = Math.min(packet.length, raw.length);
            for (var i = 0; i < copyLen; i++) {
                packet[i] = clampByte(raw[i]);
            }
        }
        return packet;
    }

    function normalizeOrder(order) {
        if (typeof order !== "string" || order.length < 3) return "RGB";
        order = order.toUpperCase();
        return /^(RGB|RBG|GRB|GBR|BRG|BGR)$/.test(order) ? order : "RGB";
    }

    var _orderMap = {
        RGB: [0, 1, 2], RBG: [0, 2, 1], GRB: [1, 0, 2],
        GBR: [1, 2, 0], BRG: [2, 0, 1], BGR: [2, 1, 0],
    };

    function reorderTriplet(r, g, b, order) {
        var map = _orderMap[normalizeOrder(order)];
        var rgb = [clampByte(r), clampByte(g), clampByte(b)];
        return [rgb[map[0]], rgb[map[1]], rgb[map[2]]];
    }

    function reorderFlatColors(colors, order) {
        var out = [];
        var map = _orderMap[normalizeOrder(order)];
        for (var i = 0; i < colors.length; i += 3) {
            var cr = clampByte(colors[i]);
            var cg = clampByte(colors[i + 1]);
            var cb = clampByte(colors[i + 2]);
            var rgb = [cr, cg, cb];
            out.push(rgb[map[0]], rgb[map[1]], rgb[map[2]]);
        }
        return out;
    }

    function colorsToSeparate(colors, order) {
        order = normalizeOrder(order);
        var a = [], b = [], c = [];
        for (var i = 0; i < colors.length; i += 3) {
            var triplet = reorderTriplet(colors[i], colors[i + 1], colors[i + 2], order);
            a.push(triplet[0]);
            b.push(triplet[1]);
            c.push(triplet[2]);
        }
        return [a, b, c];
    }

    function makeChannelState(name, ledLimit) {
        return {
            name: String(name || ""),
            ledCount: 0,
            ledLimit: Math.max(0, toInt(ledLimit, 0)),
            colors: [],
            needsPulse: true,
        };
    }

    function makeSubdeviceState(name) {
        return {
            name: String(name || ""),
            displayName: String(name || ""),
            imageUrl: "",
            width: 1,
            height: 1,
            ledNames: [],
            ledPositions: [],
            colors: [],
        };
    }

    function frameLedCount(frame, fallback) {
        if (!frame || typeof frame !== "object") {
            return Math.max(0, toInt(fallback, 0));
        }
        var raw = (frame.led_count !== undefined) ? frame.led_count : frame.ledCount;
        return Math.max(0, toInt(raw, fallback));
    }

    function subdeviceLedCount(subdevice) {
        if (!subdevice || typeof subdevice !== "object") return 0;
        return Math.max(
            Array.isArray(subdevice.ledPositions) ? subdevice.ledPositions.length : 0,
            Array.isArray(subdevice.ledNames) ? subdevice.ledNames.length : 0
        );
    }

    function applyChannelFrame(channel, frame) {
        if (!channel) return;

        var ledCount = 0;
        if (frame && Array.isArray(frame.colors)) {
            ledCount = frameLedCount(frame, Math.floor(frame.colors.length / 3));
            if (channel.ledLimit > 0 && ledCount > channel.ledLimit) {
                ledCount = channel.ledLimit;
            }
            channel.colors = fillFlatColors(channel.colors, frame.colors, ledCount);
        } else if (channel.colors.length > 0) {
            channel.colors.length = 0;
        }

        channel.ledCount = ledCount;
        channel.needsPulse = (ledCount === 0);
    }

    function applySubdeviceFrame(subdevice, frame) {
        if (!subdevice) return;
        var colors = (frame && Array.isArray(frame.colors)) ? frame.colors : [];
        subdevice.colors = fillFlatColors(subdevice.colors, colors, subdeviceLedCount(subdevice));
    }

    var pulsePhase = 0;
    var dev;

    function markTopologyDirty() {
        if (dev) dev.__topologyDirty = true;
    }

    dev = {
        _vid: 0,
        _pid: 0,
        _width: 1,
        _height: 1,
        _ledCount: 0,
        _ledNames: [],
        _ledPositions: [],
        _name: null,
        _image: null,
        __props: {},
        __totalLedLimit: 0,
        __channels: [],
        __subdevices: [],
        __mainFrame: { colors: [], width: 1, ledCount: 0 },
        __topologyDirty: true,

        __findChannel: function(name) {
            name = String(name || "");
            for (var i = 0; i < dev.__channels.length; i++) {
                if (dev.__channels[i].name === name) return dev.__channels[i];
            }
            return null;
        },

        __findSubdevice: function(name) {
            name = String(name || "");
            for (var i = 0; i < dev.__subdevices.length; i++) {
                if (dev.__subdevices[i].name === name) return dev.__subdevices[i];
            }
            return null;
        },

        __mainCanvasLedCount: function() {
            var w = Math.max(0, toInt(dev._width, 1));
            var h = Math.max(0, toInt(dev._height, 1));
            return w * h;
        },

        __totalChannelLedCount: function() {
            var total = 0;
            for (var i = 0; i < dev.__channels.length; i++) {
                total += Math.max(0, toInt(dev.__channels[i].ledCount, 0));
            }
            return total;
        },

        __currentMainLedCount: function() {
            if (Array.isArray(dev._ledPositions) && dev._ledPositions.length > 0) {
                return dev._ledPositions.length;
            }
            return Math.max(0, toInt(dev._ledCount, 0));
        },

        __setChannelFrame: function(name, frame) {
            var channel = dev.__findChannel(name);
            if (!channel) return;
            applyChannelFrame(channel, frame);
        },

        __setSubdeviceFrame: function(name, frame) {
            var subdevice = dev.__findSubdevice(name);
            if (!subdevice) return;
            applySubdeviceFrame(subdevice, frame);
        },

        __applyFrames: function(mainFrame, channelFrames, subdeviceFrames) {
            var mf = dev.__mainFrame;
            if (mainFrame && Array.isArray(mainFrame.colors)) {
                var mainLedCount = frameLedCount(mainFrame, Math.floor(mainFrame.colors.length / 3));
                mf.colors = fillFlatColors(mf.colors, mainFrame.colors, mainLedCount);
                mf.width = Math.max(1, toInt(mainFrame.width, dev._width || 1));
                mf.ledCount = mainLedCount;
            } else {
                if (mf.colors.length > 0) mf.colors.length = 0;
                mf.width = Math.max(1, toInt(dev._width, 1));
                mf.ledCount = 0;
            }

            channelFrames = channelFrames || {};
            for (var i = 0; i < dev.__channels.length; i++) {
                var channel = dev.__channels[i];
                applyChannelFrame(channel, channelFrames[channel.name]);
            }

            subdeviceFrames = subdeviceFrames || {};
            for (var j = 0; j < dev.__subdevices.length; j++) {
                var sd = dev.__subdevices[j];
                applySubdeviceFrame(sd, subdeviceFrames[sd.name]);
            }
        },

        __exportRuntimeTopology: function() {
            var channels = [];
            for (var i = 0; i < dev.__channels.length; i++) {
                var ch = dev.__channels[i];
                channels.push({
                    name: ch.name,
                    led_count: Math.max(0, toInt(ch.ledCount, 0)),
                    led_limit: Math.max(0, toInt(ch.ledLimit, 0)),
                    needs_pulse: !!ch.needsPulse,
                });
            }

            var subdevices = [];
            for (var j = 0; j < dev.__subdevices.length; j++) {
                var sd = dev.__subdevices[j];
                var subdeviceLedCount = Math.max(sd.ledPositions.length, sd.ledNames.length);
                subdevices.push({
                    name: sd.name,
                    display_name: sd.displayName,
                    image_url: sd.imageUrl,
                    width: Math.max(1, toInt(sd.width, 1)),
                    height: Math.max(1, toInt(sd.height, 1)),
                    led_count: subdeviceLedCount,
                    led_names: cloneStringArray(sd.ledNames),
                    led_positions: clonePositionArray(sd.ledPositions),
                });
            }

            return {
                name: dev._name || "",
                image_url: dev._image || "",
                total_led_limit: Math.max(0, toInt(dev.__totalLedLimit, 0)),
                main: {
                    width: Math.max(1, toInt(dev._width, 1)),
                    height: Math.max(1, toInt(dev._height, 1)),
                    led_count: dev.__currentMainLedCount(),
                    canvas_led_count: dev.__mainCanvasLedCount(),
                    led_names: cloneStringArray(dev._ledNames),
                    led_positions: clonePositionArray(dev._ledPositions),
                },
                channels: channels,
                subdevices: subdevices,
            };
        },

        __takeTopologyUpdate: function(force) {
            if (!force && !dev.__topologyDirty) return null;
            dev.__topologyDirty = false;
            return dev.__exportRuntimeTopology();
        },

        write: function(data, length) {
            var pkt = preparePacket(data, length);
            return pkt ? _hid_write(pkt) : 0;
        },

        read: function(data, length, timeout) {
            var seed = Array.isArray(data) ? data : [];
            var len = 64, tout = 1000;
            if (typeof data === "number") {
                len = data;
                tout = (length !== undefined) ? toInt(length, 1000) : 1000;
            } else {
                len = length || 64;
                tout = (timeout !== undefined) ? toInt(timeout, 1000) : 1000;
            }
            return mergeReadResult(seed, _hid_read(len, tout), len);
        },

        send_report: function(data, length) {
            var pkt = preparePacket(data, length);
            return pkt ? _hid_send_report(pkt) : 0;
        },

        get_report: function(data, length) {
            var seed = Array.isArray(data) ? data : (data ? Array.from(data) : []);
            var reportId = seed.length > 0 ? (seed[0] | 0) : 0;
            var len = length || 64;
            return mergeReadResult(seed, _hid_get_report(reportId, len), len);
        },

        input_report: function(length, timeout) {
            var len = length || 64;
            var tout = (timeout !== undefined) ? toInt(timeout, 1000) : 1000;
            return mergeReadResult([], _hid_read(len, tout), len);
        },

        set_endpoint: function(arg1, arg2, arg3) {
            var iface, usage, usage_page;
            if (typeof arg1 === "object" && arg1 !== null) {
                iface = arg1["interface"];
                usage = arg1.usage;
                usage_page = arg1.usage_page;
            } else {
                iface = arg1;
                usage = arg2;
                usage_page = arg3;
            }
            return _hid_set_endpoint(
                iface !== undefined ? iface : -1,
                usage !== undefined ? usage : -1,
                usage_page !== undefined ? usage_page : -1
            );
        },

        getHidEndpoints: function() { return _hid_get_endpoints(); },

        getHidInfo: function() {
            return { vid: dev._vid, pid: dev._pid };
        },

        getDeviceInfo: function() {
            return { vid: dev._vid, pid: dev._pid, name: dev._name || "" };
        },

        bulk_transfer: function() {
            console.warn("bulk_transfer is not supported in bridge mode");
            return [];
        },

        control_transfer: function() {
            console.warn("control_transfer is not supported in bridge mode");
            return [];
        },

        color: function(x, y) {
            var frame = dev.__mainFrame || { colors: [], width: 1 };
            var buf = frame.colors || [];
            if (!buf.length) return [0, 0, 0];
            var w = Math.max(1, toInt(frame.width, 1));
            var idx = ((toInt(y, 0) * w) + toInt(x, 0)) * 3;
            if (idx < 0 || idx + 2 >= buf.length) return [0, 0, 0];
            return [buf[idx], buf[idx + 1], buf[idx + 2]];
        },

        subdeviceColor: function(name, x, y) {
            var subdevice = dev.__findSubdevice(name);
            if (!subdevice) return [0, 0, 0];
            for (var i = 0; i < subdevice.ledPositions.length; i++) {
                var pos = subdevice.ledPositions[i];
                if (pos[0] === toInt(x, 0) && pos[1] === toInt(y, 0)) {
                    var idx = i * 3;
                    return [
                        subdevice.colors[idx] || 0,
                        subdevice.colors[idx + 1] || 0,
                        subdevice.colors[idx + 2] || 0,
                    ];
                }
            }
            return [0, 0, 0];
        },

        log: function(msg) {
            _log(typeof msg === "string" ? msg : JSON.stringify(msg));
        },

        pause: function(ms) { _pause(ms || 0); },
        getBrightness: function() { return 100; },
        getLedCount: function() {
            if (dev.__channels.length > 0) {
                return dev.__totalChannelLedCount();
            }
            var mainLedCount = dev.__currentMainLedCount();
            if (mainLedCount > 0) return mainLedCount;
            return dev.__mainCanvasLedCount();
        },
        productId: function() { return dev._pid || 0; },
        flush: function() { return _hid_flush(); },
        clearReadBuffer: function() { return _hid_flush(); },
        getLastReadSize: function() { return _hid_get_last_read_size(); },
        notify: function() {},
        setFrameRateTarget: function() {},
        addFeature: function() {},
        SetLedLimit: function(limit) {
            var nextLimit = Math.max(0, toInt(limit, 0));
            if (dev.__totalLedLimit !== nextLimit) {
                dev.__totalLedLimit = nextLimit;
                markTopologyDirty();
            }
        },
        SetTemperature: function() {},

        setName: function(name) {
            var nextName = name != null ? String(name) : null;
            if (dev._name !== nextName) {
                dev._name = nextName;
                markTopologyDirty();
            }
        },

        setImageFromUrl: function(url) {
            var nextUrl = url != null ? String(url) : null;
            if (dev._image !== nextUrl) {
                dev._image = nextUrl;
                markTopologyDirty();
            }
        },

        setSize: function(s) {
            if (Array.isArray(s)) {
                var nextWidth = Math.max(1, toInt(s[0], 1));
                var nextHeight = Math.max(1, toInt(s[1], 1));
                if (dev._width !== nextWidth || dev._height !== nextHeight) {
                    dev._width = nextWidth;
                    dev._height = nextHeight;
                    markTopologyDirty();
                }
            }
        },

        setControllableLeds: function(names, positions) {
            var nextNames = cloneStringArray(names || []);
            var nextPositions = clonePositionArray(positions || []);
            var nextLedCount = nextPositions.length > 0
                ? nextPositions.length
                : nextNames.length;

            if (
                !stringArrayEquals(dev._ledNames, nextNames) ||
                !positionArrayEquals(dev._ledPositions, nextPositions) ||
                dev._ledCount !== nextLedCount
            ) {
                dev._ledNames = nextNames;
                dev._ledPositions = nextPositions;
                dev._ledCount = nextLedCount;
                markTopologyDirty();
            }
        },

        addProperty: function(prop) {
            if (!prop || !prop.property) return;
            dev.__props[prop.property] = prop;
            var g = (typeof globalThis !== "undefined") ? globalThis : this;
            if (typeof g[prop.property] === "undefined") {
                var def = prop["default"];
                if (prop.type === "boolean") {
                    g[prop.property] = (def === "true" || def === true);
                } else if (prop.type === "number") {
                    g[prop.property] = Number(def) || 0;
                } else {
                    g[prop.property] = (def != null) ? String(def) : "";
                }
            }
        },

        getProperty: function(name) {
            return dev.__props ? dev.__props[name] : undefined;
        },

        removeProperty: function(name) {
            if (dev.__props) delete dev.__props[name];
        },

        getComponentNames: function() {
            if (dev.__channels.length > 0) {
                return dev.getChannelNames();
            }
            return [dev._name || "Main"];
        },

        getComponentData: function(componentName) {
            var channel = dev.__findChannel(componentName);
            if (channel) {
                return {
                    name: channel.name,
                    LedCount: channel.ledCount,
                    ledCount: channel.ledCount,
                };
            }
            var mainCount = dev.getLedCount();
            return {
                name: componentName != null ? String(componentName) : (dev._name || "Main"),
                LedCount: mainCount,
                ledCount: mainCount,
            };
        },

        addChannel: function(name, ledCountOrLimit, explicitLimit) {
            var channel = dev.__findChannel(name);
            var ledLimit = Math.max(
                0,
                toInt(explicitLimit, toInt(ledCountOrLimit, 0))
            );
            if (channel) {
                if (channel.ledLimit !== ledLimit) {
                    channel.ledLimit = ledLimit;
                    if (channel.ledCount > ledLimit && ledLimit > 0) {
                        channel.ledCount = ledLimit;
                        channel.colors = cloneFlatColors(channel.colors, ledLimit);
                    }
                    markTopologyDirty();
                }
                return;
            }
            dev.__channels.push(makeChannelState(name, ledLimit));
            markTopologyDirty();
        },

        removeChannel: function(name) {
            name = String(name || "");
            for (var i = 0; i < dev.__channels.length; i++) {
                if (dev.__channels[i].name === name) {
                    dev.__channels.splice(i, 1);
                    markTopologyDirty();
                    return;
                }
            }
        },

        channel: function(name) {
            var channelName = String(name || "");
            var channel = dev.__findChannel(channelName);
            if (!channel) return null;

            function liveChannel() {
                return dev.__findChannel(channelName);
            }

            var channelView = {
                _name: channelName,
                LedCount: function() {
                    var current = liveChannel();
                    return current ? current.ledCount : 0;
                },
                LedLimit: function() {
                    var current = liveChannel();
                    return current ? current.ledLimit : 0;
                },
                SetLedLimit: function(limit) {
                    var current = liveChannel();
                    if (!current) return 0;
                    var nextLimit = Math.max(0, toInt(limit, 0));
                    if (current.ledLimit === nextLimit) return current.ledLimit;
                    current.ledLimit = nextLimit;
                    if (current.ledCount > current.ledLimit && current.ledLimit > 0) {
                        current.ledCount = current.ledLimit;
                        current.colors = cloneFlatColors(current.colors, current.ledLimit);
                    }
                    current.needsPulse = (current.ledCount === 0);
                    markTopologyDirty();
                    return current.ledLimit;
                },
                shouldPulseColors: function() {
                    var current = liveChannel();
                    return !current || current.ledCount === 0 || !!current.needsPulse;
                },
                getColors: function(format, order) {
                    var current = liveChannel();
                    if (!current) return [];
                    var colors = cloneFlatColors(current.colors, current.ledCount);
                    if (format === "Seperate" || format === "Separate") {
                        return colorsToSeparate(colors, order);
                    }
                    return reorderFlatColors(colors, order);
                },
                getComponentNames: function() {
                    return [channelName];
                },
                getComponentData: function(componentName) {
                    var current = liveChannel();
                    var count = current ? current.ledCount : 0;
                    return {
                        name: componentName != null ? String(componentName) : channelName,
                        LedCount: count,
                        ledCount: count,
                    };
                },
            };

            Object.defineProperties(channelView, {
                _ledCount: {
                    enumerable: true,
                    get: function() {
                        var current = liveChannel();
                        return current ? current.ledCount : 0;
                    },
                },
                ledCount: {
                    enumerable: true,
                    get: function() {
                        var current = liveChannel();
                        return current ? current.ledCount : 0;
                    },
                },
                _ledLimit: {
                    enumerable: true,
                    get: function() {
                        var current = liveChannel();
                        return current ? current.ledLimit : 0;
                    },
                },
                ledLimit: {
                    enumerable: true,
                    get: function() {
                        var current = liveChannel();
                        return current ? current.ledLimit : 0;
                    },
                },
            });

            return channelView;
        },

        getChannelNames: function() {
            var names = [];
            for (var i = 0; i < dev.__channels.length; i++) {
                names.push(dev.__channels[i].name);
            }
            return names;
        },

        getChannelPulseColor: function() {
            pulsePhase = (pulsePhase + 5) % 360;
            var intensity = Math.floor(128 + 127 * Math.sin(pulsePhase * Math.PI / 180));
            var hex = intensity.toString(16).toUpperCase();
            if (hex.length < 2) hex = "0" + hex;
            var half = Math.floor(intensity / 2).toString(16).toUpperCase();
            if (half.length < 2) half = "0" + half;
            return "#00" + half + hex;
        },

        createColorArray: function(c, count, format, order) {
            var rgb = (typeof c === "string") ? hexToRgb(c) : [0, 0, 0];
            var colors = [];
            count = Math.max(0, toInt(count, 0));
            for (var i = 0; i < count; i++) {
                colors.push(rgb[0], rgb[1], rgb[2]);
            }
            if (format === "Seperate" || format === "Separate") {
                return colorsToSeparate(colors, order);
            }
            return reorderFlatColors(colors, order);
        },

        createSubdevice: function(name) {
            if (dev.__findSubdevice(name)) return;
            dev.__subdevices.push(makeSubdeviceState(name));
            markTopologyDirty();
        },

        setSubdeviceName: function(name, displayName) {
            var subdevice = dev.__findSubdevice(name);
            if (!subdevice) return;
            var nextName = String(displayName || name || "");
            if (subdevice.displayName !== nextName) {
                subdevice.displayName = nextName;
                markTopologyDirty();
            }
        },

        setSubdeviceImageUrl: function(name, url) {
            var subdevice = dev.__findSubdevice(name);
            if (!subdevice) return;
            var nextUrl = url != null ? String(url) : "";
            if (subdevice.imageUrl !== nextUrl) {
                subdevice.imageUrl = nextUrl;
                markTopologyDirty();
            }
        },

        setSubdeviceSize: function(name, width, height) {
            var subdevice = dev.__findSubdevice(name);
            if (!subdevice) return;
            var nextWidth = Math.max(1, toInt(width, 1));
            var nextHeight = Math.max(1, toInt(height, 1));
            if (subdevice.width !== nextWidth || subdevice.height !== nextHeight) {
                subdevice.width = nextWidth;
                subdevice.height = nextHeight;
                markTopologyDirty();
            }
        },

        setSubdeviceLeds: function(name, ledNames, ledPositions) {
            var subdevice = dev.__findSubdevice(name);
            if (!subdevice) return;
            var nextNames = cloneStringArray(ledNames || []);
            var nextPositions = clonePositionArray(ledPositions || []);
            if (
                !stringArrayEquals(subdevice.ledNames, nextNames) ||
                !positionArrayEquals(subdevice.ledPositions, nextPositions)
            ) {
                subdevice.ledNames = nextNames;
                subdevice.ledPositions = nextPositions;
                subdevice.colors = cloneFlatColors(subdevice.colors, subdeviceLedCount(subdevice));
                markTopologyDirty();
            }
        },

        removeSubdevice: function(name) {
            name = String(name || "");
            for (var i = 0; i < dev.__subdevices.length; i++) {
                if (dev.__subdevices[i].name === name) {
                    dev.__subdevices.splice(i, 1);
                    markTopologyDirty();
                    return;
                }
            }
        },

        getCurrentSubdevices: function() {
            var names = [];
            for (var i = 0; i < dev.__subdevices.length; i++) {
                names.push(dev.__subdevices[i].name);
            }
            return names;
        },

        createFanControl: function() {},
        removeFanControl: function() {},
        fanControlDisabled: function() {},
        getFanlevel: function() { return 0; },
        getNormalizedFanlevel: function() { return 0; },
        setRPM: function() {},
        createTemperatureSensor: function() {},
        removeTemperatureSensor: function() {},
        getImageBuffer: function() { return []; },
    };

    return dev;
})();

function __srgb_export_topology() {
    return device.__exportRuntimeTopology();
}

function __srgb_is_topology_dirty() {
    return !!device.__topologyDirty;
}

function __srgb_take_topology_update(force) {
    var topology = device.__takeTopologyUpdate(!!force);
    return topology || false;
}

function __srgb_apply_pending_frames() {
    var mainFrame = (typeof __srgb_main_frame !== "undefined") ? __srgb_main_frame : null;
    var channelFrames = (typeof __srgb_channel_frames !== "undefined") ? __srgb_channel_frames : null;
    var subdeviceFrames = (typeof __srgb_subdevice_frames !== "undefined") ? __srgb_subdevice_frames : null;
    device.__applyFrames(mainFrame, channelFrames, subdeviceFrames);
}
