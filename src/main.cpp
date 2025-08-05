#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/SwapChain.h>
#include <utils/EntityManager.h>

#include "SDL3/SDL.h"

#ifdef __APPLE__
#    include "SDL3/SDL_metal.h"
#else
#    include <X11/Xlib.h>
#endif

#ifdef __APPLE__
constexpr bool is_apple = true;
#else
constexpr bool is_apple = false;
#endif

#include <chrono>
#include <optional>
#include <string>

#include <spdlog/spdlog.h>

#define DISABLE_MOVE_COPY(CNAME)                                               \
    CNAME(CNAME const&)            = delete;                                   \
    CNAME(CNAME&&)                 = delete;                                   \
    CNAME& operator=(CNAME const&) = delete;                                   \
    CNAME&(CNAME&&)                = delete;

void expect(bool condition, const char* message) {
    if (!condition) {
        spdlog::error("Condition failed: {}", message);
        abort();
    }
}

void* obtain_native_window(SDL_Window* window) {
#if __APPLE__
    auto view = SDL_Metal_CreateView(window);
    return view;
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

struct Config {
    std::string        title;
    std::optional<int> device = std::nullopt;

    int w = 1024;
    int h = 768;

    bool resizeable = false;
};

class LocalPlatform {
    SDL_Window* p;

public:
    DISABLE_MOVE_COPY(LocalPlatform);

    LocalPlatform(Config const& config) {
        expect(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO),
               "Unable to initialize SDL");

        uint32_t window_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (config.resizeable) { window_flags |= SDL_WINDOW_RESIZABLE; }

        p = SDL_CreateWindow(
            config.title.c_str(), config.w, config.h, window_flags);
    }

    ~LocalPlatform() {
        SDL_DestroyWindow(p);
        SDL_Quit();
    }
};

struct EngineDestroyer {
    void operator()(filament::Engine* p) { filament::Engine::destroy(p); }
};

template <class T>
struct EngineResourceWrapper {
    filament::Engine* pointer;

    EngineResourceWrapper(filament::Engine* p) : pointer(p) { }
    void operator()(T* p) { pointer->destroy(p); }
};

using WindowWrapper = std::unique_ptr<SDL_Window, WindowDestroyer>;
using EngineWrapper = std::unique_ptr<filament::Engine, EngineDestroyer>;

template <class T>
using ResourceWrapper = std::unique_ptr<T, EngineResourceWrapper<T>>;


WindowWrapper make_window() { }

EngineWrapper make_engine(SDL_Window*) {
    auto backend = is_apple ? filament::backend::Backend::METAL
                            : filament::backend::Backend::VULKAN;

    // Can use the engine config system to add in stereo

#ifdef FILAMENT_DRIVER_SUPPORTS_VULKAN
    // set GPU
    if (backend == filament::backend::Backend::VULKAN &&
        config.device.has_value()) {
        filament::backend::Platform::
    }
    VulkanPlatform::Customization::GPUPreference pref;
    // Check to see if it is an integer, if so turn it into an index.
    if (std::all_of(gpuHint.begin(), gpuHint.end(), ::isdigit)) {
        char* p_end {};
        pref.index =
            static_cast<int8_t>(std::strtol(gpuHint.c_str(), &p_end, 10));
    } else {
        pref.deviceName = gpuHint;
    }
    mCustomization = { .gpu = pref };

#endif

    return EngineWrapper(
        filament::Engine::Builder()
            .backend(backend)
            .featureLevel(filament::backend::FeatureLevel::FEATURE_LEVEL_3)
            .build());
}

auto make_renderer(void* native_window, filament::Engine* engine) {
    auto swap_flags = filament::SwapChain::CONFIG_HAS_STENCIL_BUFFER;

    auto swap_chain = engine->createSwapChain(native_window, swap_flags);

    return ResourceWrapper<filament::Renderer>(engine->createRenderer(),
                                               engine);
}


struct RenderWindow {
    WindowWrapper                       m_window;
    void*                               m_native_window;
    EngineWrapper                       m_engine;
    ResourceWrapper<filament::Renderer> m_renderer;

    utils::Entity     main_camera;
    filament::Camera* camera = nullptr;

    RenderWindow(Config const& config)
        : m_window(make_window(config)),
          m_native_window(obtain_native_window(m_window.get())),
          m_engine(make_engine(m_window.get())),
          m_renderer(make_renderer(m_native_window, m_engine.get())) {

        auto& em = utils::EntityManager::get();

        main_camera = em.create();

        camera = m_engine->createCamera(main_camera);

        camera->setExposure(16.0f, 1.0 / 125.0f, 100.0f);

        int32_t width, height;
        SDL_GetWindowSizeInPixels(m_window.get(), &width, &height);

        auto aspect_ratio = double(width) / height;

        camera->setProjection(
            45.0, aspect_ratio, 0.0625, 4096, filament::Camera::Fov::VERTICAL);

        camera->setScaling({ 1.0 / aspect_ratio, 1.0 });

        camera->lookAt({ 4, 0, -4 }, { 0, 0, -4 }, { 0, 1, 0 });
    }
};

struct State {
    RenderWindow m_window;

    State(Config const& config) : m_window(config) { }
};


int main() {

    auto config = Config {
        .title = "Test Window",
    };

    auto state = State(config);

    auto sdl_window = state.m_window.m_window.get();

    auto renderer = state.m_window.m_renderer.get();
    // auto swap_chain = renderer->get

    if (!SDL_GL_SetSwapInterval(-1)) { SDL_GL_SetSwapInterval(1); }

    bool closed = false;

    auto since_last_frame = std::chrono::high_resolution_clock::now();

    while (!closed) {
        if (!UTILS_HAS_THREADING) { state.m_window.m_engine->execute(); }

        // do animation here

        // process events

        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT: closed = true; break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    closed = true;
                }
                break;
            }
        }

        renderer->beginFrame()
    }
}
