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

static_assert(UTILS_HAS_THREADING);

bool FSession::run_frame() {

    auto* renderer   = this->renderer().renderer();
    auto* swap_chain = this->renderer().swap_chain();

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
        m_debug_head += .01;

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
