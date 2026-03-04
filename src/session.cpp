#include "session.h"

#include <filament-iblprefilter/IBLPrefilterContext.h>
#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/IndirectLight.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/SwapChain.h>
#include <filament/Texture.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <image/LinearImage.h>
#include <imageio/ImageDecoder.h>
#include <math/mat4.h>
#include <math/vec4.h>
#include <unistd.h>
#include <utils/EntityManager.h>

#include <backend/BufferDescriptor.h>

#include <utils/EntityManager.h>

#include <fstream>

#include "config.h"
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
      m_verts(m_engine, ref_to_bytes(vertex_reference), vertex_count),
      m_index(m_engine,
              ref_to_bytes(index_reference),
              index_count,
              translate_fmesh_index_type(type)) {

    m_box.set(translate_float3(bounding_box.minimum),
              translate_float3(bounding_box.maximum));

    spdlog::debug("Creating mesh assets {}", (void*)this);
}

FMeshContent::~FMeshContent() {
    spdlog::debug("Destroying mesh assets {}", (void*)this);
}


// =============================================================================

EnvLightContent::EnvLightContent(FSession*                    ptr,
                                 RefCounted<FTextureContent>* texture)
    : m_texture(texture->borrow()), m_engine(ptr->engine()) {

    IBLPrefilterContext context(*m_engine);

    IBLPrefilterContext::EquirectangularToCubemap equirectangularToCubemap(
        context);
    IBLPrefilterContext::SpecularFilter   specularFilter(context);
    IBLPrefilterContext::IrradianceFilter irradianceFilter(context);

    m_skybox_texture = equirectangularToCubemap(m_texture->texture());
    m_specular       = specularFilter(m_skybox_texture);
    m_fog_texture    = irradianceFilter(
        {
               .generateMipmap = true,
        },
        m_skybox_texture);
    m_fog_texture->generateMipmaps(*m_engine);

    m_indirect_light = filament::IndirectLight::Builder()
                           .reflections(m_skybox_texture)
                           .intensity(30000.0f)
                           .build(*m_engine);

    expect(m_indirect_light, "Unable to build indirect light");

    m_skybox = filament::Skybox::Builder()
                   .environment(m_skybox_texture)
                   .showSun(true)
                   .build(*m_engine);

    spdlog::debug("Creating envlight {}", (void*)this);
}

EnvLightContent::~EnvLightContent() {
    m_engine->destroy(m_skybox);
    m_engine->destroy(m_indirect_light);

    m_engine->destroy(m_fog_texture);
    m_engine->destroy(m_specular);
    m_engine->destroy(m_skybox_texture);

    spdlog::debug("Destroying envlight {}", (void*)this);
}

void EnvLightContent::set_intensity(float f) {
    m_indirect_light->setIntensity(f);
}


// =============================================================================

FSession::FSession(FConfig const& config) : RenderState(config) {
    m_provider = filament::gltfio::createJitShaderProvider(engine(), true);

    m_offaxis_screen_info = config.screen_info;
    m_is_left             = config.left_eye;

    m_last = std::chrono::high_resolution_clock::now();

    spdlog::info("Created new session");
}

FSession::~FSession() {
    spdlog::debug("Closing session...");


    //  we are leaving this commented until we have a better shutdown
    //  otherwise we get a crash

    // delete m_provider;


    // for (auto* m : m_materials) {
    //        filament::Engine* engine = this->engine();
    //      engine->destroy(m);
    //}
}

void FSession::set_skybox(SkyboxPtr ptr) {
    // order of operations here... replace the skybox first so its always a
    // valid ref
    scene()->setSkybox(ptr.get());
    // now replace the handle, deleting the old one if it exists
    m_skybox = ptr;
}

void FSession::set_env_light(RefCounted<EnvLightContent>* env_light) {
    scene()->setIndirectLight(env_light->item.indirect_light());
    m_env_light = env_light->borrow();
}

void FSession::update_head(float3 pos, float4 quat) {
    m_head_pos = { pos.x, pos.y, pos.z };
    m_head_rot = { quat.w, quat.x, quat.y, quat.z };
}

filament::MaterialInstance*
FSession::new_instance_for_type(FMaterialConfigInternal const& internal) {

    auto key = internal.material_key;

    filament::gltfio::UvMap map {};

    auto* ret = m_provider->createMaterialInstance(&key, &map);

    // the key could be mutated. check.
    // spdlog::debug("Realized mat key:");
    //__builtin_dump_struct(&key, &printf);

    return ret;
}

utils::Entity FSession::new_entity() {
    auto e = manager().create();

    this->scene()->addEntity(e);

    spdlog::debug("Adding entity {}", e.getId());

    return e;
}
void FSession::delete_entity(utils::Entity e) {
    spdlog::debug("Delete entity {}", e.getId());
    del_renderable(e);
    scene()->remove(e);
    manager().destroy(e);
    // m_bound_render_resources.erase(utils::Entity::smuggle(e));
}

void FSession::add_renderable(utils::Entity                 e,
                              RefCounted<FMeshContent>*     mesh_ptr,
                              RefCounted<FMaterialContent>* mat_ptr) {
    spdlog::debug("Add renderable {} mesh {} mat {}",
                  e.getId(),
                  (void*)mesh_ptr,
                  (void*)mat_ptr);
    filament::RenderableManager::Builder b(1);

    auto& mat  = mat_ptr->item;
    auto& mesh = mesh_ptr->item;

    b.boundingBox(mesh.box())
        .material(0, mat)
        .geometry(0,
                  filament::RenderableManager::PrimitiveType::TRIANGLES,
                  mesh.verts().vertex_buffer(),
                  mesh.index().index_buffer(),
                  0,
                  mesh.index().index_count())
        .castShadows(true);

    m_bound_render_resources[utils::Entity::smuggle(e)] = {
        .material = mat_ptr->borrow(),
        .mesh     = mesh_ptr->borrow(),
    };

    b.build(*engine(), e);
}

void FSession::del_renderable(utils::Entity e) {
    spdlog::debug("Remove renderable {}", e.getId());
    filament::Engine* ptr = engine();

    auto& rm = ptr->getRenderableManager();

    rm.destroy(e);

    m_bound_render_resources.erase(utils::Entity::smuggle(e));
}

void FSession::add_transform(utils::Entity e, mat4 const* tf) {
    filament::Engine* ptr = engine();

    auto& tm = ptr->getTransformManager();

    // lets cheat
    static_assert(sizeof(mat4) == sizeof(filament::math::mat4f));

    auto hack = (filament::math::mat4f*)tf;

    if (auto inst = tm.getInstance(e); inst.isValid()) {
        tm.setTransform(inst, *hack);
    } else {
        tm.create(e, {}, *hack);
    }
}

void FSession::set_parent(utils::Entity child, utils::Entity parent) {
    spdlog::debug("Reparent child {} to {}", child.getId(), parent.getId());
    filament::Engine* ptr = engine();

    auto& tm = ptr->getTransformManager();

    if (!tm.hasComponent(parent)) { tm.create(parent); }

    if (!tm.hasComponent(child)) { tm.create(child); }

    auto parent_instance = tm.getInstance(parent);
    auto child_instance  = tm.getInstance(child);

    tm.setParent(child_instance, parent_instance);
}

void FSession::set_visible(utils::Entity entity, uint8_t value) {
    filament::Engine* ptr = engine();
    auto& rm = ptr->getRenderableManager();

    if (rm.hasComponent(entity)) {
        auto instance = rm.getInstance(entity);
        rm.setLayerMask(instance, 0xFF, value ? (1u << 0) : 0u);
    }
}

void FSession::debug_camera(mat4* out_model, mat4* out_proj) {
    auto* c = this->camera();

    auto om = filament::math::mat4f(c->getModelMatrix());
    auto op = filament::math::mat4f(c->getProjectionMatrix());

    *(filament::math::mat4f*)out_model = om;
    *(filament::math::mat4f*)out_proj  = op;
}

void FSession::debug_camera_obj(char const* file) {
    using namespace filament;

    auto* c = this->camera();

    bool depthZeroToOne = false;

    // Build transforms
    auto const M    = math::mat4f(c->getModelMatrix());      // camera -> world
    auto const P    = math::mat4f(c->getProjectionMatrix()); // projection
    auto const invP = inverse(P);                            // clip -> view

    // NDC cube corners (OpenGL style z = -1 (near) / +1 (far)).
    // If using Vulkan/DirectX depth, switch to z = 0 (near) / 1 (far).
    const float zn = depthZeroToOne ? 0.0f : -1.0f;
    const float zf = 1.0f;

    // 8 corners in NDC (x,y,z)
    const std::array<math::float3, 8> ndc = { {
        { -1.f, -1.f, zn },
        { +1.f, -1.f, zn },
        { +1.f, +1.f, zn },
        { -1.f, +1.f, zn }, // near  (0..3)
        { -1.f, -1.f, zf },
        { +1.f, -1.f, zf },
        { +1.f, +1.f, zf },
        { -1.f, +1.f, zf } // far   (4..7)
    } };

    // Unproject to world-space
    std::array<math::float3, 8> world;
    for (size_t i = 0; i < ndc.size(); ++i) {
        const math::float4 c { ndc[i].x, ndc[i].y, ndc[i].z, 1.0f }; // clip
        math::float4       v = invP * c; // view (homogeneous)
        if (v.w == 0.0f)
            throw std::runtime_error(
                "Invalid projection: w == 0 after inverse(P)*clip");
        v /= v.w;               // view (cartesian)
        math::float4 w = M * v; // world (homogeneous)
        if (w.w != 0.0f) w /= w.w;
        world[i] = math::float3 { w.x, w.y, w.z };
    }

    // Edge list (12 lines): near rectangle, far rectangle, 4 side edges
    constexpr int edges[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, // near
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }, // far
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }  // sides
    };

    // Write OBJ
    std::ofstream out(file);
    if (!out) {
        spdlog::critical("Failed to open OBJ path for writing: {}", file);
        return;
    }

    out << "# Frustum + Screen OBJ\n";

    // --- Frustum vertices ---
    out << "\n# Frustum vertices (8)\n";
    for (const auto& p : world)
        out << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';

    // --- Frustum lines ---
    out << "\ng frustum_lines\n";
    for (const auto& e : edges)
        out << "l " << (e[0] + 1) << ' ' << (e[1] + 1) << '\n';

    // --- Screen quad: either provided plane, or near face of frustum ---
    // Collect screen vertices (append after the first 8) in CCW order:


    std::array<math::float3, 4> screen;
    bool haveExplicitScreen = this->m_offaxis_screen_info.has_value();

    if (haveExplicitScreen) {
        auto const& screen_info = this->m_offaxis_screen_info.value();

        const math::float3 ll = screen_info.lower_left;
        const math::float3 lr = screen_info.lower_right;
        const math::float3 ur = screen_info.upper_right;
        const math::float3 ul = ll + (ur - lr); // complete the quad
        screen                = { ll, lr, ur, ul };
    } else {
        // Use frustum near face as "screen" (0..3 are near: LL, LR, UR, UL)
        screen = { world[0], world[1], world[2], world[3] };
    }

    // Append screen vertices
    const int baseIndex = 8; // after frustum verts
    out << "\n# Screen quad vertices (4)\n";
    for (const auto& p : screen)
        out << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';

    // Lines around the screen quad (wire outline)
    out << "\ng screen_outline\n";
    out << "l " << baseIndex + 1 << ' ' << baseIndex + 2 << '\n';
    out << "l " << baseIndex + 2 << ' ' << baseIndex + 3 << '\n';
    out << "l " << baseIndex + 3 << ' ' << baseIndex + 4 << '\n';
    out << "l " << baseIndex + 4 << ' ' << baseIndex + 1 << '\n';

    // Filled face for the screen (so it's visible as a surface in Blender)
    out << "\ng screen_face\n";
    out << "f " << baseIndex + 1 << ' ' << baseIndex + 2 << ' ' << baseIndex + 3
        << ' ' << baseIndex + 4 << '\n';


    out.flush();
    if (!out) {
        spdlog::critical("Failed while writing OBJ file: {}", file);
        return;
    }
}

static_assert(UTILS_HAS_THREADING);

bool FSession::run_frame() {

    auto now = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration<double>(now-m_last).count();

    if (duration < 1/60.) {
        //spdlog::debug("{} OVERSPEED {}", getpid(), duration*1000);
    }

    m_last = now;

    // process events

    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT: return false;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.scancode == SDL_SCANCODE_ESCAPE) { return false; }
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
            spdlog::debug("{} Screen resize!", getpid());
            this->renderer().rebuild_swapchain(this->platform());
            break;
        }
    }


    if (m_offaxis_screen_info) {

        auto const& screen_info = *m_offaxis_screen_info;

        float near = 0.1f;
        float far  = 1024.0f;

        proj::compute_off_axis_projection(screen_info,
                                          m_head_pos,
                                          filament::math::quat(m_head_rot),
                                          m_is_left,
                                          near,
                                          far,
                                          camera());
    } else {

        auto head_pos = m_head_pos;
        auto head_rot = filament::math::quat(m_head_rot);

        auto                   dir = filament::math::float3 { 0, 0, -1 };
        filament::math::float3 dir_roted;

        {
            filament::math::float3 u(head_rot.x, head_rot.y, head_rot.z);

            float s = head_rot.w;

            dir_roted = 2.0f * dot(u, dir) * u + (s * s - dot(u, u)) * dir +
                        2.0f * s * cross(u, dir);
        }

        dir_roted += head_pos;


        camera()->lookAt(head_pos, dir_roted, { 0, 1, 0 });
    }

    auto* renderer   = this->renderer().renderer();
    auto* swap_chain = this->renderer().swap_chain();

    static int delay = 17;

    // if (delay > 0) {
    //     SDL_Delay(delay);
    // }

    // this->engine()->flushAndWait();

    if (renderer->beginFrame(swap_chain)) {
        renderer->render(view());
        renderer->endFrame();
        //spdlog::debug("{} Draw frame! {} {}", getpid(), m_frame_skip_count, delay);
        // m_frame_skip_count = 0;
        // delay = std::clamp(delay - 1, 17, 100);
    } else {
        //spdlog::debug("{} Skipping frame! {} {}", getpid(), m_frame_skip_count, delay);

        // if (m_frame_skip_count ==0) {
        //     spdlog::debug("Recover...");
        //     this->renderer().rebuild_swapchain(this->platform());
        // }

        //m_frame_skip_count++;
        //delay += 1;
        
        // forcing anyway gives a lockup

        //renderer->render(view());
        //renderer->endFrame();
        
    }

    // this->engine()->flushAndWait();

    return true;
}
