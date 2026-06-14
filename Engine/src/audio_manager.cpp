#include "audio_manager.hpp"
#include "fmt/format.h"
#include "imgui.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <cstdlib>
#include <string>
#include <vector>

AudioManager::AudioManager(void) {
    // Initialize Web Audio context and global window.AudioManager
    EM_ASM({
        if (!window.AudioManager) {
            window.AudioManager = (function() {
                var ctx = null;
                var bufferCache = new Map(); // url -> AudioBuffer
                var activeNodes = new Map(); // id -> { source, gain, isMusic, pending, url }
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
                    } catch (e) {}

                    if (exists) {
                        try {
                            var fileData = FS.readFile(url);
                            var bufferCopy = fileData.buffer.slice(fileData.byteOffset, fileData.byteOffset + fileData.byteLength);
                            return getContext().decodeAudioData(bufferCopy)
                                .then(function(buf) {
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
                        .then(function(ab) {
                            return getContext().decodeAudioData(ab);
                        })
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
                    if (pitch !== undefined && pitch !== null) {
                        source.playbackRate.value = pitch;
                    }
                    source.connect(gain).connect(c.destination);
                    
                    source.onended = function() {
                        var node = activeNodes.get(id);
                        if (node && node.source === source) {
                            activeNodes.delete(id);
                        }
                    };
                    
                    activeNodes.set(id, { source: source, gain: gain, isMusic: isMusic, pending: false });
                    source.start(0);
                }

                function play(url, loop, volume, isMusic, pitch) {
                    var buf = bufferCache.get(url);
                    var id = nextId++;
                    if (!buf) {
                        // Not cached yet, load async and play if still pending
                        activeNodes.set(id, { pending: true, url: url, isMusic: isMusic, gain: null, source: null, volume: volume, pitch: pitch });
                        load(url).then(function(loadedBuf) {
                            var nodeInfo = activeNodes.get(id);
                            if (nodeInfo && nodeInfo.pending) {
                                var vol = nodeInfo.volume !== undefined ? nodeInfo.volume : volume;
                                var p = nodeInfo.pitch !== undefined ? nodeInfo.pitch : pitch;
                                startSource(id, loadedBuf, loop, vol, isMusic, p);
                            }
                        }).catch(function(err) {
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
                            } catch(e) {}
                        }
                        activeNodes.delete(id);
                    }
                }

                function stopAll() {
                    activeNodes.forEach(function(node, id) {
                        if (!node.pending && node.source) {
                            try {
                                node.source.stop();
                            } catch(e) {}
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
                                } catch(e) {}
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
                                } catch(e) {}
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

                return {
                    getContext: getContext,
                    load: load,
                    play: play,
                    stop: stop,
                    stopAll: stopAll,
                    stopAllMusics: stopAllMusics,
                    stopAllSounds: stopAllSounds,
                    setVolume: setVolume,
                    isPlaying: isPlaying,
                    unload: unload
                };
            })();
        }

        // Interaction listener to resume audio context
        function resumeAudio() {
            var ctx = window.AudioManager.getContext();
            if (ctx && ctx.state === 'suspended') {
                ctx.resume();
            }
            document.removeEventListener('click',   resumeAudio);
            document.removeEventListener('keydown', resumeAudio);
            document.removeEventListener('touchend', resumeAudio);
        }
        document.addEventListener('click',   resumeAudio);
        document.addEventListener('keydown', resumeAudio);
        document.addEventListener('touchend', resumeAudio);
    });
}

AudioManager::~AudioManager(void) {
    Reset();
}

void AudioManager::Init(Application* app) {
    Server::Init(app);
}

void AudioManager::Process(float dt) {
    // Web Audio plays asynchronously via the browser, so we don't need manual stream updates.
}

void AudioManager::DrawImGui(float dt) {
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

void AudioManager::Reset() {
    StopAll();
    for (auto [id, path] : musicPaths) {
        EM_ASM({
            window.AudioManager.unload(UTF8ToString($0));
        }, path.c_str());
    }
    for (auto [id, path] : soundPaths) {
        EM_ASM({
            window.AudioManager.unload(UTF8ToString($0));
        }, path.c_str());
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

MusicID AudioManager::LoadMusic(const char* filename) {
    MusicID id = nextMusicId++;
    std::string path = filename;
    musicPaths[id] = path;
    musicVolumes[id] = 1.0f;
    
    EM_ASM({
        window.AudioManager.load(UTF8ToString($0)).catch(function(err) {
            console.warn("AudioManager: Load failed for: " + UTF8ToString($0) + " (" + err.message + ")");
        });
    }, path.c_str());
    
    return id;
}

void AudioManager::UnloadMusic(MusicID id) {
    if (musicPaths.count(id) == 0) return;
    StopMusic(id);
    std::string path = musicPaths[id];
    EM_ASM({
        window.AudioManager.unload(UTF8ToString($0));
    }, path.c_str());
    musicPaths.erase(id);
    musicActiveJsIds.erase(id);
    musicVolumes.erase(id);
}

void AudioManager::PlayMusic(MusicID id) {
    if (musicPaths.count(id) == 0) return;
    StopMusic(id);
    std::string path = musicPaths[id];
    float vol = musicVolumes[id];
    
    int jsId = EM_ASM_INT({
        return window.AudioManager.play(UTF8ToString($0), true, $1, true, null);
    }, path.c_str(), vol);
    
    musicActiveJsIds[id].push_back(jsId);
}

void AudioManager::StopMusic(MusicID id) {
    if (musicActiveJsIds.count(id) == 0) return;
    for (int jsId : musicActiveJsIds[id]) {
        EM_ASM({
            window.AudioManager.stop($0);
        }, jsId);
    }
    musicActiveJsIds[id].clear();
}

void AudioManager::SmoothStopMusic(MusicID id) {
    StopMusic(id);
}

void AudioManager::StopAllMusics() {
    EM_ASM({
        window.AudioManager.stopAllMusics();
    });
    for (auto& [id, ids] : musicActiveJsIds) {
        ids.clear();
    }
}

void AudioManager::SetMusicVolume(MusicID id, float volume) {
    if (musicPaths.count(id) == 0) return;
    musicVolumes[id] = volume;
    if (musicActiveJsIds.count(id) > 0) {
        for (int jsId : musicActiveJsIds[id]) {
            EM_ASM({
                window.AudioManager.setVolume($0, $1);
            }, jsId, volume);
        }
    }
}

bool AudioManager::IsMusicPlaying(MusicID id) {
    if (musicPaths.count(id) == 0) return false;
    auto& jsIds = musicActiveJsIds[id];
    for (auto it = jsIds.begin(); it != jsIds.end();) {
        int jsId = *it;
        bool active = EM_ASM_INT({
            return window.AudioManager.isPlaying($0);
        }, jsId);
        if (!active) {
            it = jsIds.erase(it);
        } else {
            return true;
        }
    }
    return false;
}

SoundID AudioManager::LoadSound(const char* filename) {
    SoundID id = nextSoundId++;
    std::string path = filename;
    soundPaths[id] = path;
    soundVolumes[id] = 1.0f;
    
    EM_ASM({
        window.AudioManager.load(UTF8ToString($0)).catch(function(err) {
            console.warn("AudioManager: Load failed for: " + UTF8ToString($0) + " (" + err.message + ")");
        });
    }, path.c_str());
    
    return id;
}

void AudioManager::UnloadSound(SoundID id) {
    if (soundPaths.count(id) == 0) return;
    StopSound(id);
    std::string path = soundPaths[id];
    EM_ASM({
        window.AudioManager.unload(UTF8ToString($0));
    }, path.c_str());
    soundPaths.erase(id);
    soundActiveJsIds.erase(id);
    soundVolumes.erase(id);
}

void AudioManager::PlaySound(SoundID id) {
    if (soundPaths.count(id) == 0) return;
    std::string path = soundPaths[id];
    float vol = soundVolumes[id];
    
    int jsId = EM_ASM_INT({
        return window.AudioManager.play(UTF8ToString($0), false, $1, false, null);
    }, path.c_str(), vol);
    
    soundActiveJsIds[id].push_back(jsId);
}

void AudioManager::PlaySoundVariation(SoundID id, float pitchVariation, float volumeVariation) {
    if (soundPaths.count(id) == 0) return;
    std::string path = soundPaths[id];
    float baseVol = soundVolumes[id];
    
    float vol = baseVol;
    if (volumeVariation > 0.0f) {
        float r = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        vol = baseVol + r * volumeVariation;
        if (vol < 0.0f) vol = 0.0f;
        if (vol > 1.0f) vol = 1.0f;
    }
    
    double pitch = 1.0;
    if (pitchVariation > 0.0f) {
        float r = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        pitch = 1.0 + r * pitchVariation;
        if (pitch < 0.1) pitch = 0.1;
    }

    int jsId = EM_ASM_INT({
        return window.AudioManager.play(UTF8ToString($0), false, $1, false, $2);
    }, path.c_str(), vol, pitch);
    
    soundActiveJsIds[id].push_back(jsId);
}

void AudioManager::StopSound(SoundID id) {
    if (soundActiveJsIds.count(id) == 0) return;
    for (int jsId : soundActiveJsIds[id]) {
        EM_ASM({
            window.AudioManager.stop($0);
        }, jsId);
    }
    soundActiveJsIds[id].clear();
}

void AudioManager::StopAllSounds() {
    EM_ASM({
        window.AudioManager.stopAllSounds();
    });
    for (auto& [id, ids] : soundActiveJsIds) {
        ids.clear();
    }
}

void AudioManager::SetSoundVolume(SoundID id, float volume) {
    if (soundPaths.count(id) == 0) return;
    soundVolumes[id] = volume;
    if (soundActiveJsIds.count(id) > 0) {
        for (int jsId : soundActiveJsIds[id]) {
            EM_ASM({
                window.AudioManager.setVolume($0, $1);
            }, jsId, volume);
        }
    }
}

bool AudioManager::IsSoundPlaying(SoundID id) {
    if (soundPaths.count(id) == 0) return false;
    auto& jsIds = soundActiveJsIds[id];
    for (auto it = jsIds.begin(); it != jsIds.end();) {
        int jsId = *it;
        bool active = EM_ASM_INT({
            return window.AudioManager.isPlaying($0);
        }, jsId);
        if (!active) {
            it = jsIds.erase(it);
        } else {
            return true;
        }
    }
    return false;
}

void AudioManager::StopAll() {
    EM_ASM({
        window.AudioManager.stopAll();
    });
    for (auto& [id, ids] : soundActiveJsIds) {
        ids.clear();
    }
    for (auto& [id, ids] : musicActiveJsIds) {
        ids.clear();
    }
}

#else // !__EMSCRIPTEN__

AudioManager::AudioManager(void) {
    ::InitAudioDevice();
}

AudioManager::~AudioManager(void) {
    for (auto [id, music] : musics) {
        ::UnloadMusicStream(music);
    }
    for (auto [id, sound] : sounds) {
        ::UnloadSound(sound);
    }
    ::CloseAudioDevice();
}

void AudioManager::Init(Application* app) {
    Server::Init(app);
}

void AudioManager::Process(float dt) {
    for (auto [id, music] : musics) {
        ::UpdateMusicStream(music);
    }
}

void AudioManager::DrawImGui(float dt) {
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

void AudioManager::Reset() {
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

MusicID AudioManager::LoadMusic(const char* filename) {
    MusicID id = nextMusicId;
    musics[id] = ::LoadMusicStream(filename);
    nextMusicId++;
    return id;
}

void AudioManager::UnloadMusic(MusicID id) {
    ::UnloadMusicStream(musics[id]);
    musics.erase(id);
}

void AudioManager::PlayMusic(MusicID id) {
    if (musics.count(id) == 0) {
        return;
    }
    ::PlayMusicStream(musics[id]);
}

void AudioManager::StopMusic(MusicID id) {
    if (musics.count(id) == 0) {
        return;
    }
    ::StopMusicStream(musics[id]);
}

void AudioManager::SmoothStopMusic(MusicID id) {
    if (musics.count(id) == 0) {
        return;
    }
    // TODO: implement smooth stop
}

void AudioManager::StopAllMusics() {
    for (auto [id, music] : musics) {
        ::StopMusicStream(music);
    }
}

void AudioManager::SetMusicVolume(MusicID id, float volume) {
    if (musics.count(id) == 0) {
        return;
    }
    ::SetMusicVolume(musics[id], volume);
}

bool AudioManager::IsMusicPlaying(MusicID id) {
    if (musics.count(id) == 0) {
        return false;
    }
    return ::IsMusicStreamPlaying(musics[id]);
}

SoundID AudioManager::LoadSound(const char* filename) {
    SoundID id = nextSoundId;
    sounds[id] = ::LoadSound(filename);
    nextSoundId++;
    return id;
}

void AudioManager::UnloadSound(SoundID id) {
    ::UnloadSound(sounds[id]);
    sounds.erase(id);
}

void AudioManager::PlaySound(SoundID id) {
    if (sounds.count(id) == 0) {
        return;
    }
    ::PlaySound(sounds[id]);
}

void AudioManager::PlaySoundVariation(SoundID id, float pitchVariation, float volumeVariation) {
    if (sounds.count(id) == 0) {
        return;
    }
    // TODO: implement sound variation
    ::PlaySound(sounds[id]);
}

void AudioManager::StopSound(SoundID id) {
    if (sounds.count(id) == 0) {
        return;
    }
    ::StopSound(sounds[id]);
}

void AudioManager::StopAllSounds() {
    for (auto [id, sound] : sounds) {
        ::StopSound(sound);
    }
}

void AudioManager::SetSoundVolume(SoundID id, float volume) {
    if (sounds.count(id) == 0) {
        return;
    }
    ::SetSoundVolume(sounds[id], volume);
}

bool AudioManager::IsSoundPlaying(SoundID id) {
    if (sounds.count(id) == 0) {
        return false;
    }
    return ::IsSoundPlaying(sounds[id]);
}

void AudioManager::StopAll() {
    StopAllSounds();
    StopAllMusics();
}

#endif // __EMSCRIPTEN__