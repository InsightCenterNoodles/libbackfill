#include "material.h"

#include <filament/TextureSampler.h>
#include <image/LinearImage.h>
#include <imageio/ImageDecoder.h>

#include "session.h"

#include <algorithm>
#include <cmath>
#include <magic_enum/magic_enum.hpp>


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
        .n_channels = n,
        .size       = (size_t)w * (size_t)h * (size_t)n * sizeof(float),
    };
    m_type       = PIXEL_FLOAT32;
    m_colorspace = CS_LINEAR;

    spdlog::info("Found image {} {} {} {} bytes", w, h, n, m_description.size);

    // documentation says image data under the hood is refcounted??
    m_linear = std::make_unique<image::LinearImage>(lin_image);

    auto* ptr = m_linear->getPixelRef();

    spdlog::debug("Data {} {} {} {}", ptr[0], ptr[1], ptr[2], ptr[3]);

    spdlog::debug("Creating image {} from blob {}", (void*)this, (void*)ref.id);
}

FImageContent::FImageContent(FImageRawDesc const& desc, Bytes pixels) {
    expect(desc.n_channels >= 1 && desc.n_channels <= 4,
           "Unsupported channel count");

    m_description = {
        .width      = desc.width,
        .height     = desc.height,
        .n_channels = desc.n_channels,
        .size       = (size_t)desc.width * (size_t)desc.height *
                (size_t)desc.n_channels *
                (desc.type == PIXEL_UBYTE ? 1 : sizeof(float)),
    };

    m_type       = desc.type;
    m_colorspace = desc.colorspace;
    m_raw        = pixels;
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
    auto* ptr    = ((FTextureContent*)user);
    ptr->m_image = {};
    ptr->m_staging_bytes.clear();
    ptr->m_staging_bytes.shrink_to_fit();
    ptr->texture()->generateMipmaps(*ptr->m_engine);
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
    size_t                  byte_size  = desc.size;

    if (m_image->has_linear()) {
        // Source is float32 linear
        if (desire_u8) {
            // Fallback convert float->8bit (linear or sRGB)
            m_staging_bytes.resize(width * height * ch);
            const float* src = m_image->image_linear().getPixelRef();
            uint8_t*     dst = m_staging_bytes.data();
            if (desire_srgb) {
                for (size_t i = 0, n = width * height; i < n; ++i) {
                    for (size_t c = 0; c < ch; ++c) {
                        dst[i * ch + c] = to_srgb8(src[i * ch + c]);
                    }
                }
            } else {
                for (size_t i = 0, n = width * height; i < n; ++i) {
                    for (size_t c = 0; c < ch; ++c) {
                        dst[i * ch + c] = to_unorm8(src[i * ch + c]);
                    }
                }
            }
            pixel_type = filament::Texture::Type::UBYTE;
            data_ptr   = m_staging_bytes.data();
            byte_size  = m_staging_bytes.size();
        } else {
            // float-in, float-out
            data_ptr   = m_image->image_linear().getPixelRef();
            pixel_type = filament::Texture::Type::FLOAT;
        }
    } else {
        // Source is raw bytes
        if (desire_u8 || m_image->pixel_type() == PIXEL_UBYTE) {
            data_ptr   = m_image->image_raw().data();
            pixel_type = filament::Texture::Type::UBYTE;
            byte_size  = m_image->description().size;
        } else {
            // Raw FLOAT32 upload
            data_ptr   = m_image->image_raw().data();
            pixel_type = filament::Texture::Type::FLOAT;
            byte_size  = m_image->description().size;
        }
    }

    // Transfer to GPU
    auto buffer = filament::Texture::PixelBufferDescriptor(
        data_ptr, byte_size, pixel_format, pixel_type, completion, this);

    m_texture->setImage(*m_engine, 0, std::move(buffer));

    spdlog::debug("Creating texture {}", (void*)this);
}

FTextureContent::~FTextureContent() {
    m_engine->destroy(m_texture);

    spdlog::debug("Destroying texture {}", (void*)this);
}

// =============================================================================


FTextureConfig* ftex_config_init(FImage* ptr, TextureFormat format) {
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
void ftex_acquire(FTexture* ptr) {
    as_rc(ptr)->retain();
}
void ftex_release(FTexture* ptr) {
    as_rc(ptr)->release();
}


// =============================================================================

void FMaterialConfigInternal::set_option(FMatTexOption option, uint8_t opt) {
    switch (option) {
    case DOUBLE_SIDED: material_key.doubleSided = opt; break;
    case UNLIT: material_key.unlit = opt; break;
    case CLEARCOAT: material_key.hasClearCoat = opt; break;
    case TRANSMISSION: material_key.hasTransmission = opt; break;
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
    case MAT_ROUGH_TEX:
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
    spdlog::debug("new material: {}", (void*)m_instance);

    for (auto i : magic_enum::enum_values<FMatTexSemantic>()) {
        auto const& tex  = config.linked_textures[(int)i];
        auto const& samp = config.linked_texture_samplers[(int)i];
        if (tex) { set_texture(i, tex, samp); }
    }
}

FMaterialContent::~FMaterialContent() {
    spdlog::debug("Destroying material: {}", (void*)m_instance);
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

void FMaterialContent::set_ior(float ior) {
    m_instance->setParameter("ior", ior);
}


inline const char* map_semantic_to_param(FMatTexSemantic semantic) {
    switch (semantic) {
    case BASE_COLOR_TEX: return "baseColorMap";
    case NORMAL_TEX: return "normalMap";
    case OCCLUSION_TEX: return "occlusionMap";
    case EMISSIVE_TEX: return "emissiveMap";
    case MAT_ROUGH_TEX: return "metallicRoughnessMap";
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
    m_linked_textures[semantic]         = content;
    m_linked_texture_samplers[semantic] = sampler;

    auto b_sampler =
        filament::TextureSampler(*(filament::backend::SamplerParams*)&sampler);

    m_instance->setParameter(
        map_semantic_to_param(semantic), content->texture(), b_sampler);
}
