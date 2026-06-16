// Objective-C symbols that the Swift platform shell needs to see.
//
// SDL3 uses SDLUIKitDelegate as the UIApplicationDelegate.  Setting
// SDL_HINT_UIKIT_APP_DELEGATE_CLASS_NAME tells SDL3 to instantiate our
// AtmosAppDelegate (a Swift subclass of SDLUIKitDelegate) instead.
// SDL3 must be available as a framework for this import to resolve.
#import <SDL3/SDL_uikit_appdelegate.h>

// C bridge — lets Swift call into C++ and vice-versa
#include "iap_bridge.h"
