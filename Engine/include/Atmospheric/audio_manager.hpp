#pragma once
#include "server.hpp"
#ifndef __EMSCRIPTEN__
#include "raudio.h"
#endif
#include <unordered_map>

using SoundID = uint32_t;
using MusicID = uint32_t;

class AudioManager : public Server {
public:
    AudioManager();
    ~AudioManager();

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