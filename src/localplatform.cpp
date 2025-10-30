#include "localplatform.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_hints.h>

#ifdef __APPLE__
#    define NS_PRIVATE_IMPLEMENTATION
#    define CA_PRIVATE_IMPLEMENTATION
#    define MTL_PRIVATE_IMPLEMENTATION
#    include "vendor/metal-cpp/Metal.hpp"
#    include <SDL3/SDL_metal.h>
#else
#    include <X11/Xlib.h>
#endif


void* obtain_native_window(SDL_Window* window) {
#if __APPLE__
    auto view = SDL_Metal_CreateView(window);
    auto layer = SDL_Metal_GetLayer(view);

    auto* ptr = (CA::MetalLayer*)layer;
    // for some reason this doesnt seem to be working...
    ptr->setDisplaySyncEnabled(true);

    return layer;
#else
    expect(SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0,
           "Unable to get current video driver");

    Display* xdisplay =
        (Display*)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                         SDL_PROP_WINDOW_X11_DISPLAY_POINTER,
                                         NULL);
    Window xwindow = (Window)SDL_GetNumberProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    expect(xdisplay && xwindow, "Unable to obtain window handles.");

    return (void*)xwindow;
#endif
}


LocalPlatform::LocalPlatform(FConfig const& config) {
    spdlog::set_level(config.log_debug ? spdlog::level::debug
                                       : spdlog::level::info);

    SDL_SetHint(SDL_HINT_VIDEO_X11_XRANDR, "0");

    if (config.display.size()) { 
        spdlog::debug("Creating display at {}", config.display);
        setenv("DISPLAY", config.display.c_str(), 1); 
    }

    // SDL 3 uses true for success here
    expect(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO),
           "Unable to initialize SDL");

    uint32_t window_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;

    #ifndef  __APPLE__
        window_flags |= SDL_WINDOW_VULKAN;
    #endif

    m_window_pointer = SDL_CreateWindow(
        config.title.c_str(), config.w, config.h, window_flags);

    if (config.full_screen) {
        SDL_SetWindowFullscreen(m_window_pointer, true);
        // Wait for the window to actually be full screen
        SDL_SyncWindow(m_window_pointer);
    }

    m_native_window = obtain_native_window(m_window_pointer);
}

LocalPlatform::~LocalPlatform() {
    SDL_DestroyWindow(m_window_pointer);
    SDL_Quit();
}


std::array<uint32_t, 2> LocalPlatform::frame_size() const {
    int32_t width, height;
    SDL_GetWindowSizeInPixels(m_window_pointer, &width, &height);
    return {
        static_cast<unsigned int>(width),
        static_cast<unsigned int>(height),
    };
}
