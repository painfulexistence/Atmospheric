package com.atmospheric.microvoxel

import android.os.Bundle
import org.libsdl.app.SDLActivity

class MainActivity : SDLActivity() {

    // SDL3 is linked statically into the game library, so this is the only
    // .so to load. The name must match the CMake target in
    // Examples/MicroVoxel/CMakeLists.txt — SDLActivity resolves SDL_main
    // inside libMicroVoxel.so. (Not "main": the Gradle build configures the
    // whole repo, and 3DBasics already owns that target name.)
    override fun getLibraries(): Array<String> {
        return arrayOf("MicroVoxel")
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
    }
}
