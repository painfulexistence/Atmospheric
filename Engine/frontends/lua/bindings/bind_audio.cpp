#include "../lua_application.hpp"
#include "Atmospheric/audio_manager.hpp"

void BindAudioAPI(sol::state& lua, AudioManager* audio)
{
    sol::table atmos = lua["atmos"];
    sol::table audioTable = atmos.create("audio");

    // ===== Sound API (short sound effects) =====
    audioTable["loadSound"] = [audio](const std::string& path) -> SoundID {
        return audio->LoadSound(path.c_str());
    };

    audioTable["unloadSound"] = [audio](SoundID id) {
        audio->UnloadSound(id);
    };

    audioTable["playSound"] = [audio](SoundID id) {
        audio->PlaySound(id);
    };

    audioTable["playSoundVariation"] = [audio](SoundID id, float pitchVar, float volVar) {
        audio->PlaySoundVariation(id, pitchVar, volVar);
    };

    audioTable["stopSound"] = [audio](SoundID id) {
        audio->StopSound(id);
    };

    audioTable["setSoundVolume"] = [audio](SoundID id, float volume) {
        audio->SetSoundVolume(id, volume);
    };

    audioTable["isSoundPlaying"] = [audio](SoundID id) -> bool {
        return audio->IsSoundPlaying(id);
    };

    // ===== Music API (streaming, for background music) =====
    audioTable["loadMusic"] = [audio](const std::string& path) -> MusicID {
        return audio->LoadMusic(path.c_str());
    };

    audioTable["unloadMusic"] = [audio](MusicID id) {
        audio->UnloadMusic(id);
    };

    audioTable["playMusic"] = [audio](MusicID id) {
        audio->PlayMusic(id);
    };

    audioTable["stopMusic"] = [audio](MusicID id) {
        audio->StopMusic(id);
    };

    audioTable["smoothStopMusic"] = [audio](MusicID id) {
        audio->SmoothStopMusic(id);
    };

    audioTable["setMusicVolume"] = [audio](MusicID id, float volume) {
        audio->SetMusicVolume(id, volume);
    };

    audioTable["isMusicPlaying"] = [audio](MusicID id) -> bool {
        return audio->IsMusicPlaying(id);
    };

    // ===== Global controls =====
    audioTable["stopAllSounds"] = [audio]() {
        audio->StopAllSounds();
    };

    audioTable["stopAllMusic"] = [audio]() {
        audio->StopAllMusics();
    };

    audioTable["stopAll"] = [audio]() {
        audio->StopAll();
    };
}
