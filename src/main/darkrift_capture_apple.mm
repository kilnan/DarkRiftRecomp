// macOS helpers: window capture for the debug/test mode and window restoration settings.
#include <string>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>

#include "SDL.h"
#include "SDL_syswm.h"
#include "darkrift_debug.h"

extern SDL_Window* window;

bool darkrift::debug::capture_window(const std::string& path) {
    if (window == nullptr) {
        return false;
    }
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) {
        return false;
    }
    NSWindow* ns_window = info.info.cocoa.window;
    const CGWindowID window_id = (CGWindowID)[ns_window windowNumber];

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CGImageRef image = CGWindowListCreateImage(CGRectNull, kCGWindowListOptionIncludingWindow, window_id,
                                               kCGWindowImageBoundsIgnoreFraming | kCGWindowImageNominalResolution);
#pragma clang diagnostic pop
    if (image == nullptr) {
        return false;
    }

    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)url,
                                                                 CFSTR("public.png"), 1, nullptr);
    bool ok = false;
    if (dest != nullptr) {
        CGImageDestinationAddImage(dest, image, nullptr);
        ok = CGImageDestinationFinalize(dest);
        CFRelease(dest);
    }
    CGImageRelease(image);
    return ok;
}

void darkrift::debug::disable_window_restoration() {
    // After a crash AppKit asks whether to reopen the app's windows with a modal alert during launch.
    // There is nothing to restore, and the alert is invisible (and blocks forever) with --background.
    [[NSUserDefaults standardUserDefaults] registerDefaults:@{ @"ApplePersistenceIgnoreState": @YES }];
}
