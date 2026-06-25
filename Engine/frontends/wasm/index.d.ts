// Type declarations for the Atmospheric Engine WASM runtime.
// Mirrors the C exports in wasm_api.cpp and the Module shape Emscripten produces.

export type WasmStatus = 'unloaded' | 'loading' | 'ready' | 'error'

/** Shape of the Emscripten Module object after the runtime is initialised. */
export interface AtmosphericModule {
  canvas: HTMLCanvasElement
  ccall: (
    name: string,
    returnType: string | null,
    argTypes: string[],
    args: unknown[],
  ) => unknown
  HEAPU8: Uint8Array
  _malloc: (size: number) => number
  _free: (ptr: number) => void
  requestFullscreen?: () => void
  setStatus?: (text: string) => void
}

/**
 * High-level bridge between the editor and the Atmospheric WASM engine.
 * Construct via createWasmBridge() from @atmospheric/engine/bridge (or your
 * own implementation); call loadModule() once to boot the runtime.
 *
 * Engine C exports (wasm_api.cpp):
 *   ae_load_editor_scene_json  — additive load, replaces same-name scene
 *   ae_add_scene               — additive load, no replace
 *   ae_unload_scene            — destroy named scene + children
 *   ae_get_loaded_scenes       — JSON array of loaded scene names
 *   ae_get_last_loaded_scene   — name from most recent load (poll for async completion)
 *   ae_get_scene_error         — error string, "" on success
 *   ae_load_editor_scene       — CSB binary load (legacy)
 *   ae_mount_file              — inject asset into engine VFS
 *   ae_reset_scene             — re-run OnLoad
 *   ae_get_version             — engine version string
 *   ae_is_ready                — 1 when Application singleton is running
 */
export interface WasmBridge {
  readonly status: WasmStatus
  readonly error: string | null

  /** Boot the WASM runtime. scriptUrl points to atmos.js; wasmUrl overrides Emscripten's locateFile for atmos.wasm. */
  loadModule: (scriptUrl: string, canvas: HTMLCanvasElement, wasmUrl?: string) => Promise<void>

  /** Additive load: replaces any existing scene with the same name, leaves others intact. */
  loadSceneJson: (json: string) => boolean

  /** Truly additive load: stacks the scene alongside existing ones without replacing. */
  addScene: (json: string) => boolean

  /** Destroy a named scene container and all its children. */
  unloadScene: (name: string) => boolean

  /** Returns the list of currently loaded scene names. */
  getLoadedScenes: () => string[]

  /**
   * Name of the scene from the most recent load operation, or '' if it failed.
   * Poll this to detect async load completion (DeferSpawn executes next frame).
   */
  getLastLoadedScene: () => string

  /** Last scene-load error reported by the engine, or '' if the last load succeeded. */
  getSceneError: () => string

  /** Send scene as CSB binary (legacy path, clears all existing scenes). */
  loadSceneData: (data: Uint8Array) => boolean

  /** Inject an asset (texture / audio / etc.) into the engine VFS before loading a scene. */
  mountFile: (path: string, data: Uint8Array) => boolean

  resetScene: () => void
  getVersion: () => string | null
  isEngineReady: () => boolean
  destroy: () => void
}
