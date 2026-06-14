// Injected via --pre-js when AE_USE_WEBGPU=ON.
//
// Requests a WebGPU adapter + device asynchronously before the WASM module
// starts.  Module.addRunDependency / removeRunDependency pause Emscripten's
// runtime until the promise resolves, so emscripten_webgpu_get_device() in
// GfxFactory::Init() will always find Module.preinitializedWebGPUDevice set.
//
// NOTE: --pre-js content is inlined into the generated JS file and shares
// scope with the Emscripten runtime.  Do NOT wrap in an IIFE or redeclare
// `var Module` — that would create a local shadow and break the assignment.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
    Module.addRunDependency('webgpu-preinit');
    (navigator.gpu
        ? navigator.gpu.requestAdapter({ powerPreference: 'high-performance' })
        : Promise.resolve(null))
    .then(function (adapter) {
        return adapter ? adapter.requestDevice() : null;
    })
    .then(function (device) {
        if (device) {
            Module['preinitializedWebGPUDevice'] = device;
            console.log('[AtmosphericEngine] WebGPU device pre-initialised.');
        } else {
            console.warn('[AtmosphericEngine] WebGPU pre-init: no device; will fall back to WebGL 2.');
        }
    })
    .catch(function (e) {
        console.error('[AtmosphericEngine] WebGPU pre-init failed:', e);
    })
    .finally(function () {
        Module.removeRunDependency('webgpu-preinit');
    });
});
