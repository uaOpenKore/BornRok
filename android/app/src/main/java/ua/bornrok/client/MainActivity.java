package ua.bornrok.client;

import org.libsdl.app.SDLActivity;

// uaRO Android entry activity. SDLActivity handles the GL surface, input and the native lifecycle;
// it dlopen's the libraries below and calls their SDL_main (our main() renamed via SDL3/SDL_main.h
// on Android -- see src/main.cpp). "main" == libmain.so (the client; src/CMakeLists sets OUTPUT_NAME).
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        // SDL3 is linked STATICALLY into libmain.so (vcpkg arm64-android builds libSDL3.a), so there is
        // no libSDL3.so to dlopen -- loading "SDL3" here threw "couldn't find libSDL3.so" (S.). Load
        // only "main"; its static SDL provides SDL_main + everything SDLActivity needs.
        return new String[] {
            "main",
        };
    }
}
