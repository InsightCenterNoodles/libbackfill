#include "api.h"

#include "config.h"
#include "session.h"
#include "utility.h"

#include <filament/LightManager.h>
#include <filament/MaterialInstance.h>
#include <filament/View.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

extern "C" {

// =============================================================================

void mat4_identity(mat4* mat) {
    auto& rmat = *mat;
    rmat.a     = { 1, 0, 0, 0 };
    rmat.b     = { 0, 1, 0, 0 };
    rmat.c     = { 0, 0, 1, 0 };
    rmat.d     = { 0, 0, 0, 1 };
}
void mat4_transform(mat4* ret, float3 pos) {
    auto& new_mat = *(filament::math::mat4f*)ret;

    new_mat = new_mat * filament::math::mat4f::translation(
                            filament::math::float3 { pos.x, pos.y, pos.z });
}

// =============================================================================

void pack_vertex(FVertexPNU const* source,
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
    return new FBlob(FBlob::from_copy({ data, byte_count }));
}
void fblob_destroy(FBlob* ptr) {
    delete ptr;
}


// =============================================================================
struct FMesh {
    std::shared_ptr<FMeshContent> ptr;
};

FMesh* fmesh_init(FSession*      session,
                  FBlobRef       vertex_reference,
                  uint32_t       vertex_count,
                  FBlobRef       index_reference,
                  uint32_t       index_count,
                  FMeshIndexType type,
                  aabb           bounding_box) {

    auto content = std::make_shared<FMeshContent>(session,
                                                  vertex_reference,
                                                  vertex_count,
                                                  index_reference,
                                                  index_count,
                                                  type,
                                                  bounding_box);

    auto* ptr = new FMesh;

    ptr->ptr = content;

    return ptr;
}

void fmesh_destroy(FMesh* ptr) {
    delete ptr;
}

// =============================================================================

struct FMaterial {
    std::shared_ptr<FMaterialContent> ptr;
};

FMaterial* fmaterial_init(FSession* session, FMaterialConfig flags) {

    auto content = std::make_shared<FMaterialContent>(
        session->engine(),
        session->new_instance_for_type(MaterialType::Lit),
        (bool)flags.instanced);

    auto* ptr = new FMaterial;

    ptr->ptr = content;

    return ptr;
}
void fmaterial_destroy(FMaterial* material) {
    delete material;
}

void fmaterial_set_base_color(FMaterial* ptr, FColor c) {
    ptr->ptr->set_color(c);
}
void fmaterial_set_roughness_metallic(FMaterial* ptr, float r, float m) {
    ptr->ptr->set_rm(r, m);
}
void fmaterial_set_instances(FMaterial* ptr, mat4* data, u64 count) {
    ptr->ptr->set_instances(data, count);
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
void flc_set_shadows(FLightConfig* ptr, bool shadows) {
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
void fconfig_set_offaxis_plane(FConfig* ptr, FScreenPlane* plane) {
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

void fs_set_postprocess(FSession* ptr, bool opt) {
    ptr->view()->setPostProcessingEnabled(opt);
}

void fs_set_skybox_color(FSession* ptr, FColor color) {
    auto builder = filament::Skybox::Builder();
    builder.color({ color.r, color.g, color.b, color.a });

    auto sb = make_skybox(builder, ptr->engine());

    ptr->set_skybox(sb);
}

bool fs_frame(FSession* ptr) {
    return ptr->run_frame();
}

i32 fs_new_entity(FSession* ptr) {
    auto e = ptr->new_entity();
    return utils::Entity::smuggle(e);
}

// This does destroy component content, but I feel skeptical...
void fs_destroy_entity(FSession* ptr, i32 id) {
    ptr->manager().destroy(utils::Entity::import(id));
}

void fs_add_renderable(FSession* ptr, i32 entity, FMesh* mesh, FMaterial* mat) {
    auto e = utils::Entity::import(entity);

    ptr->add_renderable(e, mesh->ptr, mat->ptr);
}

void fs_del_renderable(FSession* ptr, i32 entity) {
    auto e = utils::Entity::import(entity);

    ptr->del_renderable(e);
}

void fs_add_transform(FSession* ptr, i32 entity, mat4 const* data) {
    auto e = utils::Entity::import(entity);

    ptr->add_transform(e, data);
}

void fs_set_parent(FSession* ptr, i32 child, i32 parent) {
    ptr->set_parent(utils::Entity::import(child),
                    utils::Entity::import(parent));
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
