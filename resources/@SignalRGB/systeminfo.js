import * as host from "signalbridge:host";

const systeminfoExport = globalThis.systeminfo || {
    GetMotherboardInfo: function() { return host.getMotherboardInfo(); },
    GetBiosInfo: function() { return host.getBiosInfo(); },
    GetRamInfo: function() { return host.getRamInfo(); },
};

export { systeminfoExport as systeminfo };
export default systeminfoExport;
