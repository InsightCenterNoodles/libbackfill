#pragma once

#ifdef __cplusplus
#    include <cstdint>
#else
#    include <stdint.h>
#endif

/// This API is SINGLE THREADED
/// All types with a release_ use reference counting. You MUST release after an
/// init. Init will create a pointer to an object with an RC of 1.

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

// Vertex Utilities ============================================================

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

// FBlobs ======================================================================
// These are binary blobs of data, refcounted. These allow us to do async
// uploads, and to refer to sub regions.

FBlob* fblob_init_copy(char const* data, u64 byte_count);
void   fblob_acquire(FBlob*);
void   fblob_release(FBlob*);

typedef struct FBlobRef {
    FBlob* id;
    u64    start;
    u64    length;
} FBlobRef;

FBlobRef fblobref_whole(FBlob*);

// Images ======================================================================
// Represents the raw bytes needed for texturing

// TODO: make sure we are SRGB safe for data content.
// This is annoying, as it seems most content is

typedef struct FImage FImage;

/// The type of pixels in an image
typedef enum FPixelType {
    PIXEL_UBYTE   = 0,
    PIXEL_FLOAT32 = 1,
} FPixelType;

/// The color space of an image
typedef enum FColorSpace {
    CS_LINEAR = 0,
    CS_SRGB   = 1,
} FColorSpace;

/// A description of an image, for raw byte annotation.
typedef struct FImageRawDesc {
    uint32_t    width;
    uint32_t    height;
    uint8_t     n_channels; // 1..4 supported
    uint64_t    byte_size;
    FPixelType  type;       // UBYTE or FLOAT32
    FColorSpace colorspace; // hint for choosing internal format and sampling
} FImageRawDesc;


/// Decode a blob of bytes as an image file
FImage* fimg_init_decode_file(FBlobRef);

/// Initialize an image from raw pixel memory referenced by the blob.
/// The blob region must contain exactly width*height*channels*bytes_per_pixel
/// bytes.
FImage* fimg_init_raw(FBlobRef, FImageRawDesc const*);

/// Lightweight file probe to choose decoding path
typedef enum FImageFileKind {
    IMG_UNKNOWN = 0,
    IMG_EXR,
    IMG_HDR, // Radiance HDR
    IMG_PNG,
    IMG_JPEG,
} FImageFileKind;

typedef struct FImageFileInfo {
    FImageFileKind kind;
    FColorSpace    colorspace;
} FImageFileInfo;

/// Inspect magic bytes to classify file kind. Returns 1 if recognized.
uint8_t fimg_probe(FBlobRef, FImageFileInfo* out);

void fimg_acquire(FImage*);
void fimg_release(FImage*);

// Textures ====================================================================

typedef struct FTexture       FTexture;
typedef struct FTextureConfig FTextureConfig;

/// The format of a texture
typedef enum FTextureFormat {
    // 8-bit UNorm (linear)
    FMT_R8,
    FMT_RG8,
    FMT_RGB8,
    FMT_RGBA8,

    // 8-bit sRGB
    FMT_SRGB8,
    FMT_SRGB8_A8,

    // 16-bit float (linear)
    FMT_R16F,
    FMT_RG16F,
    FMT_RGB16F,
    FMT_RGBA16F,

    // 32-bit float (linear)
    FMT_RGB32F,
    FMT_RGBA32F,

    // Packed float
    FMT_R11F_G11F_B10F,

    // Convenience selectors that choose based on channel count
    // AUTO_SRGB_COLOR:
    //   3->SRGB8,
    //   4->SRGB8_A8 (intended for baseColor/emissive)
    // AUTO_LINEAR_DATA:
    //   1->R8,
    //   2->RG8,
    //   3->RGB8,
    //   4->RGBA8 (for normals/ORM/etc)
    FMT_AUTO_SRGB_COLOR,
    FMT_AUTO_LINEAR_DATA,
} FTextureFormat;

FTextureConfig* ftex_config_init(FImage*, FTextureFormat);
void            ftex_config_destroy(FTextureConfig*);

FTexture* ftex_init(FSession*, FTextureConfig*);
void      ftex_acquire(FTexture*);
void      ftex_release(FTexture*);


// Environment Light ===========================================================

typedef struct FEnvironmentLight FEnvironmentLight;

FEnvironmentLight* fenv_light_init_equirect(FSession*, FTexture*);
void               fenv_light_acquire(FEnvironmentLight*);
void               fenv_light_release(FEnvironmentLight*);

void fenv_set_intensity(FEnvironmentLight*, float intensity);

// Mesh ========================================================================

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

// Materials ===================================================================

typedef enum FMatOption {
    DOUBLE_SIDED,
    UNLIT,
    CLEARCOAT,
    TRANSMISSION,
    IOR
} FMatOption;

typedef enum FMatTexSemantic {
    BASE_COLOR_TEX,
    NORMAL_TEX,
    OCCLUSION_TEX,
    EMISSIVE_TEX,
    METAL_ROUGH_TEX,
    CLEARCOAT_TEX,
    CLEARCOAT_ROUGH_TEX,
    CLEARCOAT_NORMAL_TEX,
} FMatTexSemantic;

typedef enum FMatTexUVSlot { UV0, UV1 } FMatTexUVSlot;

typedef enum FMatBlendType {
    OPAQUE,
    MASK,
    BLEND,
} FMatBlendType;

typedef struct FMaterialConfig FMaterialConfig;


typedef enum FMinFilter {
    MIN_FILTER_NEAREST,
    MIN_FILTER_LINEAR,
    MIN_FILTER_LINEAR_MIPMAP_LINEAR
} FMinFilter;

typedef enum FMagFilter {
    MAG_FILTER_NEAREST,
    MAG_FILTER_LINEAR,
} FMagFilter;

typedef enum FWrapMode {
    WRAP_CLAMP,
    WRAP_REPEAT,
    WRAP_MIRROR_REPEAT,
} FWrapMode;

typedef enum FTexAxis {
    AXIS_U,
    AXIS_V,
    AXIS_W,
} FTexAxis;

typedef struct Sampler {
    uint32_t pack;
} Sampler;

void fsamp_init(Sampler*);
void fsamp_set_mag(Sampler*, FMagFilter);
void fsamp_set_min(Sampler*, FMinFilter);
void fsamp_set_wrap(Sampler*, FWrapMode, FTexAxis);
void fsamp_set_aniso(Sampler*, uint8_t);

/// Create a material configuration. Material configs are used to specialize
/// material capabilities
FMaterialConfig* fmaterialconfig_init();
void             fmaterialconfig_destroy(FMaterialConfig*);

/// Set an option on a material.
/// NOTE: Attempting to set options parameters (like transmission) on a material
/// that is not enabled in the config will result in an error
void fmc_set_option(FMaterialConfig*, FMatOption, uint8_t);

/// Set, and enable, the use of a texture for a given semantic.
/// NOTE: Attempting to set a texture for a semantic on a constructed material
/// that does not have that semantic enabled is a hard error.
void fmc_set_texture(FMaterialConfig*,
                     FMatTexSemantic,
                     FMatTexUVSlot,
                     FTexture*,
                     Sampler*);

/// Enable, but do not set, the use of a texture semantic.
void fmc_enable_texture(FMaterialConfig*, FMatTexSemantic, FMatTexUVSlot);
void fmc_set_blend(FMaterialConfig*, FMatBlendType);

FMaterial* fmaterial_init(FSession*, FMaterialConfig*);
void       fmaterial_acquire(FMaterial*);
void       fmaterial_release(FMaterial*);

void fmaterial_set_base_color(FMaterial*, FColor);
void fmaterial_set_roughness_metallic(FMaterial*, float r, float m);
void fmaterial_set_ao_factor(FMaterial*, float ao);
void fmaterial_set_emissive(FMaterial*, float strength, float3 factor);
void fmaterial_set_transmission(FMaterial*, float tf);
void fmaterial_set_ior(FMaterial*, float ior);
void fmaterial_set_texture(FMaterial*, FMatTexSemantic, FTexture*, Sampler*);

// Lights ======================================================================

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


// Session Configuration =======================================================

FConfig* fconfig_init();
void     fconfig_destroy(FConfig*);

void fconfig_set_title(FConfig*, char const*);
void fconfig_set_display(FConfig*, char const*);
void fconfig_set_device(FConfig*, int index);
void fconfig_set_screen(FConfig*, int w, int h);
void fconfig_set_log_debug(FConfig*, uint8_t);
void fconfig_set_fullscreen(FConfig*, uint8_t);
void fconfig_set_thread_count(FConfig*, uint8_t);

/// Enable off-axis mode using this screen plane.
void fconfig_set_offaxis_plane(FConfig*, FScreenPlane const*);

typedef enum FRenderer { R_METAL, R_OPENGL, R_VULKAN } FRenderer;
void fconfig_set_renderer(FConfig*, FRenderer renderer);

/// Eye selection for stereo rendering
typedef enum FEye { EYE_LEFT, EYE_RIGHT } FEye;

/// Set off-axis eye
void fconfig_set_stereo_eye(FConfig*, FEye);

// Features, provisional
void fconfig_set_ssao(FConfig*, bool);

// Session =====================================================================

/// Create a new backfill session
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
i32  fs_new_entity(FSession*);
void fs_destroy_entity(FSession*, i32 entity);

/// Add a renderable component to an entity. You may delete the mesh and
/// material after this call.
void fs_add_renderable(FSession*, i32 entity, FMesh*, FMaterial*);
void fs_del_renderable(FSession*, i32 entity);

/// Set the transform of an entity
void fs_set_transform(FSession*, i32 entity, mat4 const*);

/// Sets the parent, and CLEARS/overwrites the childs current transform if there
/// is one.
void fs_set_parent(FSession*, i32 child, i32 parent);

/// Extract current camera matrix info
void fs_debug_camera(FSession*, mat4* out_model, mat4* out_proj);
void fs_debug_camera_obj(FSession*, char const* file);

/// Add a light component to an entity. You may destroy or reuse the
/// configuration after this call.
void fs_add_light(FSession*, i32 entity, FLightConfig*);
void fs_del_light(FSession*, i32 entity);

// =============================================================================

#ifdef __cplusplus
}
#endif
