#include "audio_subsystem.hpp"
#include "file_system.hpp"
#include "fmt/format.h"
#include "imgui.h"
#include "video_recorder.hpp"
#include <stdexcept>

// Defined once for both platform builds (the ctors/dtors below are branched).
AudioSubsystem* AudioSubsystem::_instance = nullptr;

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <random>
#include <string>
#include <vector>

// Uniform random float in [-1, 1] for pitch/volume variation. A properly
// seeded PRNG rather than rand(), which is low-quality and shares global state.
static float RandBipolar() {
    static std::mt19937 rng{ std::random_device{}() };
    static std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    return dist(rng);
}

AudioSubsystem::AudioSubsystem(void) {
    if (_instance != nullptr) throw std::runtime_error("AudioSubsystem is already initialized!");
    _instance = this;

    // Initialize Web Audio context and global window.AudioManager
    EM_ASM({
        if (!window.AudioManager) {
            window.AudioManager = (function() {
                var ctx = null;
                var bufferCache = new Map();// url -> AudioBuffer
                var activeNodes = new Map();// id -> { source, gain, isMusic, pending, url }
                var nextId = 1;

                function getContext() {
                    if (!ctx) {
                        ctx = new (window.AudioContext || window.webkitAudioContext)();
                    }
                    return ctx;
                }

                function load(url) {
                    if (bufferCache.has(url)) {
                        return Promise.resolve(bufferCache.get(url));
                    }
                    var exists = false;
                    try {
                        var lookup = FS.lookupPath(url);
                        exists = !!lookup.node;
                    } catch (e) {
                    }

                    if (exists) {
                        try {
                            var fileData = FS.readFile(url);
                            var bufferCopy =
                                fileData.buffer.slice(fileData.byteOffset, fileData.byteOffset + fileData.byteLength);
                            return getContext().decodeAudioData(bufferCopy).then(function(buf) {
                                bufferCache.set(url, buf);
                                return buf;
                            });
                        } catch (err) {
                            console.error("Failed to read audio from MEMFS: " + url, err);
                        }
                    }

                    return fetch(url)
                        .then(function(resp) {
                            if (!resp.ok) {
                                throw new Error("HTTP error! Status: " + resp.status);
                            }
                            return resp.arrayBuffer();
                        })
                        .then(function(ab) { return getContext().decodeAudioData(ab); })
                        .then(function(buf) {
                            bufferCache.set(url, buf);
                            return buf;
                        });
                }

                function startSource(id, buf, loop, volume, isMusic, pitch) {
                    var c = getContext();
                    var source = c.createBufferSource();
                    var gain = c.createGain();
                    source.buffer = buf;
                    source.loop = loop;
                    gain.gain.value = volume;
                    if (pitch != = undefined&& pitch != = null) {
                        source.playbackRate.value = pitch;
                    }
                    source.connect(gain).connect(c.destination);

                    source.onended = function() {
                        var node = activeNodes.get(id);
                        if (node&& node.source == = source) {
                            activeNodes.delete(id);
                        }
                    };

                    activeNodes.set(id, { source : source, gain : gain, isMusic : isMusic, pending : false });
                    source.start(0);
                }

                function play(url, loop, volume, isMusic, pitch) {
                    var buf = bufferCache.get(url);
                    var id = nextId++;
                    if (!buf) {
                        // Not cached yet, load async and play if still pending
                        activeNodes.set(id, {
                            pending : true,
                            url : url,
                            isMusic : isMusic,
                            gain : null,
                            source : null,
                            volume : volume,
                            pitch : pitch
                        });
                        load(url)
                            .then(function(loadedBuf) {
                                var nodeInfo = activeNodes.get(id);
                                if (nodeInfo && nodeInfo.pending) {
                                    var vol = nodeInfo.volume != = undefined ? nodeInfo.volume : volume;
                                    var p = nodeInfo.pitch != = undefined ? nodeInfo.pitch : pitch;
                                    startSource(id, loadedBuf, loop, vol, isMusic, p);
                                }
                            })
                            .catch(function(err) {
                                console.error("Failed to load and play audio: " + url, err);
                                activeNodes.delete(id);
                            });
                        return id;
                    }
                    startSource(id, buf, loop, volume, isMusic, pitch);
                    return id;
                }

                function stop(id) {
                    var node = activeNodes.get(id);
                    if (node) {
                        if (!node.pending && node.source) {
                            try {
                                node.source.stop();
                            } catch (e) {
                            }
                        }
                        activeNodes.delete(id);
                    }
                }

                function stopAll() {
                    activeNodes.forEach(function(node, id) {
                        if (!node.pending && node.source) {
                            try {
                                node.source.stop();
                            } catch (e) {
                            }
                        }
                    });
                    activeNodes.clear();
                }

                function stopAllMusics() {
                    activeNodes.forEach(function(node, id) {
                        if (node.isMusic) {
                            if (!node.pending && node.source) {
                                try {
                                    node.source.stop();
                                } catch (e) {
                                }
                            }
                            activeNodes.delete(id);
                        }
                    });
                }

                function stopAllSounds() {
                    activeNodes.forEach(function(node, id) {
                        if (!node.isMusic) {
                            if (!node.pending && node.source) {
                                try {
                                    node.source.stop();
                                } catch (e) {
                                }
                            }
                            activeNodes.delete(id);
                        }
                    });
                }

                function setVolume(id, vol) {
                    var node = activeNodes.get(id);
                    if (node) {
                        if (!node.pending && node.gain) {
                            node.gain.gain.value = vol;
                        } else {
                            node.volume = vol;
                        }
                    }
                }

                function isPlaying(id) {
                    var node = activeNodes.get(id);
                    if (node) {
                        return true;
                    }
                    return false;
                }

                function unload(url) {
                    bufferCache.delete(url);
                }

                function shutdown() {
                    stopAll();
                    if (ctx) {
                        try {
                            ctx.close();
                        } catch (e) {
                        }
                        ctx = null;
                    }
                    bufferCache.clear();
                }

                return {
                    getContext : getContext,
                    load : load,
                    play : play,
                    stop : stop,
                    stopAll : stopAll,
                    stopAllMusics : stopAllMusics,
                    stopAllSounds : stopAllSounds,
                    setVolume : setVolume,
                    isPlaying : isPlaying,
                    unload : unload,
                    shutdown : shutdown
                };
            })();
        }

        // Interaction listener to resume audio context
        function resumeAudio() {
            var ctx = window.AudioManager.getContext();
            if (ctx&& ctx.state == = 'suspended') {
                ctx.resume();
            }
            document.removeEventListener('click', resumeAudio);
            document.removeEventListener('keydown', resumeAudio);
            document.removeEventListener('touchend', resumeAudio);
        }
        document.addEventListener('click', resumeAudio);
        document.addEventListener('keydown', resumeAudio);
        document.addEventListener('touchend', resumeAudio);
    });
}

AudioSubsystem::~AudioSubsystem(void) {
    Reset();
    if (_instance == this) {
        _instance = nullptr;
    }
}

void AudioSubsystem::Init(Application* app) {
    Subsystem::Init(app);
}

void AudioSubsystem::Process(float dt) {
    // Web Audio plays asynchronously via the browser, so we don't need manual stream updates.
}

void AudioSubsystem::DrawImGui(float dt) {
    if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Stop All")) {
            StopAll();
        }
        ImGui::Separator();
        if (ImGui::TreeNode("Musics")) {
            for (auto [id, path] : musicPaths) {
                if (ImGui::TreeNode(fmt::format("Music {}: {}", id, path).c_str())) {
                    if (ImGui::SmallButton("Play")) {
                        PlayMusic(id);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Stop")) {
                        StopMusic(id);
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Sounds")) {
            for (auto [id, path] : soundPaths) {
                if (ImGui::TreeNode(fmt::format("Sound {}: {}", id, path).c_str())) {
                    if (ImGui::SmallButton("Play")) {
                        PlaySound(id);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Stop")) {
                        StopSound(id);
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }
}

void AudioSubsystem::Reset() {
    StopAll();
    for (auto [id, path] : musicPaths) {
        EM_ASM({ window.AudioManager.unload(UTF8ToString($0)); }, path.c_str());
    }
    for (auto [id, path] : soundPaths) {
        EM_ASM({ window.AudioManager.unload(UTF8ToString($0)); }, path.c_str());
    }
    musicPaths.clear();
    soundPaths.clear();
    musicActiveJsIds.clear();
    soundActiveJsIds.clear();
    musicVolumes.clear();
    soundVolumes.clear();
    nextMusicId = 1;
    nextSoundId = 1;
}

MusicID AudioSubsystem::LoadMusic(const char* filename) {
    MusicID id = nextMusicId++;
    std::string path = FileSystem::Get().ResolvePath(filename).value_or(filename);
    musicPaths[id] = path;
    musicVolumes[id] = 1.0f;

    EM_ASM(
        {
            window.AudioManager.load(UTF8ToString($0)).catch(function(err) {
                console.warn("AudioSubsystem: Load failed for: " + UTF8ToString($0) + " (" + err.message + ")");
            });
        },
        path.c_str()
    );

    return id;
}

void AudioSubsystem::UnloadMusic(MusicID id) {
    if (!musicPaths.contains(id)) return;
    StopMusic(id);
    std::string path = musicPaths[id];
    EM_ASM({ window.AudioManager.unload(UTF8ToString($0)); }, path.c_str());
    musicPaths.erase(id);
    musicActiveJsIds.erase(id);
    musicVolumes.erase(id);
}

void AudioSubsystem::PlayMusic(MusicID id) {
    if (!musicPaths.contains(id)) return;
    StopMusic(id);
    std::string path = musicPaths[id];
    float vol = musicVolumes[id];

    int jsId =
        EM_ASM_INT({ return window.AudioManager.play(UTF8ToString($0), true, $1, true, null); }, path.c_str(), vol);

    musicActiveJsIds[id].push_back(jsId);
}

void AudioSubsystem::StopMusic(MusicID id) {
    if (!musicActiveJsIds.contains(id)) return;
    for (int jsId : musicActiveJsIds[id]) {
        EM_ASM({ window.AudioManager.stop($0); }, jsId);
    }
    musicActiveJsIds[id].clear();
}

void AudioSubsystem::SmoothStopMusic(MusicID id) {
    StopMusic(id);
}

void AudioSubsystem::StopAllMusics() {
    EM_ASM({ window.AudioManager.stopAllMusics(); });
    for (auto& [id, ids] : musicActiveJsIds) {
        ids.clear();
    }
}

void AudioSubsystem::SetMusicVolume(MusicID id, float volume) {
    if (!musicPaths.contains(id)) return;
    musicVolumes[id] = volume;
    if (musicActiveJsIds.contains(id)) {
        for (int jsId : musicActiveJsIds[id]) {
            EM_ASM({ window.AudioManager.setVolume($0, $1); }, jsId, volume);
        }
    }
}

bool AudioSubsystem::IsMusicPlaying(MusicID id) {
    if (!musicPaths.contains(id)) return false;
    auto& jsIds = musicActiveJsIds[id];
    for (auto it = jsIds.begin(); it != jsIds.end();) {
        int jsId = *it;
        bool active = EM_ASM_INT({ return window.AudioManager.isPlaying($0); }, jsId);
        if (!active) {
            it = jsIds.erase(it);
        } else {
            return true;
        }
    }
    return false;
}

SoundID AudioSubsystem::LoadSound(const char* filename) {
    SoundID id = nextSoundId++;
    std::string path = FileSystem::Get().ResolvePath(filename).value_or(filename);
    soundPaths[id] = path;
    soundVolumes[id] = 1.0f;

    EM_ASM(
        {
            window.AudioManager.load(UTF8ToString($0)).catch(function(err) {
                console.warn("AudioSubsystem: Load failed for: " + UTF8ToString($0) + " (" + err.message + ")");
            });
        },
        path.c_str()
    );

    return id;
}

void AudioSubsystem::UnloadSound(SoundID id) {
    if (!soundPaths.contains(id)) return;
    StopSound(id);
    std::string path = soundPaths[id];
    EM_ASM({ window.AudioManager.unload(UTF8ToString($0)); }, path.c_str());
    soundPaths.erase(id);
    soundActiveJsIds.erase(id);
    soundVolumes.erase(id);
}

void AudioSubsystem::PlaySound(SoundID id) {
    if (!soundPaths.contains(id)) return;
    std::string path = soundPaths[id];
    float vol = soundVolumes[id];

    int jsId =
        EM_ASM_INT({ return window.AudioManager.play(UTF8ToString($0), false, $1, false, null); }, path.c_str(), vol);

    soundActiveJsIds[id].push_back(jsId);
}

void AudioSubsystem::PlaySoundVariation(SoundID id, float pitchVariation, float volumeVariation) {
    if (!soundPaths.contains(id)) return;
    std::string path = soundPaths[id];
    float baseVol = soundVolumes[id];

    float vol = baseVol;
    if (volumeVariation > 0.0f) {
        vol = baseVol + RandBipolar() * volumeVariation;
        if (vol < 0.0f) vol = 0.0f;
        if (vol > 1.0f) vol = 1.0f;
    }

    double pitch = 1.0;
    if (pitchVariation > 0.0f) {
        pitch = 1.0 + RandBipolar() * pitchVariation;
        if (pitch < 0.1) pitch = 0.1;
    }

    int jsId = EM_ASM_INT(
        { return window.AudioManager.play(UTF8ToString($0), false, $1, false, $2); }, path.c_str(), vol, pitch
    );

    soundActiveJsIds[id].push_back(jsId);
}

void AudioSubsystem::StopSound(SoundID id) {
    if (!soundActiveJsIds.contains(id)) return;
    for (int jsId : soundActiveJsIds[id]) {
        EM_ASM({ window.AudioManager.stop($0); }, jsId);
    }
    soundActiveJsIds[id].clear();
}

void AudioSubsystem::StopAllSounds() {
    EM_ASM({ window.AudioManager.stopAllSounds(); });
    for (auto& [id, ids] : soundActiveJsIds) {
        ids.clear();
    }
}

void AudioSubsystem::SetSoundVolume(SoundID id, float volume) {
    if (!soundPaths.contains(id)) return;
    soundVolumes[id] = volume;
    if (soundActiveJsIds.contains(id)) {
        for (int jsId : soundActiveJsIds[id]) {
            EM_ASM({ window.AudioManager.setVolume($0, $1); }, jsId, volume);
        }
    }
}

bool AudioSubsystem::IsSoundPlaying(SoundID id) {
    if (!soundPaths.contains(id)) return false;
    auto& jsIds = soundActiveJsIds[id];
    for (auto it = jsIds.begin(); it != jsIds.end();) {
        int jsId = *it;
        bool active = EM_ASM_INT({ return window.AudioManager.isPlaying($0); }, jsId);
        if (!active) {
            it = jsIds.erase(it);
        } else {
            return true;
        }
    }
    return false;
}

void AudioSubsystem::StopAll() {
    EM_ASM({ window.AudioManager.stopAll(); });
    for (auto& [id, ids] : soundActiveJsIds) {
        ids.clear();
    }
    for (auto& [id, ids] : musicActiveJsIds) {
        ids.clear();
    }
}

#else// !__EMSCRIPTEN__

// Non-owning bridge to the Application-owned VideoRecorder. raudio's C
// callback API has no userdata parameter, so a file-scope static is the only
// channel; attach/detach keep it symmetric with the processor registration,
// and detach always runs before the recorder is destroyed (stopRecording).
static VideoRecorder* gsCaptureRecorder = nullptr;
static void AudioCaptureCallback(void* buffer, unsigned int frames) {
    if (gsCaptureRecorder) gsCaptureRecorder->writeAudio(static_cast<const float*>(buffer), frames);
}

void AudioSubsystem::attachVideoRecorder(VideoRecorder* recorder) {
    gsCaptureRecorder = recorder;
    ::AttachAudioMixedProcessor(AudioCaptureCallback);
}

void AudioSubsystem::detachVideoRecorder() {
    ::DetachAudioMixedProcessor(AudioCaptureCallback);
    gsCaptureRecorder = nullptr;
}

AudioSubsystem::AudioSubsystem(void) {
    if (_instance != nullptr) throw std::runtime_error("AudioSubsystem is already initialized!");
    _instance = this;
    ::InitAudioDevice();
}

AudioSubsystem::~AudioSubsystem(void) {
    for (auto [id, music] : musics) {
        ::UnloadMusicStream(music);
    }
    for (auto [id, sound] : sounds) {
        ::UnloadSound(sound);
    }
    ::CloseAudioDevice();
    if (_instance == this) {
        _instance = nullptr;
    }
}

void AudioSubsystem::Init(Application* app) {
    Subsystem::Init(app);
}

void AudioSubsystem::Process(float dt) {
    for (auto [id, music] : musics) {
        ::UpdateMusicStream(music);
    }
}

void AudioSubsystem::DrawImGui(float dt) {
    if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Stop All")) {
            StopAll();
        }
        ImGui::Separator();
        if (ImGui::TreeNode("Musics")) {
            for (auto [id, music] : musics) {
                if (ImGui::TreeNode(fmt::format("Music {}", id).c_str())) {
                    if (ImGui::SmallButton("Play")) {
                        PlayMusic(id);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Stop")) {
                        StopMusic(id);
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Sounds")) {
            for (auto [id, sound] : sounds) {
                if (ImGui::TreeNode(fmt::format("Sound {}", id).c_str())) {
                    if (ImGui::SmallButton("Play")) {
                        PlaySound(id);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Stop")) {
                        StopSound(id);
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }
}

void AudioSubsystem::Reset() {
    StopAll();

    for (auto [id, music] : musics) {
        ::UnloadMusicStream(music);
    }
    musics.clear();

    for (auto [id, sound] : sounds) {
        ::UnloadSound(sound);
    }
    sounds.clear();

    nextMusicId = 1;
    nextSoundId = 1;
}

MusicID AudioSubsystem::LoadMusic(const char* filename) {
    MusicID id = nextMusicId;
    std::string path = FileSystem::Get().ResolvePath(filename).value_or(filename);
    musics[id] = ::LoadMusicStream(path.c_str());
    nextMusicId++;
    return id;
}

void AudioSubsystem::UnloadMusic(MusicID id) {
    ::UnloadMusicStream(musics[id]);
    musics.erase(id);
}

void AudioSubsystem::PlayMusic(MusicID id) {
    if (!musics.contains(id)) {
        return;
    }
    ::PlayMusicStream(musics[id]);
}

void AudioSubsystem::StopMusic(MusicID id) {
    if (!musics.contains(id)) {
        return;
    }
    ::StopMusicStream(musics[id]);
}

void AudioSubsystem::SmoothStopMusic(MusicID id) {
    if (!musics.contains(id)) {
        return;
    }
    // TODO: implement smooth stop
}

void AudioSubsystem::StopAllMusics() {
    for (auto [id, music] : musics) {
        ::StopMusicStream(music);
    }
}

void AudioSubsystem::SetMusicVolume(MusicID id, float volume) {
    if (!musics.contains(id)) {
        return;
    }
    ::SetMusicVolume(musics[id], volume);
}

bool AudioSubsystem::IsMusicPlaying(MusicID id) {
    if (!musics.contains(id)) {
        return false;
    }
    return ::IsMusicStreamPlaying(musics[id]);
}

SoundID AudioSubsystem::LoadSound(const char* filename) {
    SoundID id = nextSoundId;
    std::string path = FileSystem::Get().ResolvePath(filename).value_or(filename);
    sounds[id] = ::LoadSound(path.c_str());
    nextSoundId++;
    return id;
}

void AudioSubsystem::UnloadSound(SoundID id) {
    ::UnloadSound(sounds[id]);
    sounds.erase(id);
}

void AudioSubsystem::PlaySound(SoundID id) {
    if (!sounds.contains(id)) {
        return;
    }
    ::PlaySound(sounds[id]);
}

void AudioSubsystem::PlaySoundVariation(SoundID id, float pitchVariation, float volumeVariation) {
    if (!sounds.contains(id)) {
        return;
    }
    // TODO: implement sound variation
    ::PlaySound(sounds[id]);
}

void AudioSubsystem::StopSound(SoundID id) {
    if (!sounds.contains(id)) {
        return;
    }
    ::StopSound(sounds[id]);
}

void AudioSubsystem::StopAllSounds() {
    for (auto [id, sound] : sounds) {
        ::StopSound(sound);
    }
}

void AudioSubsystem::SetSoundVolume(SoundID id, float volume) {
    if (!sounds.contains(id)) {
        return;
    }
    ::SetSoundVolume(sounds[id], volume);
}

bool AudioSubsystem::IsSoundPlaying(SoundID id) {
    if (!sounds.contains(id)) {
        return false;
    }
    return ::IsSoundPlaying(sounds[id]);
}

void AudioSubsystem::StopAll() {
    StopAllSounds();
    StopAllMusics();
}

#endif// __EMSCRIPTEN__