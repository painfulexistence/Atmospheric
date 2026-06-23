import UIKit

// SDL3 instantiates this class as the UIApplicationDelegate because
// ios_init.mm registers its name via the SDLUIKitDelegate category override.
// Subclassing SDLUIKitDelegate preserves SDL3's lifecycle handling
// (GL context management, display link, orientation).
//
// IMPORTANT: do NOT override applicationDidEnterBackground /
// applicationWillEnterForeground here. SDLUIKitDelegate does not implement
// those selectors itself — SDL listens to UIApplicationDidEnterBackgroundNotification
// instead — so calling `super` raises an ObjC exception and crashes the app.
@objc(AtmosAppDelegate)
final class AtmosAppDelegate: SDLUIKitDelegate {
}
