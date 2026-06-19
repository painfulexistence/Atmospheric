import UIKit

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
        return super.application(application, didFinishLaunchingWithOptions: launchOptions)
    }

    override func applicationDidEnterBackground(_ application: UIApplication) {
        super.applicationDidEnterBackground(application)
    }

    override func applicationWillEnterForeground(_ application: UIApplication) {
        super.applicationWillEnterForeground(application)
    }
}
