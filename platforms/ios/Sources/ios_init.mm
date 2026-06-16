// Registers the Swift AppDelegate with SDL3 before UIApplicationMain runs.
//
// Objective-C's +load executes before main() — earlier than any C++
// static initialiser — so this is the correct place to set SDL3 hints
// that must be visible before SDL_RunApp / UIApplicationMain is called.
//
// SDL3 reads SDL_HINT_UIKIT_APP_DELEGATE_CLASS_NAME when it sets up
// UIApplicationMain and creates the delegate instance.  AtmosAppDelegate
// must be @objc-exported from Swift with that exact name.

#import <Foundation/Foundation.h>
#include <SDL3/SDL_hints.h>

@interface AtmosIOSInit : NSObject
@end

@implementation AtmosIOSInit
+ (void)load {
    SDL_SetHint(SDL_HINT_UIKIT_APP_DELEGATE_CLASS_NAME, "AtmosAppDelegate");
}
@end
