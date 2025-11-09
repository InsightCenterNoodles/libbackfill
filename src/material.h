#pragma once

#include <filament/Texture.h>

#include <gltfio/MaterialProvider.h>

#include "backfill/api.h"
#include "utility.h"


namespace image {
class LinearImage;
}

struct ImageDescription {
    size_t width;
    size_t height;
    size_t n_channels;
    size_t size;
};

class FImageContent {

    std::unique_ptr<image::LinearImage> m_pending;

    ImageDescription m_description;

    // filament::Texture::PixelBufferDescriptor m_buffer;

    // static void completion(void* buffer, size_t size, void* user);

public:
    DISABLE_MOVE_COPY(FImageContent);

    FImageContent(FBlobRef);
    ~FImageContent();

    auto const& description() const { return m_description; }

    image::LinearImage const& image() const { return *m_pending; }
};

C_BRIDGE(FImage, RefCounted<FImageContent>);

// =============================================================================

struct FTextureConfig {

    Owned<FImageContent> image;

    filament::Texture::Builder builder;

    FTextureConfig(RefCounted<FImageContent>*);
};

class FTextureContent {
    Owned<FImageContent> m_image;

    filament::Engine*  m_engine;
    filament::Texture* m_texture;

    static void completion(void* buffer, size_t size, void* user);

public:
    DISABLE_MOVE_COPY(FTextureContent);
    FTextureContent(FSession*, FTextureConfig&);
    ~FTextureContent();

    filament::Texture* texture() const { return m_texture; }
};

C_BRIDGE(FTexture, RefCounted<FTextureContent>);

// =============================================================================


struct FMaterialConfigInternal {
    filament::gltfio::MaterialKey material_key;

    std::array<Owned<FTextureContent>, 16> linked_textures;
    std::array<Sampler, 16>                linked_texture_samplers;

    void set_option(FMatTexOption, uint8_t);
    void set_texture(FMatTexSemantic, FMatTexUVSlot, FTexture*, Sampler*);
    void set_blend(FMatBlendType);
};

C_BRIDGE(FMaterialConfig, RefCounted<FMaterialConfigInternal>);


// =============================================================================


class FMaterialContent {
    filament::Engine*           m_engine   = nullptr;
    filament::MaterialInstance* m_instance = nullptr;

    std::array<Owned<FTextureContent>, 16> m_linked_textures;
    std::array<Sampler, 16>                m_linked_texture_samplers;

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

    void set_texture(FMatTexSemantic, Owned<FTextureContent> const&, Sampler);

    operator filament::MaterialInstance*() const { return m_instance; }
};

C_BRIDGE(FMaterial, RefCounted<FMaterialContent>);
