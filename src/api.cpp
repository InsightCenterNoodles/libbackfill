#include "backfill/api.h"

#include "config.h"
#include "material.h"
#include "session.h"
#include "utility.h"

#include "magic_enum/magic_enum.hpp"

#include <filament/LightManager.h>
#include <filament/MaterialInstance.h>
#include <filament/Texture.h>
#include <filament/View.h>
#include <image/LinearImage.h>
#include <imageio/ImageDecoder.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <stb_image.h>

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

void pack_vertex_u32(FVertexPNU const* source,
                     uint32_t          vertex_count,
                     uint3 const*      index,
                     uint32_t          index_count,
                     FPackedVertex*    dest) {

    static_assert(sizeof(FVertexPNU) == sizeof(Vertex));
    static_assert(sizeof(FPackedVertex) == sizeof(PackedVertex));

    auto new_source = (Vertex const*)source;
    auto new_index  = (filament::math::uint3 const*)index;
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
        .length = UINT64_MAX,
    };
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

void fenv_set_intensity(FEnvironmentLight* ptr, float intensity) {
    as_rc(ptr)->item.set_intensity(intensity);
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

static_assert(sizeof(Sampler) == sizeof(filament::backend::SamplerParams));

void fsamp_init(Sampler* sampler) {
    *sampler = Sampler { .pack = 0 };
}

inline filament::backend::SamplerParams* as_sp(Sampler* ptr) {
    return (filament::backend::SamplerParams*)ptr;
}

void fsamp_set_mag(Sampler* sampler, FMagFilter f) {
    switch (f) {
    case MAG_FILTER_NEAREST:
        as_sp(sampler)->filterMag =
            filament::backend::SamplerMagFilter::NEAREST;
        break;
    case MAG_FILTER_LINEAR:
        as_sp(sampler)->filterMag = filament::backend::SamplerMagFilter::LINEAR;
        break;
    }
}
void fsamp_set_min(Sampler* sampler, FMinFilter f) {
    switch (f) {
    case MIN_FILTER_NEAREST:
        as_sp(sampler)->filterMin =
            filament::backend::SamplerMinFilter::NEAREST;
        break;
    case MIN_FILTER_LINEAR:
        as_sp(sampler)->filterMin = filament::backend::SamplerMinFilter::LINEAR;
        break;
    case MIN_FILTER_LINEAR_MIPMAP_LINEAR:
        as_sp(sampler)->filterMin =
            filament::backend::SamplerMinFilter::LINEAR_MIPMAP_LINEAR;
        break;
    }
}
void fsamp_set_wrap(Sampler* sampler, FWrapMode mode, FTexAxis axis) {
    filament::backend::SamplerWrapMode b_mode;

    switch (mode) {
    case WRAP_CLAMP:
        b_mode = filament::backend::SamplerWrapMode::CLAMP_TO_EDGE;
        break;
    case WRAP_REPEAT:
        b_mode = filament::backend::SamplerWrapMode::REPEAT;
        break;
    case WRAP_MIRROR_REPEAT:
        b_mode = filament::backend::SamplerWrapMode::MIRRORED_REPEAT;
        break;
    default: return;
    }

    switch (axis) {
    case AXIS_U: as_sp(sampler)->wrapS = b_mode; break;
    case AXIS_V: as_sp(sampler)->wrapT = b_mode; break;
    case AXIS_W: as_sp(sampler)->wrapR = b_mode; break;
    default: break;
    }
}
void fsamp_set_aniso(Sampler* sampler, uint8_t level) {
    as_sp(sampler)->anisotropyLog2 = level;
}

FMaterialConfig* fmaterialconfig_init() {
    auto ptr = make_refcounted_unsafe<FMaterialConfigInternal>();
    return from_rc(ptr);
}

void fmaterialconfig_destroy(FMaterialConfig* ptr) {
    as_rc(ptr)->release();
}

void fmc_set_option(FMaterialConfig* ptr, FMatOption tex, uint8_t value) {
    as_rc(ptr)->item.set_option(tex, value);
}
void fmc_set_texture(FMaterialConfig* ptr,
                     FMatTexSemantic  tex_semantic,
                     FMatTexUVSlot    slot,
                     FTexture*        tex,
                     Sampler*         sampler) {
    as_rc(ptr)->item.set_texture(tex_semantic, slot, tex, sampler);
}
void fmc_set_blend(FMaterialConfig* ptr, FMatBlendType blend_type) {
    as_rc(ptr)->item.set_blend(blend_type);
}

FMaterial* fmaterial_init(FSession* session, FMaterialConfig* config) {
    auto config_ptr = as_rc(config);

    auto ptr = make_refcounted_unsafe<FMaterialContent>(
        session->engine(),
        session->new_instance_for_type(config_ptr->item),
        config_ptr->item);

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
void fmaterial_set_ao_factor(FMaterial* ptr, float ao) {
    as_rc(ptr)->item.set_ao(ao);
}
void fmaterial_set_emissive(FMaterial* ptr, float strength, float3 factor) {
    as_rc(ptr)->item.set_emissive(strength, factor);
}
void fmaterial_set_transmission(FMaterial* ptr, float tf) {
    as_rc(ptr)->item.set_transmission(tf);
}
void fmaterial_set_ior(FMaterial* ptr, float ior) {
    as_rc(ptr)->item.set_ior(ior);
}
void fmaterial_set_texture(FMaterial*      ptr,
                           FMatTexSemantic semantic,
                           FTexture*       tex,
                           Sampler*        sampler) {
    as_rc(ptr)->item.set_texture(semantic, as_rc(tex)->borrow(), *sampler);
}

// =============================================================================
// File probe and image initialization ========================================

static inline bool is_png_srgb(std::span<const char> bytes) {

    // each chunk has a header of 8, a u32 len and 4 x u8 ident

    auto advance = [&bytes](size_t amount) {
        if (bytes.size() < amount) {
            bytes = {};
        } else {
            bytes = bytes.subspan(amount);
        }
    };

    auto take_prim = [&]<class T>(T& out) {
        std::memcpy(&out, bytes.data(), sizeof(T));
        advance(sizeof(T));
    };

    auto take_uint32 = [&]() -> uint32_t {
        uint32_t local = 0;
        take_prim(local);

        return __builtin_bswap32(local);
    };

    auto near = [](float a, float b) { return std::abs(a - b) < .001; };

    uint32_t                gamma     = 0;
    std::array<uint32_t, 8> cHRM      = {};
    std::array<uint32_t, 8> cHRM_srgb = {
        31270, 32900, 64000, 33000, 30000, 60000, 15000, 6000,
    };
    bool have_chrm = false;

    while (bytes.size() > 8) {
        uint32_t length = take_uint32();

        std::array<char, 4> ident = {};
        take_prim(ident);

        auto view = std::string_view(ident.data(), 4);

        if (view == "sRGB") {
            return true;
        } else if (view == "gAMA") {
            gamma = take_uint32();
        } else if (view == "cHRM") {
            take_prim(cHRM);
            have_chrm = true;
        }

        // skip data and CRC
        advance(length + 4);
    }

    if (gamma != 0 and have_chrm) {
        bool gamma_ok = 45452 < gamma and gamma < 45457;

        if (!gamma_ok) return false;

        for (int i = 0; i < cHRM.size(); i++) {
            if (__builtin_bswap32(cHRM[i]) != cHRM_srgb[i]) { return false; }
        }

        return true;
    }

    return false;
}

static inline bool probe_kind_from_magic(std::span<const char> bytes,
                                         FImageFileInfo*       out) {

    // EXR
    if (bytes.size() >= 4) {
        // EXR magic: 0x762F3101 (little-endian order in file)
        const unsigned char* u = (const unsigned char*)bytes.data();
        uint32_t magic = (uint32_t)u[0] | ((uint32_t)u[1] << 8) |
                         ((uint32_t)u[2] << 16) | ((uint32_t)u[3] << 24);
        if (magic == 0x01312F76u || magic == 0x762F3101u) {
            out->colorspace = CS_LINEAR;
            out->kind       = IMG_EXR;
            return true;
        }
    }

    // PNG
    // TODO: PNG16/24 support
    if (bytes.size() >= 8) {
        const unsigned char* u = (const unsigned char*)bytes.data();
        // PNG signature
        if (u[0] == 0x89 && u[1] == 0x50 && u[2] == 0x4E && u[3] == 0x47 &&
            u[4] == 0x0D && u[5] == 0x0A && u[6] == 0x1A && u[7] == 0x0A) {

            bool is_srgb = is_png_srgb(bytes.subspan(8));

            out->colorspace = is_srgb ? CS_SRGB : CS_LINEAR;
            out->kind       = IMG_PNG;
            return true;
        }
    }

    // JPG
    if (bytes.size() >= 3) {
        const unsigned char* u = (const unsigned char*)bytes.data();
        // JPEG SOI
        if (u[0] == 0xFF && u[1] == 0xD8 && u[2] == 0xFF) {
            out->colorspace = CS_SRGB;
            out->kind       = IMG_JPEG;
            return true;
        }
    }

    // HDR
    if (bytes.size() >= 10) {
        // Radiance HDR starts with ASCII "#?RADIANCE" or "#?RGBE"
        std::string_view head(
            bytes.data(), bytes.data() + std::min<size_t>(bytes.size(), 10));
        if (head.rfind("#?RADIANCE", 0) == 0 || head.rfind("#?RGBE", 0) == 0) {
            out->colorspace = CS_LINEAR;
            out->kind       = IMG_HDR;
            return true;
        }
    }

    return false;
}

uint8_t fimg_probe(FBlobRef ref, FImageFileInfo* out) {
    auto b = ref_to_bytes(ref);
    if (!b) return 0;
    if (!out) return 0;
    out->kind       = IMG_UNKNOWN;
    out->colorspace = CS_SRGB;
    return probe_kind_from_magic(b.span(), out);
}

FImage* fimg_init_raw(FBlobRef ref, FImageRawDesc const* desc) {
    if (!desc) return nullptr;
    auto b = ref_to_bytes(ref);
    if (!b) return nullptr;

    // Validate byte size
    size_t expected = (size_t)desc->width * (size_t)desc->height *
                      (size_t)desc->n_channels *
                      (desc->type == PIXEL_UBYTE ? 1 : sizeof(float));

    if (b.size() < expected) {
        spdlog::error("Raw pixel blob too small: {} < {}", b.size(), expected);
        return nullptr;
    }

    // Looks good...

    auto ptr =
        make_refcounted_unsafe<FImageContent>(*desc, b.subspan(0, expected));
    return from_rc(ptr);
}


inline FImage* use_float_decoder(FBlobRef ref) {
    auto ptr = make_refcounted_unsafe<FImageContent>(ref);
    return from_rc(ptr);
}

FImage* use_ldr_decoder(FBlobRef ref, FColorSpace colorspace) {
    spdlog::debug("Using LDR decoder");
    // must be valid, otherwise we couldn't get to this function
    auto b = ref_to_bytes(ref);

    int x    = 0;
    int y    = 0;
    int comp = 0;

    if (!stbi_info_from_memory(
            (const stbi_uc*)b.data(), (int)b.size(), &x, &y, &comp)) {
        spdlog::warn("stb info failed; falling back to float decoder");
        return use_float_decoder(ref);
    }

    spdlog::debug("Image probe finds {} {} {}", x, y, comp);

    int out_comp = comp;

    // Decode pixels to memory

    auto decoded_pixel_ptr = stbi_load_from_memory(
        (const stbi_uc*)b.data(), (int)b.size(), &x, &y, &out_comp, comp);

    if (!decoded_pixel_ptr or out_comp != comp) {
        spdlog::warn("stb decode failed; falling back to float decoder");

        return use_float_decoder(ref);
    }

    // Move those pixels around...

    size_t nbytes = (size_t)x * (size_t)y * (size_t)comp;

    auto pixels = Bytes::take_ownership(
        (char const*)decoded_pixel_ptr, nbytes, stbi_image_free);


    FImageRawDesc d {
        .width      = (uint32_t)x,
        .height     = (uint32_t)y,
        .n_channels = (uint8_t)comp,
        .byte_size  = nbytes,
        .type       = PIXEL_UBYTE,
        .colorspace = colorspace,
    };

    auto ptr = make_refcounted_unsafe<FImageContent>(d, pixels);

    return from_rc(ptr);
}

FImage* fimg_init_decode_file(FBlobRef ref) {
    auto b = ref_to_bytes(ref);

    spdlog::debug("Reading file from blob {} ({} + {})",
                  (void*)ref.id,
                  ref.start,
                  ref.length);

    if (!b) return nullptr;

    FImageFileInfo info;

    if (!probe_kind_from_magic(b.span(), &info)) return nullptr;

    spdlog::debug("Image is kind {} {}",
                  magic_enum::enum_name(info.kind),
                  magic_enum::enum_name(info.colorspace));

    switch (info.kind) {
    case IMG_EXR:
    case IMG_HDR:
        // Use Filament's float decoder for HDR
        // Color space request will be ignored; these wont be SRGB
        return use_float_decoder(ref);
    case IMG_PNG:
    case IMG_JPEG: return use_ldr_decoder(ref, info.colorspace);
    default: return use_float_decoder(ref);
    }
}
void fimg_acquire(FImage* ptr) {
    as_rc(ptr)->retain();
}
void fimg_release(FImage* ptr) {
    as_rc(ptr)->release();
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
void fconfig_set_fullscreen(FConfig* ptr, uint8_t b) {
    ptr->full_screen = b;
}
void fconfig_set_thread_count(FConfig* ptr, uint8_t count) {
    ptr->thread_count = count;
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

void fconfig_set_renderer(FConfig* ptr, FRenderer renderer) {
    switch (renderer) {

    case R_METAL: ptr->renderer = filament::backend::Backend::METAL; break;
    case R_OPENGL: ptr->renderer = filament::backend::Backend::OPENGL; break;
    case R_VULKAN: ptr->renderer = filament::backend::Backend::VULKAN; break;
    }
}

void fconfig_set_stereo_eye(FConfig* ptr, FEye eye) {
    ptr->left_eye = eye == EYE_LEFT;
}

void fconfig_set_ssao(FConfig* ptr, bool b) {
    ptr->use_ssao = b;
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

void fs_debug_camera_obj(FSession* ptr, char const* file) {
    ptr->debug_camera_obj(file);
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
