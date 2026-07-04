const ContextErrorExport = globalThis.ContextError;
const AssertExport = globalThis.Assert;
const globalContextExport = globalThis.globalContext;

export { AssertExport as Assert, ContextErrorExport as ContextError, globalContextExport as globalContext };
export default { Assert: AssertExport, ContextError: ContextErrorExport, globalContext: globalContextExport };
