#pragma once

#include "backfill/api.h"
#include "geometry.h"
#include "renderstate.h"

#include <filament/Box.h>
#include <filament/Skybox.h>


std::shared_ptr<filament::Skybox> make_skybox(filament::Skybox::Builder builder,
                                              filament::Engine*);

using SkyboxPtr = std::shared_ptr<filament::Skybox>;

enum class MaterialType {
    Lit,
    InstanceLit,
};

// =============================================================================

struct FBlob : Bytes { };

inline Bytes _ref_to_bytes(FBlobRef ref) {
    return ref.id->subspan(ref.start, ref.length);
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

    filament::Engine*        engine() const { return m_engine; }
    LocalVertexBuffer const& verts() const { return m_verts; }
    LocalIndexBuffer const&  index() const { return m_index; }
    filament::Box const&     box() const { return m_box; }
};

// =============================================================================


struct FMaterialContent {
    filament::Engine*           m_engine         = nullptr;
    filament::MaterialInstance* m_instance       = nullptr;
    unsigned                    m_instance_count = 0;

public:
    FMaterialContent(filament::Engine*           engine,
                     filament::MaterialInstance* instance,
                     unsigned                    use_instances = 0);

    ~FMaterialContent();

    unsigned instance_count() const { return m_instance_count; }

    void set_color(FColor const& c);

    void set_rm(float r, float m);

    void set_instances(mat4* data, size_t count);

    operator filament::MaterialInstance*() const { return m_instance; }
};

// =============================================================================


struct UsedMatMesh {
    std::shared_ptr<FMaterialContent> material;
    std::shared_ptr<FMeshContent>     mesh;
};

class FSession : public RenderState {
    SkyboxPtr m_skybox;

    std::optional<ScreenDesc> m_offaxis_screen_info;

    float m_debug_head  = 0;

    std::vector<filament::Material*> m_materials;

    std::unordered_map<i32, UsedMatMesh> m_bound_render_resources;

public:
    FSession(FConfig const& config);

    void set_skybox(SkyboxPtr);

    filament::MaterialInstance* new_instance_for_type(MaterialType);

    utils::Entity new_entity();
    void          delete_entity(utils::Entity);

    void add_renderable(utils::Entity,
                        std::shared_ptr<FMeshContent> const&,
                        std::shared_ptr<FMaterialContent> const&);

    void del_renderable(utils::Entity);

    void add_transform(utils::Entity, mat4 const* tf);

    void set_parent(utils::Entity child, utils::Entity parent);

    bool run_frame();
};
