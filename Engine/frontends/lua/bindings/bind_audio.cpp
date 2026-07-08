#include "../lua_application.hpp"
#include "Atmospheric/audio_subsystem.hpp"

// Binding convention (see bindings/README.md):
// These are all plain forwards to AudioSubsystem, so we bind the member
// function pointer directly via set_function(name, &Type::Method, instance)
// instead of hand-writing a lambda. sol2 introspects the member pointer, so
// the Lua signature auto-tracks the C++ one — if a method's parameters change
// the binding follows automatically, and if a method is renamed/removed this
// file fails to compile and points at the exact line. No silent drift.
void BindAudioAPI(sol::state& lua, AudioSubsystem* audio) {
    sol::table atmos = lua["atmos"];
    sol::table audioTable = atmos.create("audio");

    // ===== Sound API (short sound effects) =====
    audioTable.set_function("loadSound", &AudioSubsystem::LoadSound, audio);
    audioTable.set_function("unloadSound", &AudioSubsystem::UnloadSound, audio);
    audioTable.set_function("playSound", &AudioSubsystem::PlaySound, audio);
    audioTable.set_function("playSoundVariation", &AudioSubsystem::PlaySoundVariation, audio);
    audioTable.set_function("stopSound", &AudioSubsystem::StopSound, audio);
    audioTable.set_function("setSoundVolume", &AudioSubsystem::SetSoundVolume, audio);
    audioTable.set_function("isSoundPlaying", &AudioSubsystem::IsSoundPlaying, audio);

    // ===== Music API (streaming, for background music) =====
    audioTable.set_function("loadMusic", &AudioSubsystem::LoadMusic, audio);
    audioTable.set_function("unloadMusic", &AudioSubsystem::UnloadMusic, audio);
    audioTable.set_function("playMusic", &AudioSubsystem::PlayMusic, audio);
    audioTable.set_function("stopMusic", &AudioSubsystem::StopMusic, audio);
    audioTable.set_function("smoothStopMusic", &AudioSubsystem::SmoothStopMusic, audio);
    audioTable.set_function("setMusicVolume", &AudioSubsystem::SetMusicVolume, audio);
    audioTable.set_function("isMusicPlaying", &AudioSubsystem::IsMusicPlaying, audio);

    // ===== Global controls =====
    audioTable.set_function("stopAllSounds", &AudioSubsystem::StopAllSounds, audio);
    audioTable.set_function("stopAllMusic", &AudioSubsystem::StopAllMusics, audio);
    audioTable.set_function("stopAll", &AudioSubsystem::StopAll, audio);
}
