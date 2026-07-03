#pragma once
#include "subsystem.hpp"
#ifndef __EMSCRIPTEN__
#include "raudio.h"
#endif
#include <unordered_map>

using SoundID = uint32_t;
using MusicID = uint32_t;

class VideoRecorder;

class AudioSubsystem : public Subsystem {
public:
    AudioSubsystem();
    ~AudioSubsystem();

    void Init(Application* app) override;
    void Process(float dt) override;
    void DrawImGui(float dt) override;
    void Reset();

    // Music API
    MusicID LoadMusic(const char* filename);
    void UnloadMusic(MusicID id);

    void PlayMusic(MusicID id);
    void StopMusic(MusicID id);
    void SmoothStopMusic(MusicID id);
    void StopAllMusics();

    void SetMusicVolume(MusicID id, float volume);
    bool IsMusicPlaying(MusicID id);

    // Sound API
    SoundID LoadSound(const char* filename);
    void UnloadSound(SoundID id);

    void PlaySound(SoundID id);
    void PlaySoundVariation(SoundID id, float pitchVariation, float volumeVariation);
    void StopSound(SoundID id);
    void StopAllSounds();

    void SetSoundVolume(SoundID id, float volume);
    bool IsSoundPlaying(SoundID id);

    // General API
    void StopAll();

    // Hook raudio's final mixed output into a VideoRecorder's audio track.
    // Called automatically by VideoRecorder when captureAudio=true.
    // PCM format: float32 interleaved stereo at 44100 Hz (raudio device default).
#ifndef __EMSCRIPTEN__
    void attachVideoRecorder(VideoRecorder* recorder);
    void detachVideoRecorder();
#endif

#ifdef __EMSCRIPTEN__
    std::unordered_map<SoundID, std::string> soundPaths;
    std::unordered_map<MusicID, std::string> musicPaths;
    std::unordered_map<SoundID, std::vector<int>> soundActiveJsIds;
    std::unordered_map<MusicID, std::vector<int>> musicActiveJsIds;
    std::unordered_map<SoundID, float> soundVolumes;
    std::unordered_map<MusicID, float> musicVolumes;
#else
    std::unordered_map<SoundID, Sound> sounds;
    std::unordered_map<MusicID, Music> musics;
#endif

    SoundID nextSoundId = 1;
    MusicID nextMusicId = 1;
};