#include "material.h"

#include "session.h"

#include <filament/TextureSampler.h>
#include <image/LinearImage.h>
#include <imageio/ImageDecoder.h>

#include <magic_enum/magic_enum.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>


// =============================================================================

/// An in-memory stream class. Useful for filament APIs that want file streams,
/// but we can point them to memory instead
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
        .n_channels = static_cast<uint8_t>(n),
        .byte_size  = (size_t)w * (size_t)h * (size_t)n * sizeof(float),
        .type       = PIXEL_FLOAT32,
        .colorspace = CS_LINEAR,
    };

    spdlog::info(
        "Creating image {} {} {} {} bytes", w, h, n, m_description.byte_size);

    // documentation says image data under the hood is refcounted??
    m_linear = std::make_unique<image::LinearImage>(lin_image);

    spdlog::debug("Creating image {} from blob {}", (void*)this, (void*)ref.id);
}

FImageContent::FImageContent(FImageRawDesc const& desc, Bytes pixels)
    : m_description(desc), m_raw(pixels) {
    expect(desc.n_channels >= 1 && desc.n_channels <= 4,
           "Unsupported channel count");


    spdlog::info("Creating image, {} {}, {} {} {} = {} bytes",
                 magic_enum::enum_name(desc.type),
                 magic_enum::enum_name(desc.colorspace),
                 m_description.width,
                 m_description.height,
                 m_description.n_channels,
                 m_description.byte_size);
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
        .usage(filament::Texture::Usage::DEFAULT |
               filament::Texture::Usage::GEN_MIPMAPPABLE);
}


void FTextureContent::completion(void* buffer, size_t, void* user) {
    spdlog::debug("Completing texture {}", user);
    auto* ptr    = ((FTextureContent*)user);

    // we no longer need the image
    // ptr->m_image = {};

    // Remove all staging bytes
    // ptr->m_staging_bytes.clear();
    // ptr->m_staging_bytes.shrink_to_fit();

    // Regenerate mipmaps
    ptr->texture()->generateMipmaps(*ptr->m_engine);

    ptr->m_ready = true;

    // Drop the temporary self-retain taken before setImage.
    auto* rc_self = reinterpret_cast<RefCounted<FTextureContent>*>(ptr);
    rc_self->release();
}

static inline uint8_t to_unorm8(float v) {
    float x = std::clamp(v, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::lroundf(x * 255.0f));
}

static inline uint8_t to_srgb8(float linear) {
    float x    = std::clamp(linear, 0.0f, 1.0f);
    float srgb = x <= 0.0031308f ? x * 12.92f
                                 : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(
        std::lroundf(std::clamp(srgb, 0.0f, 1.0f) * 255.0f));
}

static inline void convert_float_to_u8(float const*          src,
                                       size_t                width,
                                       size_t                height,
                                       size_t                nchannel,
                                       bool                  desire_srgb,
                                       std::vector<uint8_t>& dest) {
    dest.resize(width * height * nchannel);

    uint8_t* dst = dest.data();

    if (desire_srgb) {
        for (size_t i = 0, n = width * height; i < n; ++i) {
            for (size_t c = 0; c < nchannel; ++c) {
                dst[i * nchannel + c] = to_srgb8(src[i * nchannel + c]);
            }
        }
    } else {
        for (size_t i = 0, n = width * height; i < n; ++i) {
            for (size_t c = 0; c < nchannel; ++c) {
                dst[i * nchannel + c] = to_unorm8(src[i * nchannel + c]);
            }
        }
    }
}

FTextureContent::FTextureContent(FSession* session, FTextureConfig& config)
    : m_image(config.image), m_engine(session->engine()) {
    m_texture = config.builder.build(*m_engine);

    auto const& desc   = m_image->description();
    auto const  width  = static_cast<size_t>(desc.width);
    auto const  height = static_cast<size_t>(desc.height);
    auto const  ch     = static_cast<size_t>(desc.n_channels);

    auto pixel_format = filament::Texture::Format::RGB;
    switch (desc.n_channels) {
    case 1: pixel_format = filament::Texture::Format::R; break;
    case 2: pixel_format = filament::Texture::Format::RG; break;
    case 3: pixel_format = filament::Texture::Format::RGB; break;
    case 4: pixel_format = filament::Texture::Format::RGBA; break;
    default: break;
    }

    // Desired upload type inferred from internal format request
    bool desire_u8   = false;
    bool desire_srgb = false;

    switch (config.requested_format) {
    case FMT_SRGB8:
    case FMT_SRGB8_A8:
    case FMT_AUTO_SRGB_COLOR:
        desire_u8   = true;
        desire_srgb = true;
        break;
    case FMT_R8:
    case FMT_RG8:
    case FMT_RGB8:
    case FMT_RGBA8:
    case FMT_AUTO_LINEAR_DATA:
        desire_u8   = true;
        desire_srgb = false;
        break;
    default: desire_u8 = false; break; // float formats
    }

    filament::Texture::Type pixel_type = filament::Texture::Type::FLOAT;
    const void*             data_ptr   = nullptr;
    size_t                  byte_size  = desc.byte_size;

    if (m_image->has_float()) {

        // Source is float32 linear

        if (desire_u8) {
            // but they want u8
            spdlog::warn("Converting linear texture to ubyte");

            convert_float_to_u8(m_image->image_float().getPixelRef(),
                                width,
                                height,
                                ch,
                                desire_srgb,
                                m_staging_bytes);


            pixel_type = filament::Texture::Type::UBYTE;

            data_ptr   = m_staging_bytes.data();
            byte_size  = m_staging_bytes.size();

        } else {
            // float-in, float-out
            spdlog::debug("Linear to linear: no conversion");

            data_ptr   = m_image->image_float().getPixelRef();
            pixel_type = filament::Texture::Type::FLOAT;
        }
    } else {
        // Image does NOT have floating point data.

        // It is possible to have float in the bytes if it came from an external
        // source, not the file loader.

        if (desire_u8) {
            switch (m_image->pixel_type()) {
            case PIXEL_UBYTE:
                spdlog::debug("U8 to U8: no conversion");
                // No conversion required
                // we have stored the image, so the data refs here should live
                // long enough
                data_ptr   = m_image->image_raw().data();
                pixel_type = filament::Texture::Type::UBYTE;
                byte_size  = m_image->description().byte_size;
                break;
            case PIXEL_FLOAT32:
                spdlog::warn("Float to U8: conversion!");
                // We will have to convert.
                convert_float_to_u8(m_image->image_float().getPixelRef(),
                                    width,
                                    height,
                                    ch,
                                    desire_srgb,
                                    m_staging_bytes);


                pixel_type = filament::Texture::Type::UBYTE;

                data_ptr  = m_staging_bytes.data();
                byte_size = m_staging_bytes.size();
                break;
            }
        } else {
            // they want float

            switch (m_image->pixel_type()) {
            case PIXEL_UBYTE:
                // They want float, but we have u8. So we have to convert.
                // but is this SRGB?
                spdlog::critical("Float to U8 Not yet supported");
                abort();
                break;
            case PIXEL_FLOAT32:
                // float-in, float-out
                spdlog::debug("Linear to linear: no conversion");

                data_ptr   = m_image->image_raw().data();
                pixel_type = filament::Texture::Type::FLOAT;
                byte_size  = m_image->description().byte_size;
                break;
            }
        }
    }

    spdlog::debug("Start transfer {} {} {}",
                  byte_size,
                  magic_enum::enum_name(pixel_format),
                  magic_enum::enum_name(pixel_type));

    // Keep the refcounted wrapper alive until Filament calls completion.
    auto* rc_self = reinterpret_cast<RefCounted<FTextureContent>*>(this);
    rc_self->retain();

    spdlog::debug("QUICK DUMP: {} {} {} {}",
                  ((uint8_t*)data_ptr)[0],
                  ((uint8_t*)data_ptr)[1],
                  ((uint8_t*)data_ptr)[2],
                  ((uint8_t*)data_ptr)[3]);

    // Transfer to GPU
    auto buffer =
        filament::Texture::PixelBufferDescriptor(data_ptr,
                                                 byte_size,
                                                 pixel_format,
                                                 pixel_type,
                                                 FTextureContent::completion,
                                                 this);

    try {
        m_texture->setImage(*m_engine, 0, std::move(buffer));
    } catch (...) {
        rc_self->release();
        throw;
    }

    spdlog::debug("Creating texture {}", (void*)this);
}

FTextureContent::~FTextureContent() {
    assert(m_staging_bytes.empty());
    m_engine->destroy(m_texture);

    spdlog::debug("Destroying texture {}", (void*)this);
}

bool FTextureContent::wait_ready(uint32_t timeout_ms) {
    while (!m_ready.load()) { }

    return true;
}

// =============================================================================


FTextureConfig* ftex_config_init(FImage* ptr, FTextureFormat format) {
    if (!ptr) return nullptr;

    auto p = new FTextureConfig(as_rc(ptr));

    auto fmt = filament::Texture::InternalFormat::RGB8;

    // channel count is used by auto selectors
    auto const& desc = p->image->description();
    auto const  ch   = (int)desc.n_channels;

    switch (format) {
    case FMT_R8: fmt = filament::Texture::InternalFormat::R8; break;
    case FMT_RG8: fmt = filament::Texture::InternalFormat::RG8; break;
    case FMT_RGB8: fmt = filament::Texture::InternalFormat::RGB8; break;
    case FMT_RGBA8: fmt = filament::Texture::InternalFormat::RGBA8; break;

    case FMT_SRGB8: fmt = filament::Texture::InternalFormat::SRGB8; break;
    case FMT_SRGB8_A8: fmt = filament::Texture::InternalFormat::SRGB8_A8; break;

    case FMT_R16F: fmt = filament::Texture::InternalFormat::R16F; break;
    case FMT_RG16F: fmt = filament::Texture::InternalFormat::RG16F; break;
    case FMT_RGB16F: fmt = filament::Texture::InternalFormat::RGB16F; break;
    case FMT_RGBA16F: fmt = filament::Texture::InternalFormat::RGBA16F; break;

    case FMT_RGB32F: fmt = filament::Texture::InternalFormat::RGB32F; break;
    case FMT_RGBA32F: fmt = filament::Texture::InternalFormat::RGBA32F; break;

    case FMT_R11F_G11F_B10F:
        fmt = filament::Texture::InternalFormat::R11F_G11F_B10F;
        break;
    case FMT_AUTO_SRGB_COLOR:
        fmt = (ch == 4) ? filament::Texture::InternalFormat::SRGB8_A8
                        : filament::Texture::InternalFormat::SRGB8;
        break;
    case FMT_AUTO_LINEAR_DATA:
        if (ch == 1) fmt = filament::Texture::InternalFormat::R8;
        else if (ch == 2)
            fmt = filament::Texture::InternalFormat::RG8;
        else if (ch == 4)
            fmt = filament::Texture::InternalFormat::RGBA8;
        else
            fmt = filament::Texture::InternalFormat::RGB8;
        break;
    default: spdlog::warn("Unknown texture format {}!", (int)format);
    }

    spdlog::debug("ftex set format {}", (int)fmt);

    p->builder.format(fmt);
    p->requested_format = format;

    return p;
}

void ftex_config_destroy(FTextureConfig* ptr) {
    delete ptr;
}

FTexture* ftex_init(FSession* ptr, FTextureConfig* cfg) {
    auto p = make_refcounted_unsafe<FTextureContent>(ptr, *cfg);

    return from_rc(p);
}
uint8_t ftex_wait_ready(FTexture* ptr, uint32_t timeout_ms) {
    if (!ptr) return 0;
    return as_rc(ptr)->item.wait_ready(timeout_ms) ? 1 : 0;
}
void ftex_acquire(FTexture* ptr) {
    as_rc(ptr)->retain();
}
void ftex_release(FTexture* ptr) {
    as_rc(ptr)->release();
}


// =============================================================================

void FMaterialConfigInternal::set_option(FMatOption option, uint8_t opt) {
    switch (option) {
    case DOUBLE_SIDED: material_key.doubleSided = opt; break;
    case UNLIT: material_key.unlit = opt; break;
    case CLEARCOAT: material_key.hasClearCoat = opt; break;
    case TRANSMISSION: {
        material_key.hasTransmission = opt;
        material_key.hasVolume       = opt;
    } break;
    case IOR: material_key.hasIOR = opt; break;
    }
}

void FMaterialConfigInternal::set_texture(FMatTexSemantic semantic,
                                          FMatTexUVSlot   slot,
                                          FTexture*       tex,
                                          Sampler*        sampler) {
    auto texture = as_rc(tex);

    linked_textures.at(semantic)         = texture->borrow();
    linked_texture_samplers.at(semantic) = *sampler;

    switch (semantic) {
    case BASE_COLOR_TEX:
        material_key.hasBaseColorTexture = true;
        material_key.baseColorUV         = slot;
        break;
    case NORMAL_TEX:
        material_key.hasNormalTexture = true;
        material_key.normalUV         = slot;
        break;
    case OCCLUSION_TEX:
        material_key.hasOcclusionTexture = true;
        // no uv?
        break;
    case EMISSIVE_TEX:
        material_key.hasEmissiveTexture = true;
        material_key.emissiveUV         = slot;
        break;
    case METAL_ROUGH_TEX:
        material_key.hasMetallicRoughnessTexture = true;
        material_key.metallicRoughnessUV         = slot;
        break;
    case CLEARCOAT_TEX:
        material_key.hasClearCoatTexture = true;
        material_key.clearCoatUV         = slot;
        break;
    case CLEARCOAT_ROUGH_TEX:
        material_key.hasClearCoatRoughnessTexture = true;
        material_key.clearCoatRoughnessUV         = slot;
        break;
    case CLEARCOAT_NORMAL_TEX:
        material_key.hasClearCoatNormalTexture = true;
        material_key.clearCoatNormalUV         = slot;
        break;
    default: break;
    }
}
void FMaterialConfigInternal::set_blend(FMatBlendType type) {
    switch (type) {
    case OPAQUE:
        material_key.alphaMode = filament::gltfio::AlphaMode::OPAQUE;
        break;
    case MASK:
        material_key.alphaMode = filament::gltfio::AlphaMode::MASK;
        break;
    case BLEND:
        material_key.alphaMode = filament::gltfio::AlphaMode::BLEND;
        break;
    default: break;
    }
}


// =============================================================================

FMaterialContent::FMaterialContent(filament::Engine*              engine,
                                   filament::MaterialInstance*    instance,
                                   FMaterialConfigInternal const& config)
    : m_engine(engine), m_instance(instance) {
    spdlog::debug("new material: {}", (void*)this);

    assert(magic_enum::enum_count<FMatTexSemantic>() <
           m_linked_textures.size());

    for (auto i : magic_enum::enum_values<FMatTexSemantic>()) {
        auto const& tex  = config.linked_textures[(int)i];
        auto const& samp = config.linked_texture_samplers[(int)i];
        if (tex) { set_texture(i, tex, samp); }
    }

    // spdlog::debug("Mat key:");

    //__builtin_dump_struct(&config.material_key, &printf);

    // auto* mat = instance->getMaterial();

    // std::vector<filament::Material::ParameterInfo> infos(
    //     mat->getParameterCount());

    // mat->getParameters(infos.data(), infos.size());

    // for (auto info : infos) {
    //     spdlog::debug("- {}: {} {}",
    //                   info.name,
    //                   magic_enum::enum_name(info.type),
    //                   magic_enum::enum_name(info.samplerType));
    // }
}

FMaterialContent::~FMaterialContent() {
    spdlog::debug("Destroying material: {}", (void*)this);
    m_engine->destroy(m_instance);
}

void FMaterialContent::set_color(FColor const& c) {
    m_instance->setParameter("baseColorFactor",
                             filament::math::float4 { c.r, c.g, c.b, c.a });
}

void FMaterialContent::set_rm(float r, float m) {
    m_instance->setParameter("roughnessFactor", r);
    m_instance->setParameter("metallicFactor", m);
}

void FMaterialContent::set_ao(float ao) {
    m_instance->setParameter("aoStrength", ao);
}

void FMaterialContent::set_emissive(float strength, float3 factor) {
    m_instance->setParameter(
        "emissiveFactor",
        filament::math::float3 { factor.x, factor.y, factor.z });
    m_instance->setParameter("emissiveStrength", strength);
}

void FMaterialContent::set_transmission(float ao) {
    m_instance->setParameter("transmissionFactor", ao);
}

void FMaterialContent::set_clearcoat(float cc) {
    m_instance->setParameter("clearCoatFactor", cc);
}

void FMaterialContent::set_ior(float ior) {
    m_instance->setParameter("ior", ior);
}


inline const char* map_semantic_to_param(FMatTexSemantic semantic) {
    switch (semantic) {
    case BASE_COLOR_TEX: return "baseColorMap";
    case NORMAL_TEX: return "normalMap";
    case OCCLUSION_TEX: return "occlusionMap";
    case EMISSIVE_TEX: return "emissiveMap";
    case METAL_ROUGH_TEX: return "metallicRoughnessMap";
    case CLEARCOAT_TEX: return "clearCoatMap";
    case CLEARCOAT_ROUGH_TEX: return "clearCoatRoughnessMap";
    case CLEARCOAT_NORMAL_TEX: return "clearCoatNormalMap";
    default:
        spdlog::warn("Unknown texture semantic {}", (int)semantic);
        return nullptr;
    }
}

void FMaterialContent::set_texture(FMatTexSemantic               semantic,
                                   Owned<FTextureContent> const& content,
                                   Sampler                       sampler) {
    spdlog::debug("Material {}, set tex {} {} {}",
                  (void*)this,
                  magic_enum::enum_name(semantic),
                  (void*)content.get(),
                  sampler.pack);
    m_linked_textures[semantic]         = content;
    m_linked_texture_samplers[semantic] = sampler;

    auto b_sampler =
        filament::TextureSampler(*(filament::backend::SamplerParams*)&sampler);

    auto parameter_name = map_semantic_to_param(semantic);

    spdlog::debug("Material {}: {} {}",
                  (void*)this,
                  parameter_name,
                  (void*)content->texture());

    //__builtin_dump_struct(&b_sampler, &printf);

    switch (semantic) {

    case BASE_COLOR_TEX: break;
    case NORMAL_TEX: m_instance->setParameter("normalScale", 1.0f); break;
    case OCCLUSION_TEX: break;
    case EMISSIVE_TEX: break;
    case METAL_ROUGH_TEX: break;
    case CLEARCOAT_TEX: break;
    case CLEARCOAT_ROUGH_TEX: break;
    case CLEARCOAT_NORMAL_TEX: break;
    }

    m_instance->setParameter(parameter_name, content->texture(), b_sampler);
}
