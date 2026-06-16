import UIKit
import StoreKit

// SDL3 instantiates this class as the UIApplicationDelegate because
// ios_init.mm registers its name via SDL_HINT_UIKIT_APP_DELEGATE_CLASS_NAME.
// Subclassing SDLUIKitDelegate means SDL3's own lifecycle handling
// (GL context management, display link, orientation) is preserved.
@objc(AtmosAppDelegate)
final class AtmosAppDelegate: SDLUIKitDelegate {

    override func application(
        _ application: UIApplication,
        didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
    ) -> Bool {
        // Start StoreKit 2 transaction listener before the game loop starts
        // so we never miss a transaction delivered while the app was closed.
        Task { await IAPManager.shared.listenForTransactions() }

        return super.application(application, didFinishLaunchingWithOptions: launchOptions)
    }

    // SDL3 handles OpenGL context preservation on background/foreground via
    // SDL_EVENT_DID_ENTER_BACKGROUND / SDL_EVENT_WILL_ENTER_FOREGROUND,
    // so we only need to handle platform-specific extras here.

    override func applicationDidEnterBackground(_ application: UIApplication) {
        super.applicationDidEnterBackground(application)
    }

    override func applicationWillEnterForeground(_ application: UIApplication) {
        super.applicationWillEnterForeground(application)
        // Refresh entitlements when the user returns (e.g. after completing
        // a subscription purchase started outside the app).
        Task { try? await AppStore.sync() }
    }
}
