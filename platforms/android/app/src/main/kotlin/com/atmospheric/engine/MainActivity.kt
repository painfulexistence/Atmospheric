package com.atmospheric.engine

import android.os.Bundle
import org.libsdl.app.SDLActivity

// SDL3 provides SDLActivity which handles the JNI bridge, surface lifecycle,
// and input routing.  We only need to override what the engine requires on top.
class MainActivity : SDLActivity() {

    private lateinit var iapManager: IAPManager

    override fun getLibraries(): Array<String> = arrayOf("AtmosLua")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        iapManager = IAPManager(this)
        iapManager.connect()
    }

    override fun onDestroy() {
        iapManager.disconnect()
        super.onDestroy()
    }

    // Called from C++ via JNI to trigger a purchase flow.
    // The function name must match the JNI signature:
    //   Java_com_atmospheric_engine_MainActivity_nativePurchase
    @Suppress("unused")
    fun nativePurchase(productId: String) {
        iapManager.purchase(productId)
    }

    @Suppress("unused")
    fun nativeRestorePurchases() {
        iapManager.restore()
    }

    // Deliver purchase result back to the C++ engine.
    external fun nativeOnPurchaseComplete(productId: String)
    external fun nativeOnRestoreComplete()
    external fun nativeOnPurchaseError(productId: String, message: String)

    companion object {
        // Called from C++ (SDL3 NDK glue loads the library automatically).
        @JvmStatic
        external fun nativeInit(): Boolean
    }
}
