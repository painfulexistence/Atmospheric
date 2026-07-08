#include "asset_manager.hpp"
#include "camera_component.hpp"
#include "gfx_factory.hpp"
#include "gl_render_target.hpp"
#include "graphics_subsystem.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "renderer.hpp"
#include "window.hpp"

#include <algorithm>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
#include "command_encoder.hpp"
#include "gpu_pipeline.hpp"
#include "gpu_render_target.hpp"
#include "portal_pass_wgsl.hpp"
#endif

// ============================================================================
//  PortalPass
// ============================================================================

PortalPass::PortalPass() {
    // Handheld tile-based GPUs pay dearly per offscreen pass (each recursion
    // level is a full scene re-render + RT load/store), so drop to a single
    // hop and a smaller RT there. Desktop/web keep the full corridor. Any
    // scene using portals inherits this — no per-example platform code.
#if defined(ANDROID) || (defined(__APPLE__) && TARGET_OS_IOS)
    recursionDepth = 1;
    resolutionScale = 0.3f;
#endif
}

PortalPass::~PortalPass() {
    if (_glFallbackTex) glDeleteTextures(1, &_glFallbackTex);
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    for (auto& [key, entry] : _surfTexBGCache)
        wgpuBindGroupRelease(entry.bg);
    if (_surfUniformBG) wgpuBindGroupRelease(_surfUniformBG);
    if (_surfUniformBGL) wgpuBindGroupLayoutRelease(_surfUniformBGL);
    if (_surfTexBGL) wgpuBindGroupLayoutRelease(_surfTexBGL);
    if (_surfPipeline) wgpuRenderPipelineRelease(_surfPipeline);
    if (_surfDrawBuf) wgpuBufferRelease(_surfDrawBuf);
    if (_surfFallbackTex) wgpuTextureRelease(_surfFallbackTex);
    if (_surfSampler) wgpuSamplerRelease(_surfSampler);
#endif
}

PortalPass::WorldReplay& PortalPass::_replaySlot(size_t index) {
    while (_replays.size() <= index) {
        auto replay = std::make_unique<WorldReplay>();
        replay->forward = std::make_unique<ForwardOpaquePass>();
        replay->skybox = std::make_unique<SkyboxPass>();
        replay->sun = std::make_unique<SunPass>();
        replay->voxel = std::make_unique<VoxelChunkPass>();
        replay->water = std::make_unique<WaterPass>();
        _replays.push_back(std::move(replay));
    }
    return *_replays[index];
}

PortalPass::PortalView& PortalPass::_viewFor(PortalMaterial* mat) {
    for (auto& v : _views)
        if (v.mat == mat) return v;
    _views.push_back({});
    _views.back().mat = mat;
    return _views.back();
}

void PortalPass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    _anyActive = false;
    _activeViewProjs.clear();
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    _surfSlotCursor = 0;// per-frame dynamic-slot allocator for surface draws
#endif
    for (auto& v : _views) {
        v.active = false;
        v.seenThisFrame = false;
    }

    CameraComponent* camera = ctx->GetMainCamera();
    if (!camera) return;
    if (!renderer.sceneRT) return;

    // Collect every PortalMaterial draw submitted this frame. Portals sit in
    // the transparent queue (see PortalMaterial's comment).
    std::unordered_map<PortalMaterial*, std::pair<glm::mat4, Mesh*>> found;
    for (const auto& s : renderer.GetTransparentQueue()) {
        Mesh* mesh = AssetManager::Get().GetMeshPtr(s.cmd.mesh);
        if (!mesh) continue;
        auto* pm = dynamic_cast<PortalMaterial*>(AssetManager::Get().ResolveMaterial(mesh->GetMaterial()));
        if (pm) found[pm] = { s.cmd.transform, mesh };
    }
    if (found.empty()) return;

    for (auto& [pm, entry] : found) {
        PortalView& v = _viewFor(pm);
        v.seenThisFrame = true;
        v.surfaceTransform = entry.first;
        v.mesh = entry.second;
    }

    const glm::mat4 view = camera->GetViewMatrix();
    const glm::mat4 proj = camera->GetProjectionMatrix();
    const glm::vec3 eye = camera->GetEyePosition();

    // 180° about the portal's local up: entering the front of one portal
    // exits the front of the other (the Portal-game convention).
    const glm::mat4 R180 = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));

    // Portal RT sample count: same tradeoff as PlanarReflectionPass — match
    // sceneRT on WebGPU (resolved texture is sampleable, keeps one pipeline
    // sample count), single-sampled on GL (MSAA textures aren't sampler2D).
    const int rtSamples = (GfxFactory::GetBackend() == GfxBackend::WebGPU) ? renderer.sceneRT->GetNumSamples() : 1;
    auto [pw, ph] = Window::Get()->GetPhysicalSize();
    const int rw = std::max(1, static_cast<int>(static_cast<float>(pw) * resolutionScale));
    const int rh = std::max(1, static_cast<int>(static_cast<float>(ph) * resolutionScale));

    auto* mainSky = renderer.GetPass<SkyboxPass>();
    glm::vec3 skyColor = mainSky ? mainSky->skyColor : glm::vec3(0.686f, 0.933f, 0.933f);

    size_t replayIndex = 0;
    for (auto& v : _views) {
        if (!v.seenThisFrame) continue;
        PortalMaterial* partnerMat = v.mat->partner;
        if (!partnerMat) continue;
        auto partnerIt = found.find(partnerMat);
        if (partnerIt == found.end()) continue;// partner not submitted this frame

        const glm::mat4& mSelf = v.surfaceTransform;
        const glm::mat4& mPartner = partnerIt->second.first;

        // Only render the chain when the camera is on the portal's front side
        // (the surface shows the void from behind anyway).
        glm::vec3 nSelf = glm::normalize(glm::vec3(mSelf[2]));
        glm::vec3 pSelf = glm::vec3(mSelf[3]);
        if (glm::dot(nSelf, eye - pSelf) <= 0.0f) continue;

        // (Re)size the recursion RT chain.
        if (static_cast<int>(v.rts.size()) != recursionDepth) {
            v.rts.clear();
            v.rts.resize(recursionDepth);
        }
        bool rtsOK = true;
        for (auto& rt : v.rts) {
            if (!rt) {
                RenderTarget::Props p;
                p.width = rw;
                p.height = rh;
                p.withDepth = true;
                p.hdr = true;
                p.numSamples = rtSamples;
                rt = GfxFactory::CreateRenderTarget(p);
            } else if (rt->GetWidth() != rw || rt->GetHeight() != rh) {
                rt->Resize(rw, rh);
            }
            if (!rt || !rt->IsValid()) rtsOK = false;
        }
        if (!rtsOK) continue;

        // One portal transit, as seen by the view matrix:
        //   view_k = view_main * (M_self * R180 * M_partner^-1)^k
        // and by world-space points (the teleport transform):
        //   T_world = M_partner * R180 * M_self^-1.
        const glm::mat4 tView = mSelf * R180 * glm::inverse(mPartner);
        const glm::mat4 tWorld = mPartner * R180 * glm::inverse(mSelf);

        // Every recursion level looks out through the partner: clip everything
        // behind its plane (facing +Z), with a small slack against z-acne.
        glm::vec3 nPartner = glm::normalize(glm::vec3(mPartner[2]));
        glm::vec3 pPartner = glm::vec3(mPartner[3]);
        glm::vec4 clip(nPartner, -glm::dot(nPartner, pPartner) + 0.01f);

        // Precompute per-level camera state; rts[i] = image after i+1 transits.
        std::vector<glm::mat4> levelViews(recursionDepth);
        std::vector<glm::vec3> levelEyes(recursionDepth);
        {
            glm::mat4 vAcc = view;
            glm::vec3 eAcc = eye;
            for (int i = 0; i < recursionDepth; ++i) {
                vAcc = vAcc * tView;
                eAcc = glm::vec3(tWorld * glm::vec4(eAcc, 1.0f));
                levelViews[i] = vAcc;
                levelEyes[i] = eAcc;
                // Publish this level's frustum for next frame's aux-view culling.
                _activeViewProjs.push_back(proj * vAcc);
            }
        }

        // Mark active BEFORE rendering the chain: the recursion draws nested
        // portal surfaces (below) that sample this same view's deeper RTs, and
        // DrawPortalSurfaces gates content on v.active. Because levels render
        // deepest-first, rts[i+1] is always already rendered when level i draws
        // its nested surface — so the whole corridor fills, not just level 0.
        // (Setting it after the loop was the "only two rings" bug: every nested
        // level saw active == false and fell back to the void.)
        v.active = true;
        _anyActive = true;

        // Deepest level first: rts[i] draws portal surfaces sampling rts[i+1],
        // so the deeper image must exist before the shallower one renders.
        for (int i = recursionDepth - 1; i >= 0; --i) {
            RenderViewOverride ov;
            ov.view = levelViews[i];
            ov.proj = proj;
            ov.eyePos = levelEyes[i];
            ov.target = v.rts[i].get();
            ov.clipPlane = clip;
            ov.flipCull = false;// portal transit is a rigid transform, winding survives

            WorldReplay& replay = _replaySlot(replayIndex + static_cast<size_t>(i));
            replay.skybox->skyColor = skyColor;
            if (mainSky) replay.skybox->horizonColor = mainSky->horizonColor;
            // Voxel palette rides on VoxelMaterial and reaches replay.voxel
            // through the shared render queue — no per-pass mirroring needed.

            renderer.viewOverride = &ov;
            v.rts[i]->Clear(glm::vec4(skyColor, 1.0f));
            replay.forward->Execute(ctx, renderer, enc);
            replay.skybox->Execute(ctx, renderer, enc);
            replay.sun->Execute(ctx, renderer, enc);
            replay.voxel->Execute(ctx, renderer, enc);
            replay.water->Execute(ctx, renderer, enc);// simplified water (depth-less, no reflection)
            // Nested portals: surfaces inside level i+1's image show level i+2
            // (or the void at the recursion floor). Exclude the partner — the
            // virtual camera sits just behind it; it is the window itself.
            DrawPortalSurfaces(ctx, renderer, enc, i + 1, partnerMat);
            renderer.viewOverride = nullptr;
        }

        replayIndex += static_cast<size_t>(recursionDepth);
    }
}

// ============================================================================
//  Portal surface drawing (shared by recursion levels and PortalSurfacePass)
// ============================================================================
#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
void PortalPass::_initSurfaceGPU(WGPUDevice device, WGPUQueue queue, uint32_t sampleCount) {
    _gpuDevice = device;
    _gpuQueue = queue;

    // One dynamic-offset slot per surface draw; opaque, depth-tested and
    // depth-written like solid geometry. Cull None: the back face shows the
    // void fill (handled in the shader), matching the GL path's two-sided draw.
    auto p = GpuPipelineBuilder(device)
                 .wgsl(PORTAL_SURFACE_WGSL)
                 .bgl({ gpuDynUniform(0, wgsl_stage::both, PORTAL_SURF_UNIFORM_SIZE) })
                 .bgl({ gpuTexture(0), gpuSampler(1) })
                 .vertex(
                     56,
                     { { WGPUVertexFormat_Float32x3, 0, 0 },
                       { WGPUVertexFormat_Float32x2, 12, 1 },
                       { WGPUVertexFormat_Float32x3, 20, 2 } }
                 )
                 .colorFormat(WGPUTextureFormat_RGBA16Float)
                 .depth(true, WGPUCompareFunction_Less)
                 .multisample(sampleCount)
                 .build();
    _surfPipeline = p.pipeline;
    _surfUniformBGL = p.bgl(0);
    _surfTexBGL = p.bgl(1);

    {
        WGPUSamplerDescriptor d = gpuSamplerDesc();
        d.minFilter = WGPUFilterMode_Linear;
        d.magFilter = WGPUFilterMode_Linear;
        _surfSampler = wgpuDeviceCreateSampler(device, &d);
    }
    // 1x1 black fallback bound while a surface shows the void (hasView == 0
    // zeroes the sample's contribution; this only satisfies the layout).
    {
        WGPUTextureDescriptor d{};
        d.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        d.dimension = WGPUTextureDimension_2D;
        d.size = { 1, 1, 1 };
        d.format = WGPUTextureFormat_RGBA16Float;
        d.mipLevelCount = 1;
        d.sampleCount = 1;
        _surfFallbackTex = wgpuDeviceCreateTexture(device, &d);

        const uint16_t black[4] = { 0, 0, 0, 0 };// f16 zero bits
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = _surfFallbackTex;
        dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = sizeof(black);
        layout.rowsPerImage = 1;
        WGPUExtent3D extent{ 1, 1, 1 };
        wgpuQueueWriteTexture(queue, &dst, black, sizeof(black), &layout, &extent);
    }
}

void PortalPass::_ensureSurfCapacity(uint32_t slotCount) {
    if (slotCount <= _surfSlotCapacity && _surfDrawBuf) return;

    uint32_t newCapacity = std::max<uint32_t>(slotCount, std::max<uint32_t>(_surfSlotCapacity * 2, 16));

    if (_surfDrawBuf) {
        wgpuBufferRelease(_surfDrawBuf);
        _surfDrawBuf = nullptr;
    }
    if (_surfUniformBG) {
        wgpuBindGroupRelease(_surfUniformBG);
        _surfUniformBG = nullptr;
    }

    WGPUBufferDescriptor d{};
    d.size = static_cast<uint64_t>(newCapacity) * PORTAL_SURF_SLOT_STRIDE;
    d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _surfDrawBuf = wgpuDeviceCreateBuffer(_gpuDevice, &d);
    _surfSlotCapacity = newCapacity;

    _surfUniformBG =
        GpuBindGroupBuilder(_gpuDevice, _surfUniformBGL).buffer(0, _surfDrawBuf, PORTAL_SURF_UNIFORM_SIZE).build();
}

WGPUBindGroup PortalPass::_getOrCreateSurfTexBG(WGPUTexture tex) {
    uintptr_t key = reinterpret_cast<uintptr_t>(tex);
    auto it = _surfTexBGCache.find(key);
    if (it != _surfTexBGCache.end()) {
        if (it->second.tex == tex) return it->second.bg;
        wgpuBindGroupRelease(it->second.bg);
        _surfTexBGCache.erase(it);
    }
    WGPUBindGroup bg = GpuBindGroupBuilder(_gpuDevice, _surfTexBGL).texture(0, tex).sampler(1, _surfSampler).build();
    _surfTexBGCache[key] = { bg, tex };
    return bg;
}
#endif// AE_USE_WEBGPU && __EMSCRIPTEN__

void PortalPass::DrawPortalSurfaces(
    GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc, int sampleLevel, const Material* exclude
) {
    CameraComponent* camera = ctx->GetMainCamera();
    if (!camera) return;

    // Anything to draw?
    bool any = false;
    for (auto& v : _views)
        if (v.seenThisFrame && v.mat != exclude && v.mesh) any = true;
    if (!any) return;

    ResolvedView rv = ResolveView(renderer, camera);
    glm::mat4 viewProj = rv.proj * rv.view;

#if defined(AE_USE_WEBGPU) && defined(__EMSCRIPTEN__)
    if (GfxFactory::GetBackend() == GfxBackend::WebGPU) {
        if (!_surfPipeline) {
            WGPUDevice dev = GfxFactory::GetWebGPUDevice();
            WGPUQueue q = GfxFactory::GetWebGPUQueue();
            if (!dev) return;
            _initSurfaceGPU(dev, q, (uint32_t)rv.target->GetNumSamples());
        }

        // Reserve a slot per surface drawn in this invocation.
        uint32_t needed = 0;
        for (auto& v : _views)
            if (v.seenThisFrame && v.mat != exclude && v.mesh) ++needed;
        _ensureSurfCapacity(_surfSlotCursor + needed);

        auto* gpuEnc = static_cast<GPUCommandEncoder*>(enc);
        rv.target->Begin(enc);
        wgpuRenderPassEncoderSetPipeline(gpuEnc->pass, _surfPipeline);

        for (auto& v : _views) {
            if (!v.seenThisFrame || v.mat == exclude || !v.mesh) continue;
            if (!v.mesh->UsesRenderMesh()) continue;
            Buffer* buf = ctx->GetRenderMesh(v.mesh->GetRenderMeshHandle());
            if (!buf || !buf->IsInitialized() || buf->GetVertexCount() == 0) continue;

            bool hasView = v.active && sampleLevel < static_cast<int>(v.rts.size()) && v.rts[sampleLevel];
            WGPUTexture tex = hasView ? static_cast<GPURenderTarget*>(v.rts[sampleLevel].get())->GetNativeTexture()
                                      : _surfFallbackTex;

            struct {
                glm::mat4 model;
                glm::mat4 viewProj;
                glm::vec4 params0;
                glm::vec4 cameraPos;
            } slotData{
                v.surfaceTransform,
                viewProj,
                glm::vec4(v.mat->rimColor, hasView ? 1.0f : 0.0f),
                glm::vec4(rv.eye, 1.0f),
            };
            uint32_t slot = _surfSlotCursor++;
            wgpuQueueWriteBuffer(
                _gpuQueue,
                _surfDrawBuf,
                static_cast<uint64_t>(slot) * PORTAL_SURF_SLOT_STRIDE,
                &slotData,
                sizeof(slotData)
            );

            uint32_t dynamicOffset = slot * PORTAL_SURF_SLOT_STRIDE;
            wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 0, _surfUniformBG, 1, &dynamicOffset);
            wgpuRenderPassEncoderSetBindGroup(gpuEnc->pass, 1, _getOrCreateSurfTexBG(tex), 0, nullptr);
            buf->Draw(enc, PrimitiveTopology::Triangles);
        }
        rv.target->End();
        return;
    }
#endif
    ShaderProgram* shader = AssetManager::Get().GetShader("portal");
    if (!shader) return;

    glViewport(0, 0, rv.target->GetWidth(), rv.target->GetHeight());
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLRenderTarget*>(rv.target)->GetNativeFBOID());

    // The void-fill draws still sample u_portalTex (select-after-sample, see
    // portal.frag); bind a real 1x1 black texture so the sample is defined
    // (an incomplete texture trips macOS's "texture unloadable" warning).
    if (_glFallbackTex == 0) {
        glGenTextures(1, &_glFallbackTex);
        glBindTexture(GL_TEXTURE_2D, _glFallbackTex);
        const unsigned char black[4] = { 0, 0, 0, 255 };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    shader->Activate();
    shader->SetUniform("u_viewProj", viewProj);
    shader->SetUniform("u_cameraPos", rv.eye);
    shader->SetUniform("u_portalTex", 0);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);// back side shows the void fill (shader-handled)
#if !defined(__EMSCRIPTEN__) && !defined(ANDROID) && !(defined(__APPLE__) && TARGET_OS_IOS)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

    for (auto& v : _views) {
        if (!v.seenThisFrame || v.mat == exclude || !v.mesh) continue;

        bool hasView = v.active && sampleLevel < static_cast<int>(v.rts.size()) && v.rts[sampleLevel];
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(
            GL_TEXTURE_2D, hasView ? static_cast<GLuint>(v.rts[sampleLevel]->GetTextureID()) : _glFallbackTex
        );
        shader->SetUniform("u_model", v.surfaceTransform);
        shader->SetUniform("u_rimColor", v.mat->rimColor);
        shader->SetUniform("u_hasView", hasView ? 1.0f : 0.0f);

        glBindVertexArray(v.mesh->vao);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(v.mesh->triCount * 3), GL_UNSIGNED_SHORT, nullptr);
        glBindVertexArray(0);
    }

    glEnable(GL_CULL_FACE);
    shader->Deactivate();
}

// ============================================================================
//  PortalSurfacePass  (main-view portal windows)
// ============================================================================
void PortalSurfacePass::Execute(GraphicsSubsystem* ctx, Renderer& renderer, CommandEncoder* enc) {
    auto* portals = renderer.GetPass<PortalPass>();
    if (!portals) return;
    portals->DrawPortalSurfaces(ctx, renderer, enc, 0);
}
