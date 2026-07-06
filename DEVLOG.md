## Devlog

- 2024/03/22 - considering using glad+sdl2 instead of glew+glfw
- 2024/06/24 - upgraded dear imgui so that it can work on Apple M3 machines
- 2024/06/26 - fixed an issue where the error is not thrown when the framebuffer is incomplete (missing "throw" keyword)
- 2024/06/26 - fixed an issue where glGetString(GL_VERSION) returns null and all textures are missing and not loadable by switching to system-wide GLEW and back
- 2024/06/26 - found out that passing vec3(0, 0, 0) to shadow pass World matrix can cause an interesting bug where the shadows move autonomously
- 2024/06/27 - implemented normal mapping (normals flipped)
- 2024/06/28 - fixed flipped normals
- 2024/06/28 - it turns out that the bottleneck is light calculation; turning aux lights off can make the game run at a reasonable speed
- 2024/06/29 - using high frequency image (such as brown_mud_leaves_norm_gl.jpg) as a height map can generate an interesting effect
- 2024/06/30 - implemented a tessellated terrain
- 2024/07/03 - implemented phyiscs debug UI and learned that constantly growing std::vector without reserving space can cause freeze
- 2025/01/09 - unable to resolve "fatal error LNK1104: cannot open file 'm.lib'" on Windows
- 2025/05/25 - realized that LNK1104 might be caused by raudio or lua, solved by removing raudio
- 2026/06/01 - completed major engine port to WebAssembly and resolved critical WebGL2 rendering bugs
- 2026/07/06 - added Vertex Animation Texture (VAT) playback: vertices are displaced from baked position/normal textures in vat.vert (fetched by gl_VertexID), so cloth/soft-body/fluid clips play with no compute or CPU skinning; VATClip::Bake + VATComponent drive it, 3DBasics demos it with a runtime-baked blob
- 2026/07/06 - wired VAT across every pass and backend: VATClip now bakes through GfxFactory (RGBA32F on GL, rgba32float on WebGPU) so it is backend-agnostic; ShadowPass got VAT-aware directional + omni depth shaders (GL) and a displacing shadow pipeline (WebGPU) so shadows track the animation; ForwardOpaquePass got a WebGPU VAT pipeline (VAT_WGSL, group 3 = animation textures via textureLoad). The deferred passes aren't in the render graph, so they need no VAT path. Untested in-repo (no Emscripten/GPU in CI); VAT code is isolated to VAT materials so it can't regress normal rendering on any backend
- TODO: use std::filesystem to get the correct asset loading path

