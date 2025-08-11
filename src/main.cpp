#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/SwapChain.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>

#include <backend/BufferDescriptor.h>

#include <utils/EntityManager.h>

#include "generated.h"
#include "geometry.h"
#include "utility.h"

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

void* obtain_native_window(SDL_Window* window) {
#if __APPLE__
    auto view = SDL_Metal_CreateView(window);
    return SDL_Metal_GetLayer(view);
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
    SDL_Window* m_window_pointer;
    void*       m_native_window;

public:
    DISABLE_MOVE_COPY(LocalPlatform);

    LocalPlatform(Config const& config) {
        expect(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO),
               "Unable to initialize SDL");

        uint32_t window_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (config.resizeable) { window_flags |= SDL_WINDOW_RESIZABLE; }

        m_window_pointer = SDL_CreateWindow(
            config.title.c_str(), config.w, config.h, window_flags);

        m_native_window = obtain_native_window(m_window_pointer);

        if (!SDL_GL_SetSwapInterval(-1)) { SDL_GL_SetSwapInterval(1); }
    }

    ~LocalPlatform() {
        SDL_DestroyWindow(m_window_pointer);
        SDL_Quit();
    }

    void* native_window() const { return m_native_window; }

    std::array<uint32_t, 2> frame_size() const {
        int32_t width, height;
        SDL_GetWindowSizeInPixels(m_window_pointer, &width, &height);
        return {
            static_cast<unsigned int>(width),
            static_cast<unsigned int>(height),
        };
    }
};

class LocalEngine {
    filament::Engine* m_pointer;

public:
    DISABLE_MOVE_COPY(LocalEngine);

    LocalEngine() {
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
        m_pointer =
            filament::Engine::Builder()
                .backend(backend)
                .featureLevel(filament::backend::FeatureLevel::FEATURE_LEVEL_3)
                .build();
        expect(!!m_pointer, "Unable to initialize engine");
    }

    ~LocalEngine() { filament::Engine::destroy(m_pointer); }

    filament::Engine* operator->() { return m_pointer; }

    operator filament::Engine*() const { return m_pointer; }
};

class LocalRenderer {
    filament::Engine*    m_engine;
    filament::SwapChain* m_swap_chain;
    filament::Renderer*  m_renderer;

public:
    DISABLE_MOVE_COPY(LocalRenderer);

    LocalRenderer(LocalEngine const& le, LocalPlatform const& lp) {
        m_engine = le;

        auto swap_flags = filament::SwapChain::CONFIG_HAS_STENCIL_BUFFER;

        m_swap_chain =
            m_engine->createSwapChain(lp.native_window(), swap_flags);

        m_renderer = m_engine->createRenderer();
    }

    ~LocalRenderer() {
        m_engine->destroy(m_renderer);
        m_engine->destroy(m_swap_chain);
    }

    filament::SwapChain* swap_chain() const { return m_swap_chain; }
    filament::Renderer*  renderer() const { return m_renderer; }
};


class RenderState {
    LocalPlatform         m_platform;
    LocalEngine           m_engine;
    LocalRenderer         m_renderer;
    utils::EntityManager& m_manager;

    filament::Scene* m_scene = nullptr;

    utils::Entity     m_main_camera;
    filament::Camera* m_camera = nullptr;

    filament::View* m_view = nullptr;

public:
    DISABLE_MOVE_COPY(RenderState);

    RenderState(Config const& config)
        : m_platform(config),
          m_engine(),
          m_renderer(m_engine, m_platform),
          m_manager(utils::EntityManager::get()) {

        m_scene = m_engine->createScene();

        m_main_camera = m_manager.create();

        m_camera = m_engine->createCamera(m_main_camera);

        m_camera->setExposure(16.0f, 1.0 / 125.0f, 100.0f);

        auto [width, height] = m_platform.frame_size();

        auto aspect_ratio = double(width) / height;

        m_camera->setProjection(
            45.0, aspect_ratio, 0.0625, 4096, filament::Camera::Fov::VERTICAL);

        // m_camera->setScaling({ 1.0 / aspect_ratio, 1.0 });

        m_camera->lookAt({ 10, 0, -10 }, { 0, 0, 0 }, { 0, 1, 0 });

        m_view = m_engine->createView();
        m_view->setViewport({ 0, 0, width, height });

        m_view->setScene(m_scene);
        m_view->setCamera(m_camera);
    }

    ~RenderState() {
        m_engine->destroy(m_view);
        m_engine->destroyCameraComponent(m_main_camera);
        m_engine->destroy(m_scene);
    }

    LocalPlatform const&  platform() { return m_platform; };
    LocalEngine const&    engine() { return m_engine; };
    LocalRenderer const&  renderer() { return m_renderer; };
    utils::EntityManager& manager() { return m_manager; };
    filament::Scene*      scene() { return m_scene; }
    filament::View*       view() { return m_view; }
};

struct State {
    RenderState m_state;

    std::shared_ptr<LocalVertexBuffer> m_verts;
    std::shared_ptr<LocalIndexBuffer>  m_index;

    State(Config const& config) : m_state(config) { }

    void initial_content() {
        auto* engine = (filament::Engine*)m_state.engine();
        auto* scene  = m_state.scene();
        auto* view   = m_state.view();

        auto* skybox = filament::Skybox::Builder()
                           .color({ 0.1, 0.125, 0.25, 1.0 })
                           .build(*engine);
        scene->setSkybox(skybox);
        view->setPostProcessingEnabled(false);

        m_verts = std::make_shared<LocalVertexBuffer>(engine, sphere_verts());
        m_index = std::make_shared<LocalIndexBuffer>(engine, sphere_index());

        auto* mat =
            filament::Material::Builder()
                .package(generated::get_primaryinstancelit_matbin().data(),
                         generated::get_primaryinstancelit_matbin().size())
                .build(*engine);

        // std::vector<filament::Material::ParameterInfo> mat_info(128);

        // mat_info.reserve(mat->getParameters(mat_info.data(),
        // mat_info.size()));

        // for (auto const& param : mat_info) {
        //     spdlog::debug("Paramter {} : {} {}",
        //                   param.name,
        //                   (int)param.type,
        //                   param.count);
        // }

        auto* mat_instance = mat->getDefaultInstance();
        mat_instance->setParameter(
            "baseColor", filament::RgbaType::LINEAR, { 1.0, 1.0, 1.0, 1.0 });
        mat_instance->setParameter("roughness", 0.5f);
        mat_instance->setParameter("metallic", 1.0f);


        std::vector<filament::math::mat4f> instances;

        auto random_num = []() {
            return ((float)rand() / float(RAND_MAX)) * 2.0 - 1.0;
        };

        for (auto i = 0; i < 128; i++) {
            auto at = filament::math::float4 {
                random_num() * 5, random_num() * 5, random_num() * 5, 1.0
            };
            instances.emplace_back(filament::math::mat4f {
                at,
                filament::math::float4 { 0, 0, 0, 1 },
                filament::math::float4 { 0.5, 0.5, 0.5, 0 },
                filament::math::float4 {},
            });
        }

        mat_instance->setParameter(
            "inst_data", instances.data(), instances.size());

        auto renderable = m_state.manager().create();
        scene->addEntity(renderable);

        auto& rcm = engine->getRenderableManager();

        filament::RenderableManager::Builder(1)
            .boundingBox({ { -100, -100, -100 }, { 100, 100, 100 } })
            .instances(instances.size())
            .material(0, mat_instance)
            .geometry(0,
                      filament::RenderableManager::PrimitiveType::TRIANGLES,
                      m_verts->vertex_buffer(),
                      m_index->index_buffer(),
                      0,
                      m_index->index_count())
            .castShadows(true)
            .build(*engine, renderable);

        utils::Entity sun = utils::EntityManager::get().create();
        filament::LightManager::Builder(filament::LightManager::Type::SUN)
            .intensity(100000.0f)
            .castShadows(true)
            //.direction({ -0.5f, -1.0f, -0.3f })
            .build(*engine, sun);
        scene->addEntity(sun);
    }

    void run() {
        // auto sdl_window = state.m_window.m_window.get();

        // auto renderer = state.m_window.m_renderer.get();
        //  auto swap_chain = renderer->get

        auto* engine     = (filament::Engine*)m_state.engine();
        auto* renderer   = m_state.renderer().renderer();
        auto* swap_chain = m_state.renderer().swap_chain();


        bool closed = false;

        auto since_last_frame = std::chrono::high_resolution_clock::now();

        while (!closed) {
            if (!UTILS_HAS_THREADING) { engine->execute(); }

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

            if (renderer->beginFrame(swap_chain)) {
                renderer->render(m_state.view());
                renderer->endFrame();
            }
        }
    }
};


int main() {

    spdlog::set_level(spdlog::level::debug);

    auto config = Config {
        .title = "Test Window",
    };

    auto state = State(config);

    state.initial_content();

    state.run();
}
