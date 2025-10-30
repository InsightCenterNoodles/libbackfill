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
#include <unistd.h>
#include <utils/EntityManager.h>

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

FMaterialContent::FMaterialContent(filament::Engine*           engine,
                                   filament::MaterialInstance* instance,
                                   unsigned                    use_instances)
    : m_engine(engine), m_instance(instance), m_instance_count(use_instances) {
    spdlog::debug("new material: {}", (void*)m_instance);
}

FMaterialContent::~FMaterialContent() {
    spdlog::debug("Destroying material: {}", (void*)m_instance);
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

void FMaterialContent::set_instances(mat4 const* data, size_t count) {
    static_assert(sizeof(mat4) == sizeof(filament::math::mat4f));
    if (count > 1024) {
        spdlog::warn("Setting instance counts above 1024 could cause "
                     "unexpected behavior!");
    }
    m_instance->setParameter("inst_data", (filament::math::mat4f*)data, count);
}

// =============================================================================

struct membuf : std::basic_streambuf<char> {
    membuf(const char* data, std::size_t size) {
        // Do NOT allow writes; keep it read-only.
        auto* p = const_cast<char*>(data); // safe as long as we never write
        setg(p, p, p + size);              // [eback, gptr, egptr]
    }

protected:
    pos_type seekoff(off_type                off,
                     std::ios_base::seekdir  dir,
                     std::ios_base::openmode which) override {
        if (!(which & std::ios_base::in)) return pos_type(off_type(-1));

        char* base = eback();
        char* curr = gptr();
        char* end  = egptr();

        char* target = nullptr;
        switch (dir) {
        case std::ios_base::beg: target = base + off; break;
        case std::ios_base::cur: target = curr + off; break;
        case std::ios_base::end: target = end + off; break;
        default: return pos_type(off_type(-1));
        }
        if (target < base || target > end) return pos_type(off_type(-1));
        setg(base, target, end);
        return pos_type(target - base);
    }

    pos_type seekpos(pos_type sp, std::ios_base::openmode which) override {
        return seekoff(off_type(sp), std::ios_base::beg, which);
    }
};


FImageContent::FImageContent(FBlobRef ref) {
    auto b = ref_to_bytes(ref);

    auto sbuf = membuf(b.data(), b.size());

    auto stream = std::istream(&sbuf);

    auto lin_image = image::ImageDecoder::decode(stream, "In memory stream");

    if (!lin_image.isValid()) { throw std::invalid_argument("Invalid image"); }

    auto w = lin_image.getWidth();
    auto h = lin_image.getHeight();
    auto n = lin_image.getChannels();

    m_description = {
        .width      = w,
        .height     = h,
        .n_channels = n,
        .size       = w * h * n * sizeof(float),
    };

    spdlog::info("Found image {} {} {} {} bytes", w, h, n, m_description.size);

    // documentation says image data under the hood is refcounted??
    m_pending = std::make_unique<image::LinearImage>(lin_image);

    auto* ptr = m_pending->getPixelRef();

    spdlog::debug("Data {} {} {} {}", ptr[0], ptr[1], ptr[2], ptr[3]);

    spdlog::debug("Creating image {} from blob {}", (void*)this, (void*)ref.id);
}

FImageContent::~FImageContent() {
    spdlog::debug("Destroying image {}", (void*)this);
}

// =============================================================================

FTextureConfig::FTextureConfig(RefCounted<FImageContent>* ptr)
    : image(ptr->borrow()) {
    auto const& desc = image->description();
    builder.width(desc.width)
        .height(desc.height)
        .levels(0xff) // will be automatically clamped
        .sampler(filament::Texture::Sampler::SAMPLER_2D)
        .usage(filament::Texture::Usage::DEFAULT);
}


void FTextureContent::completion(void* buffer, size_t, void* user) {
    ((FTextureContent*)user)->m_image = {};
}

FTextureContent::FTextureContent(FSession* session, FTextureConfig& config)
    : m_image(config.image), m_engine(session->engine()) {
    m_texture = config.builder.build(*m_engine);

    auto const& desc = m_image->description();

    // Transfer to GPU. The PBD only references the data, thus it must stay
    // alive, while uploading. There is an internal gpu buffer id it holds.
    auto buffer =
        filament::Texture::PixelBufferDescriptor(m_image->image().getPixelRef(),
                                                 desc.size,
                                                 filament::Texture::Format::RGB,
                                                 filament::Texture::Type::FLOAT,
                                                 completion,
                                                 this);

    m_texture->setImage(*m_engine, 0, std::move(buffer));

    spdlog::debug("Creating texture {}", (void*)this);
}

FTextureContent::~FTextureContent() {
    m_engine->destroy(m_texture);

    spdlog::debug("Destroying texture {}", (void*)this);
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

    m_offaxis_screen_info = config.screen_info;
    m_is_left             = config.left_eye;

    spdlog::info("Created new session");
}

FSession::~FSession() {
    spdlog::debug("Closing session...");
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

filament::MaterialInstance* FSession::new_instance_for_type(MaterialType type) {
    spdlog::debug("Creating new instance for material type {}", (int)type);
    return m_materials.at((size_t)type)->createInstance();
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

    if (mat.instance_count() > 0) { b.instances(mat.instance_count()); }

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

void FSession::debug_camera(mat4* out_model, mat4* out_proj) {
    auto* c = this->camera();

    auto om = filament::math::mat4f(c->getModelMatrix());
    auto op = filament::math::mat4f(c->getProjectionMatrix());

    *(filament::math::mat4f*)out_model = om;
    *(filament::math::mat4f*)out_proj  = op;
}

static_assert(UTILS_HAS_THREADING);

bool FSession::run_frame() {

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
    }

    auto* renderer   = this->renderer().renderer();
    auto* swap_chain = this->renderer().swap_chain();

    static int delay = 17;

    if (delay > 0) {
        SDL_Delay(delay);
    }

    if (renderer->beginFrame(swap_chain)) {
        renderer->render(view());
        renderer->endFrame();
        spdlog::debug("{} Draw frame! {} {}", getpid(), m_frame_skip_count, delay);
        m_frame_skip_count = 0;
        delay = std::clamp(delay - 1, 0, 100);
    } else {
        spdlog::debug("{} Skipping frame! {} {}", getpid(), m_frame_skip_count, delay);

        // if (m_frame_skip_count ==0) {
        //     spdlog::debug("Recover...");
        //     this->renderer().rebuild_swapchain(this->platform());
        // }

        m_frame_skip_count++;
        delay += 1;
        
        // forcing anyway gives a lockup

        //renderer->render(view());
        //renderer->endFrame();
        
    }

    return true;
}
