#pragma once

#include <climits>
#include <cstdint>

extern "C" {

using i32 = int32_t;
using u64 = uint64_t;

struct FScreenPlane {
    double lower_left[3];
    double lower_right[3];
    double upper_right[3];
};

struct ushort2 {
    uint16_t x, y;
};

struct ushort3 {
    uint16_t x, y, z;
};

struct float2 {
    float x, y;
};

struct float3 {
    float x, y, z;
};

struct float4 {
    float x, y, z, w;
};

struct short4 {
    int16_t x, y, z, w;
};

struct mat4 {
    float4 a, b, c, d;
};

struct aabb {
    float3 minimum;
    float3 maximum;
};

void mat4_identity(mat4*);
void mat4_transform(mat4*, float3);

// =============================================================================

/// Unpacked vertex information
struct FVertexPNU {
    float3 position;
    float3 normal;
    float2 uv;
};

struct FPackedVertex {
    float3  position;
    short4  surface;
    ushort2 texture;
};

struct FColor {
    float r, g, b, a;
};

struct FConfig;
struct FSession;
struct FBlob;
struct FMesh;
struct FMaterial;

// =============================================================================

void pack_vertex(FVertexPNU const* source,
                 uint32_t          vertex_count,
                 ushort3 const*    index,
                 uint32_t          index_count,
                 FPackedVertex*    dest);

// =============================================================================

/// Blobs are internallly refcounted. You MUST delete the one you create,
/// but you can safely destroy it and other users should be ok.

FBlob* fblob_init_copy(char const* data, u64 byte_count);
void   fblob_destroy(FBlob*);

struct FBlobRef {
    FBlob* id;
    u64    start  = 0;
    u64    length = SIZE_T_MAX;
};

// =============================================================================

enum FMeshIndexType { U16, U32 };

FMesh* fmesh_init(FSession*,
                  FBlobRef vertex_reference,
                  uint32_t vertex_count,
                  FBlobRef index_reference,
                  uint32_t index_count,
                  FMeshIndexType,
                  aabb bounding_box);

void fmesh_destroy(FMesh*);

// =============================================================================

// In the future we can have material classes like Unlit, Lit, etc.
// consider new instance format (maybe use textures)
// used a compressed quat (recover qw). only allow uniform texture scale?
// would uniform scale break our arrows?
// px, py, pz, uvx
// qx, qy, qz, uvy
// sx, sy, sz, uvs

struct FMaterialConfig {
    bool unlit : 1;
    bool instanced : 1;
};


FMaterial* fmaterial_init(FSession*, FMaterialConfig flags);
void       fmaterial_destroy(FMaterial*);

void fmaterial_set_base_color(FMaterial*, FColor);
void fmaterial_set_roughness_metallic(FMaterial*, float r, float m);
void fmaterial_set_instances(FMaterial*, mat4* data, u64 count);

// =============================================================================

struct FLightConfig;

enum FLightType { POINT, SPOT, DIRECTIONAL, SUN };

FLightConfig* flightconfig_init(FLightType);
void          flightconfig_destroy(FLightConfig*);

void flc_set_intensity(FLightConfig*, float);
void flc_set_color(FLightConfig*, FColor);
void flc_set_falloff(FLightConfig*, float);
void flc_set_direction(FLightConfig*, float3);
void flc_set_spot_cone(FLightConfig*, float inner, float outer);
void flc_set_shadows(FLightConfig*, bool);


// =============================================================================

FConfig* fconfig_init();
void     fconfig_destroy(FConfig*);

void fconfig_set_title(FConfig*, char const*);
void fconfig_set_display(FConfig*, char const*);
void fconfig_set_device(FConfig*, int);
void fconfig_set_screen(FConfig*, int w, int h);
void fconfig_set_offaxis_plane(FConfig*, FScreenPlane*);

// =============================================================================

FSession* fs_init(FConfig*);
void      fs_destroy(FSession*);

void fs_set_postprocess(FSession*, bool);

void fs_set_skybox_color(FSession*, FColor);

/// Render a frame. Processes window events. Returns false if the window has
/// been closed.
bool fs_frame(FSession*);

/// Create a new, blank, entity
i32 fs_new_entity(FSession*);

void fs_destroy_entity(FSession*, i32);

/// Add a renderable component to an entity. You may delete the mesh and
/// material after this call.
void fs_add_renderable(FSession*, i32, FMesh*, FMaterial*);
void fs_del_renderable(FSession*, i32);

/// Set the transform of an entity
void fs_add_transform(FSession*, i32, mat4 const*);

/// Sets the parent, and CLEARS the local transform
void fs_set_parent(FSession*, i32 child, i32 parent);

/// Add a light component to an entity. You may destroy or reuse the
/// configuration after this call.
void fs_add_light(FSession*, i32, FLightConfig*);
void fs_del_light(FSession*, i32);

// =============================================================================
}
