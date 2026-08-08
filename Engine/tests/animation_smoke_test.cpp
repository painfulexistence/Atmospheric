// Headless animation smoke test: exercises the animation *data + sampling* layer
// end to end without a GL context, a window, or any asset files. It builds
// skeletons / clips / tracks procedurally and checks the pure math the players
// rely on — so it catches "does sampling crash or produce garbage" regressions
// that only ever surfaced before when we fed real models through the importer.
//
// Deliberately GL-free: SkeletalComponent::Evaluate uploads a bone texture, so
// it cannot run headless. Instead we mirror its joint-hierarchy accumulation
// here and drive it through the engine's own sampler functions
// (SampleVec3Track / SampleQuatTrack / ActionTrack::Sample), which ARE pure.
//
// For *visual* validation of real skinned models (Mixamo glTF, UsdSkel), use the
// GLTFViewer / USDViewer examples — those boot a real Application with GL and
// drive ImportPrefab → Instantiate. This test guards the layer beneath them.
//
// Exit code 0 = all checks passed.
#include <Atmospheric/animation_clip.hpp>
#include <Atmospheric/animation_subsystem.hpp>
#include <Atmospheric/skeleton.hpp>
#include <Atmospheric/vat.hpp>// completes VATClip for AnimationLibrary's unique_ptr member dtor

#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace {

    int g_failures = 0;

    void check(bool cond, const char* what) {
        if (!cond) {
            std::printf("  FAIL: %s\n", what);
            ++g_failures;
        }
    }

    bool finite3(const glm::vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    bool finite4(const glm::vec4& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
    }
    bool finiteMat(const glm::mat4& m) {
        for (int c = 0; c < 4; ++c)
            if (!finite4(m[c])) return false;
        return true;
    }
    bool near3(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
        return glm::length(a - b) <= eps;
    }
    bool nearMat(const glm::mat4& m, const glm::mat4& n, float eps = 1e-4f) {
        for (int c = 0; c < 4; ++c)
            if (glm::length(m[c] - n[c]) > eps) return false;
        return true;
    }

    // A 90° rotation about +Y, stored the way ActionKey/JointChannel carry it.
    const float kS = 0.70710678f;// sin/cos 45°
    glm::quat quatY90() {
        return glm::quat(kS, 0.0f, kS, 0.0f);// glm ctor is (w, x, y, z)
    }

    // ── 1. ActionTrack::Sample (line-B node/tween tracks) ────────────────────────
    void testActionTrack() {
        std::printf("[1] ActionTrack::Sample\n");

        ActionTrack pos;
        pos.property = ActionProperty::Position;
        pos.keys = { { 0.0f, glm::vec4(0, 0, 0, 0), EasingType::Linear },
                     { 1.0f, glm::vec4(10, 0, 0, 0), EasingType::Linear } };
        check(std::abs(pos.Sample(0.5f).x - 5.0f) < 1e-4f, "position lerps to the midpoint");
        check(std::abs(pos.Sample(-1.0f).x - 0.0f) < 1e-4f, "position clamps below the first key");
        check(std::abs(pos.Sample(2.0f).x - 10.0f) < 1e-4f, "position clamps above the last key");
        check(finite4(pos.Sample(0.5f)), "position sample is finite");

        ActionTrack empty;
        empty.property = ActionProperty::Position;
        check(near3(glm::vec3(empty.Sample(0.5f)), glm::vec3(0)), "empty track returns zero");

        // RotationQuat is slerp'd; endpoints exact, midpoint stays unit-length.
        ActionTrack rot;
        rot.property = ActionProperty::RotationQuat;
        rot.keys = { { 0.0f, glm::vec4(0, 0, 0, 1), EasingType::Linear },// identity (x,y,z,w)
                     { 1.0f, glm::vec4(0, kS, 0, kS), EasingType::Linear } };// 90° about Y
        glm::vec4 q0 = rot.Sample(0.0f);
        glm::vec4 qm = rot.Sample(0.5f);
        check(finite4(qm), "rotation sample is finite");
        check(std::abs(glm::length(qm) - 1.0f) < 1e-3f, "slerp keeps the quaternion unit-length");
        check(glm::length(glm::vec4(q0.x, q0.y, q0.z, q0.w) - glm::vec4(0, 0, 0, 1)) < 1e-4f,
            "rotation endpoint is exact");
    }

    // ── 2. Keyframe samplers + bind-pose fallback (line-A skeletal) ───────────────
    void testSamplers() {
        std::printf("[2] SampleVec3Track / SampleQuatTrack\n");

        std::vector<Vec3Key> tk = { { 0.0f, glm::vec3(0) }, { 2.0f, glm::vec3(4, 0, 0) } };
        check(near3(SampleVec3Track(tk, 1.0f, glm::vec3(-1)), glm::vec3(2, 0, 0)), "vec3 linear midpoint");
        check(near3(SampleVec3Track(tk, -5.0f, glm::vec3(-1)), glm::vec3(0)), "vec3 clamps low");
        check(near3(SampleVec3Track(tk, 99.0f, glm::vec3(-1)), glm::vec3(4, 0, 0)), "vec3 clamps high");
        check(near3(SampleVec3Track({}, 1.0f, glm::vec3(9)), glm::vec3(9)), "empty vec3 track returns the fallback");

        std::vector<QuatKey> qk = { { 0.0f, glm::quat(1, 0, 0, 0) }, { 1.0f, quatY90() } };
        glm::quat qm = SampleQuatTrack(qk, 0.5f, glm::quat(1, 0, 0, 0));
        check(std::isfinite(qm.w) && std::abs(glm::length(qm) - 1.0f) < 1e-3f, "quat sample is finite & unit-length");
        glm::quat fb = quatY90();
        glm::quat got = SampleQuatTrack({}, 0.5f, fb);
        check(std::abs(got.w - fb.w) < 1e-4f && std::abs(got.y - fb.y) < 1e-4f,
            "empty quat track returns the fallback (bind pose)");
    }

    // ── 3. SkeletonClip::Recompute duration ──────────────────────────────────────
    void testClipDuration() {
        std::printf("[3] SkeletonClip::Recompute\n");
        SkeletonClip clip;
        clip.name = "dur";
        JointChannel jc;
        jc.joint = 0;
        jc.rotation = { { 0.0f, glm::quat(1, 0, 0, 0) }, { 1.5f, quatY90() } };
        clip.channels.push_back(jc);
        clip.Recompute();
        check(std::abs(clip.duration - 1.5f) < 1e-4f, "duration is the max key time across channels");
    }

    // Local TRS → matrix, matching SkeletalComponent's composition order.
    glm::mat4 localMat(const glm::vec3& t, const glm::quat& r, const glm::vec3& s) {
        return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
    }

    // Mirror of SkeletalComponent::Evaluate's palette build (GL upload elided):
    // sample each joint's local TRS (bind fallback for un-animated channels),
    // accumulate down the hierarchy, then palette[i] = model[i] * inverseBind[i].
    std::vector<glm::mat4> buildPalette(const Skeleton& sk, const SkeletonClip* clip, float time) {
        std::vector<glm::mat4> model(sk.joints.size(), glm::mat4(1.0f));
        std::vector<glm::mat4> palette(sk.joints.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < sk.joints.size(); ++i) {
            const Joint& j = sk.joints[i];
            const JointChannel* ch = nullptr;
            if (clip)
                for (const auto& c : clip->channels)
                    if (c.joint == static_cast<int>(i)) {
                        ch = &c;
                        break;
                    }
            const glm::vec3 t = ch ? SampleVec3Track(ch->translation, time, j.bindTranslation) : j.bindTranslation;
            const glm::quat r = ch ? SampleQuatTrack(ch->rotation, time, j.bindRotation) : j.bindRotation;
            const glm::vec3 s = ch ? SampleVec3Track(ch->scale, time, j.bindScale) : j.bindScale;
            const glm::mat4 local = localMat(t, r, s);
            model[i] = (j.parent < 0) ? local : model[j.parent] * local;
            palette[i] = model[i] * j.inverseBind;
        }
        return palette;
    }

    // ── 4. Hierarchy accumulation + skinning palette ─────────────────────────────
    // The strongest correctness check: at the bind pose the skinning palette must
    // be identity for every joint (model[i] == worldBind[i], and palette =
    // worldBind * inverse(worldBind) = I). If accumulation order, the bind-TRS
    // fallback, or inverseBind were wrong, this would drift off identity.
    void testSkinningPalette() {
        std::printf("[4] hierarchy accumulation + skinning palette\n");

        Skeleton sk;
        Joint root;
        root.name = "root";
        root.parent = -1;
        root.bindTranslation = glm::vec3(0, 1, 0);
        root.bindRotation = glm::quat(1, 0, 0, 0);
        root.bindScale = glm::vec3(1);
        Joint child;
        child.name = "child";
        child.parent = 0;
        child.bindTranslation = glm::vec3(1, 0, 0);
        child.bindRotation = glm::quat(1, 0, 0, 0);
        child.bindScale = glm::vec3(1);

        // Derive inverseBind from the world bind pose (as an importer would).
        const glm::mat4 rootWorld = localMat(root.bindTranslation, root.bindRotation, root.bindScale);
        const glm::mat4 childWorld = rootWorld * localMat(child.bindTranslation, child.bindRotation, child.bindScale);
        root.inverseBind = glm::inverse(rootWorld);
        child.inverseBind = glm::inverse(childWorld);
        sk.joints = { root, child };

        // Empty clip → every joint falls back to its bind pose → identity palette.
        std::vector<glm::mat4> bindPalette = buildPalette(sk, nullptr, 0.0f);
        for (size_t i = 0; i < bindPalette.size(); ++i) {
            check(finiteMat(bindPalette[i]), "bind palette matrix is finite");
            check(nearMat(bindPalette[i], glm::mat4(1.0f)), "bind pose yields an identity skinning matrix");
        }

        // Animate the root (rotate 90° about Y): child must move, all finite, and
        // the palette must leave the bind identity.
        SkeletonClip clip;
        clip.name = "pose";
        JointChannel jc;
        jc.joint = 0;
        jc.rotation = { { 0.0f, quatY90() } };// single key → constant posed rotation
        clip.channels.push_back(jc);
        clip.Recompute();
        std::vector<glm::mat4> posed = buildPalette(sk, &clip, 0.0f);
        bool anyFinite = true, moved = false;
        for (const auto& m : posed) {
            anyFinite = anyFinite && finiteMat(m);
            if (!nearMat(m, glm::mat4(1.0f))) moved = true;
        }
        check(anyFinite, "posed palette matrices are finite");
        check(moved, "a non-bind pose changes the skinning palette");
    }

    // ── 5. AnimationLibrary pointer stability (WASM OOB regression) ───────────────
    // Components cache raw clip pointers for a clip's lifetime; a vector-backed
    // store would dangle them on a later Add* realloc (an out-of-bounds trap on
    // WebAssembly — a bug we actually shipped and fixed). The deque backing must
    // keep earlier element addresses stable across many inserts.
    void testLibraryStability() {
        std::printf("[5] AnimationLibrary deque pointer stability\n");

        AnimationLibrary lib;
        SkeletonClip first;
        first.name = "first";
        JointChannel jc;
        jc.joint = 0;
        jc.rotation = { { 0.0f, glm::quat(1, 0, 0, 0) }, { 1.0f, quatY90() } };
        first.channels.push_back(jc);
        first.Recompute();

        SkeletonClipHandle h0 = lib.AddSkeletonClip(first);
        const SkeletonClip* p0 = lib.GetSkeletonClip(h0);
        check(p0 != nullptr && h0.IsValid(), "first clip is retrievable by handle");

        for (int i = 0; i < 256; ++i) {
            SkeletonClip c;
            c.name = "clip_" + std::to_string(i);
            lib.AddSkeletonClip(c);
        }

        const SkeletonClip* p0After = lib.GetSkeletonClip(h0);
        check(p0 == p0After, "cached clip pointer stays valid after 256 more inserts");
        check(p0After != nullptr && p0After->name == "first", "clip data survives reallocation pressure");
        check(lib.FindSkeletonClip("first") == h0, "by-name lookup still resolves to the original handle");
        check(std::abs(p0After->duration - 1.0f) < 1e-4f, "clip duration is intact");
    }

}// namespace

int main() {
    std::printf("AnimationSmokeTest: sampling + skinning data-layer checks\n");
    testActionTrack();
    testSamplers();
    testClipDuration();
    testSkinningPalette();
    testLibraryStability();

    if (g_failures > 0) {
        std::printf("AnimationSmokeTest: %d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("AnimationSmokeTest: all checks passed\n");
    return 0;
}
