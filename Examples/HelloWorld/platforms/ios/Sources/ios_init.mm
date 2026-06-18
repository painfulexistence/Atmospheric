// Registers the Swift AppDelegate with SDL3 before UIApplicationMain runs.
#import <Foundation/Foundation.h>
#include <SDL3/SDL_hints.h>

@interface AtmosIOSInit : NSObject
@end

@implementation AtmosIOSInit
+ (void)load {
    SDL_SetHint(SDL_HINT_UIKIT_APP_DELEGATE_CLASS_NAME, "AtmosAppDelegate");
}
@end
