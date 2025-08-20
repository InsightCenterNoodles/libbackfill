#include "session.h"

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


template <>
struct fmt::formatter<filament::math::float4> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        return ctx.end();
    }

    template <typename FormatContext>
    auto format(filament::math::float4 const& input, FormatContext& ctx) const
        -> decltype(ctx.out()) {
        return format_to(
            ctx.out(), "({} {} {} {})", input.x, input.y, input.z, input.w);
    }
};

template <>
struct fmt::formatter<filament::math::mat4f> {
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin()) {
        return ctx.end();
    }

    template <typename FormatContext>
    auto format(filament::math::mat4f const& input, FormatContext& ctx) const
        -> decltype(ctx.out()) {
        return format_to(ctx.out(),
                         "(a={}, b={}, c={}, d={})",
                         input[0],
                         input[1],
                         input[2],
                         input[3]);
    }
};

// =============================================================================

std::shared_ptr<filament::Skybox> make_skybox(filament::Skybox::Builder builder,
                                              filament::Engine* engine) {
    return std::shared_ptr<filament::Skybox>(
        builder.build(*engine), [engine](auto x) { engine->destroy(x); });
}

using SkyboxPtr = std::shared_ptr<filament::Skybox>;

// =============================================================================

static inline IndexType translate_fmesh_index_type(FMeshIndexType type) {
    switch (type) {
    case U16: return IndexType::U16;
    case U32: return IndexType::U32;
    }
}

static filament::math::float3 translate_float3(float3 v) {
    return { v.x, v.y, v.z };
}


FMeshContent::FMeshContent(FSession*      session,
                           FBlobRef       vertex_reference,
                           uint32_t       vertex_count,
                           FBlobRef       index_reference,
                           uint32_t       index_count,
                           FMeshIndexType type,
                           aabb           bounding_box)
    : m_engine(session->engine()),
      m_verts(m_engine, _ref_to_bytes(vertex_reference), vertex_count),
      m_index(m_engine,
              _ref_to_bytes(index_reference),
              index_count,
              translate_fmesh_index_type(type)) {

    m_box.set(translate_float3(bounding_box.minimum),
              translate_float3(bounding_box.maximum));
}

// =============================================================================

FMaterialContent::FMaterialContent(filament::Engine*           engine,
                                   filament::MaterialInstance* instance,
                                   unsigned                    use_instances)
    : m_engine(engine),
      m_instance(instance),
      m_instance_count(use_instances) { }

FMaterialContent::~FMaterialContent() {
    m_engine->destroy(m_instance);
}

void FMaterialContent::set_color(FColor const& c) {
    m_instance->setParameter(
        "baseColor", filament::RgbaType::LINEAR, { c.r, c.g, c.b, c.a });
}

void FMaterialContent::set_rm(float r, float m) {
    m_instance->setParameter("roughness", r);
    m_instance->setParameter("metallic", m);
}

void FMaterialContent::set_instances(mat4* data, size_t count) {
    static_assert(sizeof(mat4) == sizeof(filament::math::mat4f));
    if (count > 1024) {
        spdlog::warn("Setting instance counts above 1024 could cause "
                     "unexpected behavior!");
    }
    m_instance->setParameter("inst_data", (filament::math::mat4f*)data, count);
}

// =============================================================================

FSession::FSession(FConfig const& config) : RenderState(config) {
    m_materials.push_back(
        filament::Material::Builder()
            .package(generated::get_primarylit_matbin().data(),
                     generated::get_primarylit_matbin().size())
            .build(*engine()));

    m_materials.push_back(
        filament::Material::Builder()
            .package(generated::get_primaryinstancelit_matbin().data(),
                     generated::get_primaryinstancelit_matbin().size())
            .build(*engine()));

    if (config.screen_info) {
        proj::init(*config.screen_info);

        m_use_offaxis = true;
    }


    spdlog::info("Created new session");
}

void FSession::set_skybox(SkyboxPtr ptr) {
    m_skybox = ptr;
    scene()->setSkybox(ptr.get());
}


filament::MaterialInstance* FSession::new_instance_for_type(MaterialType type) {
    return m_materials.at((size_t)type)->createInstance();
}

utils::Entity FSession::new_entity() {
    auto e = manager().create();

    this->scene()->addEntity(e);

    spdlog::debug("Adding entity {}", e.getId());

    return e;
}
void FSession::delete_entity(utils::Entity e) {
    manager().destroy(e);
    m_bound_render_resources.erase(utils::Entity::smuggle(e));
}

/*
void FSession::initial_content() {
    auto* engine = (filament::Engine*)this->engine();
    auto* scene  = this->scene();

    auto& transform = engine->getTransformManager();

    auto* mat = filament::Material::Builder()
                    .package(generated::get_primaryinstancelit_matbin().data(),
                             generated::get_primaryinstancelit_matbin().size())
                    .build(*engine);

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

    mat_instance->setParameter("inst_data", instances.data(), instances.size());

    auto renderable = manager().create();
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
            .spotLightCone(DEG_TO_RAD * (s.innerDeg), DEG_TO_RAD * (s.outerDeg))
            .castShadows(true)
            .build(*engine, e);

        // place the light
        if (auto inst = tm.getInstance(e)) {
            tm.setTransform(inst,
                            filament::math::mat4f::translation(s.position));
        } else {
            tm.create(e, {}, filament::math::mat4f::translation(s.position));
        }

        // (optional) set direction again post-build, in case you animate
        // later
        if (auto li = lm.getInstance(e)) { lm.setDirection(li, s.direction); }

        scene->addEntity(e);
    }
}

*/

void FSession::add_renderable(utils::Entity                            e,
                              std::shared_ptr<FMeshContent> const&     mesh,
                              std::shared_ptr<FMaterialContent> const& mat) {
    filament::RenderableManager::Builder b(1);

    b.boundingBox(mesh->box())
        .material(0, *mat)
        .geometry(0,
                  filament::RenderableManager::PrimitiveType::TRIANGLES,
                  mesh->verts().vertex_buffer(),
                  mesh->index().index_buffer(),
                  0,
                  mesh->index().index_count())
        .castShadows(true);

    if (mat->instance_count() > 0) { b.instances(mat->instance_count()); }

    m_bound_render_resources[utils::Entity::smuggle(e)] = {
        .material = mat,
        .mesh     = mesh,
    };

    b.build(*engine(), e);
}

void FSession::del_renderable(utils::Entity e) {
    filament::Engine* ptr = engine();

    auto& rm = ptr->getRenderableManager();

    rm.destroy(e);
}

void FSession::add_transform(utils::Entity e, mat4 const* tf) {
    filament::Engine* ptr = engine();

    auto& tm = ptr->getTransformManager();

    // lets cheat
    static_assert(sizeof(mat4) == sizeof(filament::math::mat4f));

    auto hack = (filament::math::mat4f*)tf;

    if (auto inst = tm.getInstance(e)) {
        tm.setTransform(inst, *hack);
    } else {
        tm.create(e, {}, *hack);
    }
}

void FSession::set_parent(utils::Entity child, utils::Entity parent) {
    filament::Engine* ptr = engine();

    auto& tm = ptr->getTransformManager();

    if (!tm.hasComponent(parent)) { tm.create(parent); }

    auto parent_instance = tm.getInstance(parent);

    tm.create(child, parent_instance);
}

bool FSession::run_frame() {
    // auto sdl_window = state.m_window.m_window.get();

    // auto renderer = state.m_window.m_renderer.get();
    //  auto swap_chain = renderer->get

    auto* engine     = (filament::Engine*)this->engine();
    auto* renderer   = this->renderer().renderer();
    auto* swap_chain = this->renderer().swap_chain();


    if (!UTILS_HAS_THREADING) { engine->execute(); }

    // do animation here

    // process events

    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT: return false;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.scancode == SDL_SCANCODE_ESCAPE) { return false; }
            break;
        }
    }


    if (m_use_offaxis) { // new way

        auto world_to_screen_matrix = proj::compute_world_to_screen_matrix();

        float near = 0.1;
        float far  = 1000;

        float new_head_x = std::sin(m_debug_head) * 2.0 - 1;
        m_debug_head += .00001;

        filament::math::float3 head_pos = { new_head_x, 1.5, 5 };
        filament::math::quatf  head_rot = { 1.0, 0.0, 0.0, 0.0 };

        auto use_offaxis = false;

        auto H = filament::math::mat4f::translation(head_pos);
        // filament::math::mat4f(head_rot);

        auto P =
            proj::compute_off_axis_projection(world_to_screen_matrix,
                                              head_pos,
                                              filament::math::quat(head_rot),
                                              true,
                                              near,
                                              far);


        auto V       = world_to_screen_matrix;
        auto V_prime = V * inverse(H);

        auto C = inverse(V_prime);


        // spdlog::debug("model: {}", filament::math::mat4f(C));
        // spdlog::debug("proj: {}", filament::math::mat4f(P));

        camera()->setModelMatrix(C);
        camera()->setCustomProjection(P, near, far);


        // evaluate(model, VP);
    }

    // exit(0);


    if (renderer->beginFrame(swap_chain)) {
        renderer->render(view());
        renderer->endFrame();
    }

    return true;
}
