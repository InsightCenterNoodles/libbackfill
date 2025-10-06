#include "backfill/api.h"

#include "config.h"
#include "session.h"
#include "utility.h"

#include <filament/LightManager.h>
#include <filament/MaterialInstance.h>
#include <filament/Texture.h>
#include <filament/View.h>
#include <image/LinearImage.h>
#include <imageio/ImageDecoder.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <cstring>

extern "C" {

static_assert(sizeof(short4) == 4 * sizeof(short));
static_assert(sizeof(ushort2) == 2 * sizeof(unsigned short));
static_assert(sizeof(ushort3) == 3 * sizeof(unsigned short));
static_assert(sizeof(uint3) == 3 * sizeof(unsigned int));
static_assert(sizeof(float2) == 2 * sizeof(float));
static_assert(sizeof(float3) == 3 * sizeof(float));
static_assert(sizeof(float4) == 4 * sizeof(float));
static_assert(sizeof(mat4) == 16 * sizeof(float));
static_assert(sizeof(aabb) == 2 * sizeof(float3));

// =============================================================================

void mat4_identity(mat4* mat) {
    auto& rmat = *mat;
    rmat.a     = { 1, 0, 0, 0 };
    rmat.b     = { 0, 1, 0, 0 };
    rmat.c     = { 0, 0, 1, 0 };
    rmat.d     = { 0, 0, 0, 1 };
}
void mat4_translate(mat4* ret, float3 pos) {
    auto& new_mat = *(filament::math::mat4f*)ret;

    new_mat = new_mat * filament::math::mat4f::translation(
                            filament::math::float3 { pos.x, pos.y, pos.z });
}

void mat4_from_array(mat4* out, const float m[16]) {
    memcpy(out, m, 16 * sizeof(float));
}

// =============================================================================

void pack_vertex_u16(FVertexPNU const* source,
                     uint32_t          vertex_count,
                     ushort3 const*    index,
                     uint32_t          index_count,
                     FPackedVertex*    dest) {

    static_assert(sizeof(FVertexPNU) == sizeof(Vertex));
    static_assert(sizeof(FPackedVertex) == sizeof(PackedVertex));

    auto new_source = (Vertex const*)source;
    auto new_index  = (filament::math::ushort3 const*)index;
    auto new_dest   = (PackedVertex*)dest;

    vert_compress(std::span { new_source, vertex_count },
                  std::span { new_index, index_count },
                  std::span { new_dest, vertex_count });
}

// =============================================================================

FBlob* fblob_init_copy(char const* data, u64 byte_count) {
    auto ptr =
        make_refcounted_unsafe<Bytes>(Bytes::from_copy({ data, byte_count }));

    return from_rc(ptr);
}
void fblob_acquire(FBlob* ptr) {
    as_rc(ptr)->retain();
}
void fblob_release(FBlob* ptr) {
    as_rc(ptr)->release();
}

FBlobRef fblobref_whole(FBlob* ptr) {
    return FBlobRef {
        .id     = ptr,
        .start  = 0,
        .length = UINT64_MAX.,
    };
}

// =============================================================================


FImage* fimg_init_exr(FBlobRef ref) {
    auto ptr = make_refcounted_unsafe<FImageContent>(ref);

    return from_rc(ptr);
}
void fimg_acquire(FImage* ptr) {
    as_rc(ptr)->retain();
}
void fimg_release(FImage* ptr) {
    as_rc(ptr)->release();
}

// =============================================================================

FTextureConfig* ftex_config_init(FImage* ptr, TextureFormat format) {
    if (!ptr) return nullptr;

    auto p = new FTextureConfig(as_rc(ptr));

    auto fmt = filament::Texture::InternalFormat::RGB8;

    switch (format) {
    case R11F_G11F_B10F:
        fmt = filament::Texture::InternalFormat::R11F_G11F_B10F;
        break;
    default: spdlog::warn("Unknown texture format {}!", (int)format);
    }

    spdlog::debug("ftex set format {}", (int)fmt);

    p->builder.format(fmt);

    return p;
}

void ftex_config_destroy(FTextureConfig* ptr) {
    delete ptr;
}

FTexture* ftex_init(FSession* ptr, FTextureConfig* cfg) {
    auto p = make_refcounted_unsafe<FTextureContent>(ptr, *cfg);

    return from_rc(p);
}
void ftex_acquire(FTexture* ptr) {
    as_rc(ptr)->retain();
}
void ftex_release(FTexture* ptr) {
    as_rc(ptr)->release();
}

// =============================================================================

FEnvironmentLight* fenv_light_init_equirect(FSession* ptr, FTexture* tex) {
    auto p = make_refcounted_unsafe<EnvLightContent>(ptr, as_rc(tex));

    return from_rc(p);
}

void fenv_light_acquire(FEnvironmentLight* p) {
    as_rc(p)->retain();
}

void fenv_light_release(FEnvironmentLight* ptr) {
    as_rc(ptr)->release();
}

// =============================================================================

FMesh* fmesh_init(FSession*      session,
                  FBlobRef       vertex_reference,
                  uint32_t       vertex_count,
                  FBlobRef       index_reference,
                  uint32_t       index_count,
                  FMeshIndexType type,
                  aabb           bounding_box) {

    auto ptr = make_refcounted_unsafe<FMeshContent>(session,
                                                    vertex_reference,
                                                    vertex_count,
                                                    index_reference,
                                                    index_count,
                                                    type,
                                                    bounding_box);

    return from_rc(ptr);
}
void fmesh_acquire(FMesh* ptr) {
    as_rc(ptr)->retain();
}
void fmesh_release(FMesh* ptr) {
    as_rc(ptr)->release();
}

// =============================================================================


FMaterial* fmaterial_init(FSession* session, FMaterialConfig* flags) {
    auto ptr = make_refcounted_unsafe<FMaterialContent>(
        session->engine(),
        session->new_instance_for_type(MaterialType::Lit),
        flags->instance_count);

    return from_rc(ptr);
}
void fmaterial_acquire(FMaterial* ptr) {
    as_rc(ptr)->retain();
}
void fmaterial_release(FMaterial* ptr) {
    as_rc(ptr)->release();
}

void fmaterial_set_base_color(FMaterial* ptr, FColor c) {
    as_rc(ptr)->item.set_color(c);
}
void fmaterial_set_roughness_metallic(FMaterial* ptr, float r, float m) {
    as_rc(ptr)->item.set_rm(r, m);
}
void fmaterial_set_instances(FMaterial* ptr, mat4 const* data, u64 count) {
    as_rc(ptr)->item.set_instances(data, count);
}

// =============================================================================

struct FLightConfig : filament::LightManager::Builder {
    using Builder::Builder;
};

FLightConfig* flightconfig_init(FLightType type) {

    filament::LightManager::Type new_type;

    switch (type) {
    case POINT: new_type = filament::LightManager::Type::POINT; break;
    case SPOT: new_type = filament::LightManager::Type::SPOT; break;
    case DIRECTIONAL:
        new_type = filament::LightManager::Type::DIRECTIONAL;
        break;
    case SUN: new_type = filament::LightManager::Type::SUN; break;
    }

    return new FLightConfig(new_type);
}
void flightconfig_destroy(FLightConfig* ptr) {
    delete ptr;
}

void flc_set_intensity(FLightConfig* ptr, float v) {
    ptr->intensity(v);
}
void flc_set_color(FLightConfig* ptr, FColor c) {
    ptr->color({ c.r, c.g, c.b });
}
void flc_set_falloff(FLightConfig* ptr, float v) {
    ptr->falloff(v);
}
void flc_set_direction(FLightConfig* ptr, float3 d) {
    ptr->direction({ d.x, d.y, d.z });
}
void flc_set_spot_cone(FLightConfig* ptr, float inner, float outer) {
    ptr->spotLightCone(inner, outer);
}
void flc_set_shadows(FLightConfig* ptr, uint8_t shadows) {
    ptr->castShadows(shadows);
}


// =============================================================================

FConfig* fconfig_init() {
    return new FConfig();
}
void fconfig_destroy(FConfig* ptr) {
    delete ptr;
}

void fconfig_set_title(FConfig* ptr, char const* text) {
    ptr->title = text;
}
void fconfig_set_display(FConfig* ptr, char const* text) {
    ptr->display = text;
}
void fconfig_set_device(FConfig* ptr, int device) {
    ptr->device = device;
}
void fconfig_set_screen(FConfig* ptr, int w, int h) {
    ptr->w = w;
    ptr->h = h;
}
void fconfig_set_log_debug(FConfig* ptr, uint8_t b) {
    ptr->log_debug = b;
}
void fconfig_set_offaxis_plane(FConfig* ptr, FScreenPlane const* plane) {
    ScreenDesc desc {
        .lower_left = {
            plane->lower_left[0],
            plane->lower_left[1],
            plane->lower_left[2],
        },
        .lower_right = {
            plane->lower_right[0],
            plane->lower_right[1],
            plane->lower_right[2],
        },
        .upper_right = {
            plane->upper_right[0],
            plane->upper_right[1],
            plane->upper_right[2],
        },
    };

    ptr->screen_info = desc;
}

// =============================================================================

FSession* fs_init(FConfig* ptr) {
    if (!ptr) return nullptr;

    return new FSession(*ptr);
}
void fs_destroy(FSession* ptr) {
    delete ptr;
}

void fs_set_postprocess(FSession* ptr, uint8_t opt) {
    ptr->view()->setPostProcessingEnabled(opt);
}

void fs_set_skybox_color(FSession* ptr, FColor color) {
    auto builder = filament::Skybox::Builder();
    builder.color({ color.r, color.g, color.b, color.a });

    auto sb = make_skybox(builder, ptr->engine());

    ptr->set_skybox(sb);
}

void fs_set_environment_light(FSession* ptr, FEnvironmentLight* light) {
    ptr->set_env_light(as_rc(light));
}

void fs_update_head(FSession* ptr, float3 pos, float4 quat) {
    ptr->update_head(pos, quat);
}

uint8_t fs_frame(FSession* ptr) {
    return ptr->run_frame();
}

i32 fs_new_entity(FSession* ptr) {
    auto e = ptr->new_entity();
    return utils::Entity::smuggle(e);
}

// This does destroy component content, but I feel skeptical...
void fs_destroy_entity(FSession* ptr, i32 id) {
    ptr->delete_entity(utils::Entity::import(id));
}

void fs_add_renderable(FSession* ptr, i32 entity, FMesh* mesh, FMaterial* mat) {
    auto e = utils::Entity::import(entity);

    ptr->add_renderable(e, as_rc(mesh), as_rc(mat));
}

void fs_del_renderable(FSession* ptr, i32 entity) {
    auto e = utils::Entity::import(entity);

    ptr->del_renderable(e);
}

void fs_set_transform(FSession* ptr, i32 entity, mat4 const* data) {
    auto e = utils::Entity::import(entity);

    ptr->add_transform(e, data);
}

void fs_set_parent(FSession* ptr, i32 child, i32 parent) {
    ptr->set_parent(utils::Entity::import(child),
                    utils::Entity::import(parent));
}

void fs_debug_camera(FSession* ptr, mat4* out_model, mat4* out_proj) {
    ptr->debug_camera(out_model, out_proj);
}

void fs_add_light(FSession* ptr, i32 entity, FLightConfig* config) {
    config->build(*(ptr->engine()), utils::Entity::import(entity));
}
void fs_del_light(FSession* ptr, i32 entity) {
    filament::Engine* engine = ptr->engine();

    engine->getLightManager().destroy(utils::Entity::import(entity));
}

// =============================================================================
}
