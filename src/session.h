#pragma once

#include "backfill/api.h"
#include "geometry.h"
#include "material.h"
#include "renderstate.h"

#include <filament/Box.h>
#include <filament/Skybox.h>

#include <SDL3/SDL_timer.h>

#include <chrono>


std::shared_ptr<filament::Skybox> make_skybox(filament::Skybox::Builder builder,
                                              filament::Engine*);

using SkyboxPtr = std::shared_ptr<filament::Skybox>;

// =============================================================================

C_BRIDGE(FBlob, RefCounted<Bytes>);

// =============================================================================


inline Bytes ref_to_bytes(FBlobRef ref) {
    return as_rc(ref.id)->item.subspan(ref.start, ref.length);
}

// =============================================================================

class FMeshContent {
    filament::Engine* m_engine;
    LocalVertexBuffer m_verts;
    LocalIndexBuffer  m_index;
    filament::Box     m_box;


public:
    FMeshContent(FSession*      session,
                 FBlobRef       vertex_reference,
                 uint32_t       vertex_count,
                 FBlobRef       index_reference,
                 uint32_t       index_count,
                 FMeshIndexType type,
                 aabb           bounding_box);

    ~FMeshContent();

    filament::Engine*        engine() const { return m_engine; }
    LocalVertexBuffer const& verts() const { return m_verts; }
    LocalIndexBuffer const&  index() const { return m_index; }
    filament::Box const&     box() const { return m_box; }
};

C_BRIDGE(FMesh, RefCounted<FMeshContent>);


// =============================================================================

class EnvLightContent {
    Owned<FTextureContent> m_texture;
    filament::Engine*      m_engine;

    // owned:
    filament::Texture*       m_skybox_texture;
    filament::Texture*       m_specular;
    filament::Texture*       m_fog_texture;
    filament::IndirectLight* m_indirect_light;
    filament::Skybox*        m_skybox;

public:
    DISABLE_MOVE_COPY(EnvLightContent);

    EnvLightContent(FSession*, RefCounted<FTextureContent>*);
    ~EnvLightContent();

    filament::IndirectLight* indirect_light() { return m_indirect_light; }
    filament::Skybox*        skybox() { return m_skybox; }

    void set_intensity(float);
};

C_BRIDGE(FEnvironmentLight, RefCounted<EnvLightContent>);

// =============================================================================

struct UsedMatMesh {
    Owned<FMaterialContent> material;
    Owned<FMeshContent>     mesh;
};

class FSession : public RenderState {
    SkyboxPtr m_skybox;
    Owned<EnvLightContent> m_env_light;

    std::optional<ScreenDesc> m_offaxis_screen_info;

    filament::math::float3 m_head_pos;
    filament::math::quatf  m_head_rot;
    bool                   m_is_left = false;

    unsigned m_frame_skip_count = 0;
    std::chrono::high_resolution_clock::time_point m_last;

    filament::gltfio::MaterialProvider* m_provider;

    std::unordered_map<i32, UsedMatMesh> m_bound_render_resources;

public:
    FSession(FConfig const& config);
    ~FSession();

    void set_skybox(SkyboxPtr);
    void set_env_light(RefCounted<EnvLightContent>*);

    void update_head(float3 pos, float4 quat);

    filament::MaterialInstance*
    new_instance_for_type(FMaterialConfigInternal const&);

    utils::Entity new_entity();
    void          delete_entity(utils::Entity);

    void add_renderable(utils::Entity,
                        RefCounted<FMeshContent>*,
                        RefCounted<FMaterialContent>*);

    void del_renderable(utils::Entity);

    void add_transform(utils::Entity, mat4 const* tf);

    void set_parent(utils::Entity child, utils::Entity parent);

    void set_visible(utils::Entity, uint8_t);

    void debug_camera(mat4* out_model, mat4* out_proj);
    void debug_camera_obj(char const* file);

    bool run_frame();
};
