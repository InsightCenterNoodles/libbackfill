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
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>

#include <backend/BufferDescriptor.h>

#include <utils/EntityManager.h>

#include "config.h"
#include "generated.h"
#include "geometry.h"
#include "projection.h"
#include "renderstate.h"

#include "SDL3/SDL_events.h"


#include <chrono>


struct State {
    RenderState m_state;

    std::shared_ptr<LocalVertexBuffer> m_verts;
    std::shared_ptr<LocalIndexBuffer>  m_index;

    State(Config const& config) : m_state(config) { }

    void initial_content() {
        auto* engine = (filament::Engine*)m_state.engine();
        auto* scene  = m_state.scene();
        auto* view   = m_state.view();

        auto& transform = engine->getTransformManager();

        auto* skybox = filament::Skybox::Builder()
                           .color({ 0.1, 0.125, 0.25, 1.0 })
                           .build(*engine);
        scene->setSkybox(skybox);
        view->setPostProcessingEnabled(true);
        // view->setPostProcessingEnabled(false);

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
        mat_instance->setParameter("roughness", 0.3f);
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

        if (false) {
            utils::Entity sun = utils::EntityManager::get().create();

            filament::LightManager::Builder(filament::LightManager::Type::SUN)
                .intensity(100000.0f)
                .color({ 0, 1, 0 })
                .castShadows(true)
                .build(*engine, sun);
            scene->addEntity(sun);
        }

        constexpr float intensity = 1200000.0f;

        struct Spec {
            filament::math::float3 position;
            filament::math::float3 direction; // normalized
            filament::LinearColor  color;
            float                  intensity; // lumens
            float                  falloff;   // meters
            float                  innerDeg, outerDeg;
        } specs[] = {
            // -X (red): place at +X, point toward -X (origin)
            { { 10.0f, 0.0f, 0.0f },
              normalize(filament::math::float3 { -1, 0, 0 }),
              filament::LinearColor { 1, 0, 0 },
              intensity,
              30.0f,
              20.0f,
              25.0f },

            // -Y (green): place above, point downward
            { { 0.0f, 10.0f, 0.0f },
              normalize(filament::math::float3 { 0, -1, 0 }),
              filament::LinearColor { 0, 1, 0 },
              intensity,
              30.0f,
              20.0f,
              25.0f },

            // -Z (blue): place in front, point toward -Z (origin if your scene
            // is around (0,0,0))
            { { 0.0f, 0.0f, 10.0f },
              normalize(filament::math::float3 { 0, 0, -1 }),
              filament::LinearColor { 0, 0, 1 },
              intensity,
              30.0f,
              20.0f,
              25.0f },
        };

        constexpr auto DEG_TO_RAD = (M_PI / 180);

        auto& en = utils::EntityManager::get();

        auto& tm = engine->getTransformManager();
        auto& lm = engine->getLightManager();


        for (const auto& s : specs) {
            utils::Entity e = en.create();
            filament::LightManager::Builder(filament::LightManager::Type::SPOT)
                .intensity(s.intensity)
                .color(s.color)
                .falloff(s.falloff)
                .direction(s.direction)
                .spotLightCone(DEG_TO_RAD * (s.innerDeg),
                               DEG_TO_RAD * (s.outerDeg))
                .castShadows(true)
                .build(*engine, e);

            // place the light
            if (auto inst = tm.getInstance(e)) {
                tm.setTransform(inst,
                                filament::math::mat4f::translation(s.position));
            } else {
                tm.create(
                    e, {}, filament::math::mat4f::translation(s.position));
            }

            // (optional) set direction again post-build, in case you animate
            // later
            if (auto li = lm.getInstance(e)) {
                lm.setDirection(li, s.direction);
            }

            scene->addEntity(e);
        }
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

        float head_offset = 0;

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

            auto world_to_screen_matrix =
                proj::compute_world_to_screen_matrix();

            float near = 0.1;
            float far  = 1000;

            float new_head_x = std::sin(head_offset) * 2.0 - 1;
            head_offset += .00001;

            filament::math::float3 head_pos = { new_head_x, 1.5, 5 };
            filament::math::quatf  head_rot = { 1.0, 0.0, 0.0, 0.0 };


            { // new way

                auto H = filament::math::mat4f::translation(head_pos) *
                         filament::math::mat4f(head_rot);

                auto P = proj::compute_off_axis_projection(
                    world_to_screen_matrix,
                    head_pos,
                    filament::math::quat(head_rot),
                    true,
                    near,
                    far);


                auto V       = world_to_screen_matrix;
                auto V_prime = V * inverse(H);

                auto C = inverse(V_prime);


                m_state.camera()->setModelMatrix(C);
                m_state.camera()->setCustomProjection(P, near, far);


                // evaluate(model, VP);
            }

            // exit(0);


            if (renderer->beginFrame(swap_chain)) {
                renderer->render(m_state.view());
                renderer->endFrame();
            }
        }
    }
};


int main() {
    spdlog::set_level(spdlog::level::debug);

    proj::init(proj::ScreenDesc {
        .lower_left  = { -2.5, 0, -1.768 },
        .lower_right = { 2.5, 0, -1.768 },
        .upper_right = { 2.5, 2.5, -1.768 },
    });

    auto config = Config {
        .title = "Test Window",
    };

    auto state = State(config);

    state.initial_content();

    state.run();
}
