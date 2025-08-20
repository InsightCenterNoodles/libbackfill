#include "localplatform.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

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

    if (config.display.size()) { setenv("DISPLAY", config.display.c_str(), 1); }

    expect(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO),
           "Unable to initialize SDL");

    uint32_t window_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;

    m_window_pointer = SDL_CreateWindow(
        config.title.c_str(), config.w, config.h, window_flags);

    m_native_window = obtain_native_window(m_window_pointer);

    if (!SDL_GL_SetSwapInterval(-1)) { SDL_GL_SetSwapInterval(1); }

    int actual_interval = 0;
    SDL_GL_GetSwapInterval(&actual_interval);

    spdlog::info("Set GL swap interval: {}", actual_interval);
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
