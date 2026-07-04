const serialExport = globalThis.serial || {
    availablePorts: function() { return []; },
    getDeviceInfo: function() { return {}; },
    connect: function() { return false; },
    disconnect: function() {},
    isConnected: function() { return false; },
    write: function() { return false; },
    read: function() { return []; },
};

export { serialExport as serial };
export default serialExport;
