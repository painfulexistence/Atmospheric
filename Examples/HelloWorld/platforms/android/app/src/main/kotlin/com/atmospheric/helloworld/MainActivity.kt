package com.atmospheric.helloworld

import android.os.Bundle
import org.libsdl.app.SDLActivity

class MainActivity : SDLActivity() {

    override fun getLibraries(): Array<String> {
        return arrayOf("HelloWorld")
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
    }
}
