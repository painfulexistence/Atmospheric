// Registers the Swift AppDelegate with SDL3 by overriding getAppDelegateClassName.
// SDL3 3.2+ removed SDL_HINT_UIKIT_APP_DELEGATE_CLASS_NAME; the correct approach
// is a category on SDLUIKitDelegate that returns the subclass name.
#import "SDL_uikitappdelegate.h"

@implementation SDLUIKitDelegate (AtmosAppDelegateOverride)
+ (NSString *)getAppDelegateClassName {
    return @"AtmosAppDelegate";
}
@end
