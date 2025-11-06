#pragma once

#ifdef __cplusplus
#    include <cstdint>
#else
#    include <stdint.h>
#endif

/// This API is SINGLE THREADED

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t  i32;
typedef uint64_t u64;

typedef struct FScreenPlane {
    double lower_left[3];
    double lower_right[3];
    double upper_right[3];
} FScreenPlane;

typedef struct short4 {
    int16_t x, y, z, w;
} short4;

typedef struct ushort2 {
    uint16_t x, y;
} ushort2;

typedef struct ushort3 {
    uint16_t x, y, z;
} ushort3;

typedef struct uint3 {
    uint32_t x, y, z;
} uint3;


typedef struct float2 {
    float x, y;
} float2;

typedef struct float3 {
    float x, y, z;
} float3;

typedef struct float4 {
    float x, y, z, w;
} float4;

/// Column major format
typedef struct mat4 {
    float4 a, b, c, d;
} mat4;

typedef struct aabb {
    float3 minimum;
    float3 maximum;
} aabb;

// These functions are for debugging. It is assumed that another library will be
// making matrices for you.

/// Initialize a matrix to the indentity.
void mat4_identity(mat4*);

/// Append a translation to the given matrix.
void mat4_translate(mat4*, float3);

/// Overwrite a matrix with the context of the column-major array.
void mat4_from_array(mat4* out, const float m[16]);

// =============================================================================

/// Unpacked vertex information
typedef struct FVertexPNU {
    float3 position;
    float3 normal;
    float2 uv;
} FVertexPNU;

typedef struct FPackedVertex {
    float3  position;
    short4  surface;
    ushort2 texture;
} FPackedVertex;

typedef struct FColor {
    float r, g, b, a;
} FColor;

typedef struct FConfig   FConfig;
typedef struct FSession  FSession;
typedef struct FBlob     FBlob;
typedef struct FMesh     FMesh;
typedef struct FMaterial FMaterial;

// =============================================================================

void pack_vertex_u16(FVertexPNU const* source,
                     uint32_t          vertex_count,
                     ushort3 const*    index,
                     uint32_t          index_count,
                     FPackedVertex*    dest);

void pack_vertex_u32(FVertexPNU const* source,
                     uint32_t          vertex_count,
                     uint3 const*      index,
                     uint32_t          index_count,
                     FPackedVertex*    dest);

// =============================================================================

/// All types with a release_ use reference counting. You MUST release after an
/// init. Init will create a pointer to an object with an RC of 1.

// =============================================================================

FBlob* fblob_init_copy(char const* data, u64 byte_count);
void   fblob_acquire(FBlob*);
void   fblob_release(FBlob*);

typedef struct FBlobRef {
    FBlob* id;
    u64    start;
    u64    length;
} FBlobRef;

FBlobRef fblobref_whole(FBlob*);

// =============================================================================

typedef struct FImage FImage;

FImage* fimg_init_exr(FBlobRef);
void    fimg_acquire(FImage*);
void    fimg_release(FImage*);

// =============================================================================

typedef struct FTexture       FTexture;
typedef struct FTextureConfig FTextureConfig;

typedef enum TextureFormat {
    R11F_G11F_B10F,
} TextureFormat;

FTextureConfig* ftex_config_init(FImage*, TextureFormat);
void            ftex_config_destroy(FTextureConfig*);

FTexture* ftex_init(FSession*, FTextureConfig*);
void      ftex_acquire(FTexture*);
void      ftex_release(FTexture*);


// =============================================================================

typedef struct FEnvironmentLight FEnvironmentLight;

FEnvironmentLight* fenv_light_init_equirect(FSession*, FTexture*);
void               fenv_light_acquire(FEnvironmentLight*);
void               fenv_light_release(FEnvironmentLight*);

// =============================================================================

typedef enum FMeshIndexType { U16, U32 } FMeshIndexType;

FMesh* fmesh_init(FSession*,
                  FBlobRef vertex_reference,
                  uint32_t vertex_count,
                  FBlobRef index_reference,
                  uint32_t index_count,
                  FMeshIndexType,
                  aabb bounding_box);

void fmesh_acquire(FMesh*);
void fmesh_release(FMesh*);

// =============================================================================

// In the future we can have material classes like Unlit, Lit, etc.
// consider new instance format (maybe use textures)
// used a compressed quat (recover qw). only allow uniform texture scale?
// would uniform scale break our arrows?
// px, py, pz, uvx
// qx, qy, qz, uvy
// sx, sy, sz, uvs

typedef enum MatConfigFlags {
    MC_UNLIT = (1 << 0),
} MatConfigFlags;

typedef struct FMaterialConfig {
    uint32_t mask;
    uint32_t instance_count;
} FMaterialConfig;


FMaterial* fmaterial_init(FSession*, FMaterialConfig*);
void       fmaterial_acquire(FMaterial*);
void       fmaterial_release(FMaterial*);

void fmaterial_set_base_color(FMaterial*, FColor);
void fmaterial_set_roughness_metallic(FMaterial*, float r, float m);

/// We use one matrix per instance. The data is copied into a UBO, which means a
/// limit of 1024 instances per material instance.
void fmaterial_set_instances(FMaterial*, mat4 const* data, u64 count);

// =============================================================================

typedef struct FLightConfig FLightConfig;

typedef enum FLightType { POINT, SPOT, DIRECTIONAL, SUN } FLightType;

FLightConfig* flightconfig_init(FLightType);
void          flightconfig_destroy(FLightConfig*);

void flc_set_intensity(FLightConfig*, float);
void flc_set_color(FLightConfig*, FColor);
void flc_set_falloff(FLightConfig*, float);
void flc_set_direction(FLightConfig*, float3);
void flc_set_spot_cone(FLightConfig*, float inner, float outer);
void flc_set_shadows(FLightConfig*, uint8_t);


// =============================================================================

FConfig* fconfig_init();
void     fconfig_destroy(FConfig*);

void fconfig_set_title(FConfig*, char const*);
void fconfig_set_display(FConfig*, char const*);
void fconfig_set_device(FConfig*, int index);
void fconfig_set_screen(FConfig*, int w, int h);
void fconfig_set_log_debug(FConfig*, uint8_t);
void fconfig_set_fullscreen(FConfig*, uint8_t);

/// Enable off-axis mode using this screen plane.
void fconfig_set_offaxis_plane(FConfig*, FScreenPlane const*);


/// Eye selection for stereo rendering
typedef enum FEye { EYE_LEFT, EYE_RIGHT } FEye;

/// Set off-axis eye
void fconfig_set_stereo_eye(FConfig*, FEye);

// =============================================================================

FSession* fs_init(FConfig*);
void      fs_destroy(FSession*);

void fs_set_postprocess(FSession*, uint8_t);

void fs_set_skybox_color(FSession*, FColor);
void fs_set_environment_light(FSession*, FEnvironmentLight*);

void fs_update_head(FSession*, float3 pos, float4 quat);

/// Render a frame. Processes window events. Returns false if the window has
/// been closed.
uint8_t fs_frame(FSession*);

/// Create a new, blank, entity
i32 fs_new_entity(FSession*);

void fs_destroy_entity(FSession*, i32);

/// Add a renderable component to an entity. You may delete the mesh and
/// material after this call.
void fs_add_renderable(FSession*, i32, FMesh*, FMaterial*);
void fs_del_renderable(FSession*, i32);

/// Set the transform of an entity
void fs_set_transform(FSession*, i32, mat4 const*);

/// Sets the parent, and CLEARS/overwrites the childs current transform if there
/// is one.
void fs_set_parent(FSession*, i32 child, i32 parent);

/// Extract current camera matrix info
void fs_debug_camera(FSession*, mat4* out_model, mat4* out_proj);
void fs_debug_camera_obj(FSession*, char const* file);

/// Add a light component to an entity. You may destroy or reuse the
/// configuration after this call.
void fs_add_light(FSession*, i32, FLightConfig*);
void fs_del_light(FSession*, i32);

// =============================================================================

#ifdef __cplusplus
}
#endif
