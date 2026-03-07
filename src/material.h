#pragma once

#include <filament/Texture.h>

#include <gltfio/MaterialProvider.h>

#include "backfill/api.h"
#include "utility.h"

#include <array>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace image {
class LinearImage;
}

class FImageContent {

    std::unique_ptr<image::LinearImage> m_linear; // float32 path
    Bytes                               m_raw;    // raw pixel bytes

    FImageRawDesc m_description;

public:
    DISABLE_MOVE_COPY(FImageContent);

    // Decode from an encoded file (EXR, PNG, JPG). Uses Filament's ImageDecoder
    // for HDR/EXR, may use another path for LDR.
    FImageContent(FBlobRef);
    // Create from user-provided raw pixels.
    FImageContent(FImageRawDesc const&, Bytes);
    ~FImageContent();

    auto const& description() const { return m_description; }
    FPixelType  pixel_type() const { return m_description.type; }
    FColorSpace colorspace() const { return m_description.colorspace; }

    // Accessors depending on storage kind
    bool                      has_float() const { return (bool)m_linear; }
    image::LinearImage const& image_float() const { return *m_linear; }
    Bytes const&              image_raw() const { return m_raw; }
};

C_BRIDGE(FImage, RefCounted<FImageContent>);

// =============================================================================

struct FTextureConfig {

    Owned<FImageContent> image;

    filament::Texture::Builder builder;

    // The requested high-level format from the API
    FTextureFormat requested_format = FMT_RGB8;

    FTextureConfig(RefCounted<FImageContent>*);
};

class FTextureContent {
    Owned<FImageContent> m_image;

    filament::Engine*  m_engine;
    filament::Texture* m_texture;

    // Staging buffer for 8-bit uploads (linear or sRGB)
    std::vector<uint8_t> m_staging_bytes;
    std::atomic<bool>    m_ready = false;

    static void completion(void* buffer, size_t size, void* user);

public:
    DISABLE_MOVE_COPY(FTextureContent);
    FTextureContent(FSession*, FTextureConfig&);
    ~FTextureContent();

    filament::Texture* texture() const { return m_texture; }
    bool               wait_ready(uint32_t timeout_ms);
};

C_BRIDGE(FTexture, RefCounted<FTextureContent>);

// =============================================================================


struct FMaterialConfigInternal {
    filament::gltfio::MaterialKey material_key {};

    std::array<Owned<FTextureContent>, 16> linked_textures {};
    std::array<Sampler, 16>                linked_texture_samplers {};

    void set_option(FMatOption, uint8_t);
    void set_texture(FMatTexSemantic, FMatTexUVSlot, FTexture*, Sampler*);
    void set_blend(FMatBlendType);
};

C_BRIDGE(FMaterialConfig, RefCounted<FMaterialConfigInternal>);


// =============================================================================


class FMaterialContent {
    filament::Engine*           m_engine   = nullptr;
    filament::MaterialInstance* m_instance = nullptr;

    std::array<Owned<FTextureContent>, 16> m_linked_textures {};
    std::array<Sampler, 16>                m_linked_texture_samplers {};

public:
    FMaterialContent(filament::Engine*              engine,
                     filament::MaterialInstance*    instance,
                     FMaterialConfigInternal const& config);

    ~FMaterialContent();

    void set_color(FColor const& c);

    void set_rm(float r, float m);

    void set_ao(float ao);

    void set_emissive(float strength, float3 factor);

    void set_transmission(float ao);

    void set_ior(float ior);

    void set_clearcoat(float cc);

    void set_texture(FMatTexSemantic, Owned<FTextureContent> const&, Sampler);

    operator filament::MaterialInstance*() const { return m_instance; }
};

C_BRIDGE(FMaterial, RefCounted<FMaterialContent>);
