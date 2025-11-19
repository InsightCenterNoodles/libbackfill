# Backfill

Backfill is a small, single‑threaded C API for real‑time physically‑based 
rendering for immersive environments built on [Google Filament](https://github.com/google/filament). 
It exposes a minimal entity/component model, PBR materials, 
image/texture utilities, environment lighting, off‑axis and stereo projection, 
and an application to demonstrate the API.

Status: early (0.1)
- The API may evolve. 
- All calls are not thread‑safe.

## Features

- Simple C API with reference‑counted objects
- Filament backends: Metal, OpenGL, Vulkan
- Entities with renderable and light components; transforms and parenting
- PBR materials with common texture semantics (base color, normal, occlusion, emissive, metal/roughness, clearcoat)
- Image decode for EXR/HDR/PNG/JPEG with color‑space probing
    - Raw pixel upload path for interfacing engines
- Textures with sampler control (filters, wrap, anisotropy)
- Environment light from equirectangular HDR; skybox support
- Off‑axis and stereo projection for immersive displays
- Demo app to exercise textures, materials, lights, and IBL

## Requirements

- Build tools: CMake >= 3.24, clang/clang++ (required)
  - Clang is required by Filament, this might be relaxed later.
- No manual third‑party setup required: Filament, SDL3, spdlog, stb, and magic_enum are fetched via CMake/CPM
- Platforms
  - macOS: Metal (default)
  - Linux: X11 + OpenGL/Vulkan
    - Wayland is not supported at this time 
  - Windows: Experimental! Not tested!

## Build and Install

### Option A: helper script (recommended)

The script installs to `~/.local` by default. Optionally can install to a global location.

```bash
./deploy.py --dest local
```

Useful options:

- `--prefix /custom/prefix` (overrides destination)
- `--build-type Release` (or Debug)
- `-G Ninja` (choose generator)
- `--extra-cmake -DKEY=VALUE ...` (pass extra cache entries)

### Option B: manual CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cmake --install build --prefix ~/.local
```

Installed files:

- Library: `libbackfill.{dylib,so,dll}`
- Headers: `include/backfill/*.h`
- Pkg‑config file: `backfill.pc`

## Using the Library

### Pkg‑config (CLI)

```bash
cflags=$(pkg-config --cflags backfill)
libs=$(pkg-config --libs backfill)
clang++ $cflags -o app app.cpp $libs
```

### Pkg‑config (CMake)

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(BACKFILL REQUIRED backfill)

add_executable(app main.cpp)
target_include_directories(app PRIVATE ${BACKFILL_INCLUDE_DIRS})
target_link_libraries(app PRIVATE ${BACKFILL_LINK_LIBRARIES})
```

## Run the Demo

After building:

```bash
build/backfill_demo -a assets/tex -i assets/ibl/workshop_4k_small.exr
```

Flags:

- `-a <dir>`: root for base/normal/RM textures (e.g., MetalPlates… files)
- `-i <file>`: equirectangular env map (.exr/.hdr/.png/.jpg)
- `-m`: export current camera frustum (+ optional off‑axis screen) to `camera.obj` and exit

Example assets are under `assets/tex` and `assets/ibl`.

## API Quickstart (C)

```c
#include <backfill/api.h>

int main(void) {
    FConfig* cfg = fconfig_init();
    fconfig_set_title(cfg, "Backfill Window");
    // Choose a backend when needed, e.g. Vulkan on Linux:
    // fconfig_set_renderer(cfg, R_VULKAN);
    // fconfig_set_device(cfg, 0); // optional GPU index for Vulkan
    fconfig_set_screen(cfg, 1280, 720);

    FSession* s = fs_init(cfg);
    fconfig_destroy(cfg);

    // Solid sky as a quick start
    fs_set_skybox_color(s, (FColor){1.f, 1.f, 1.f, 1.f});

    // Main loop: process events and render frames
    while (fs_frame(s)) {
        float3 head_pos = { 0.f, 1.5f, 5.f };
        float4 head_rot = { 0.f, 0.f, 0.f, 1.f };
        fs_update_head(s, head_pos, head_rot);
    }

    fs_destroy(s);
    return 0;
}
```

### Entities, meshes, and materials

High‑level flow:

1. Create a mesh from vertex/index blobs: `fmesh_init(...)`
2. Create a material from a `FMaterialConfig` (set options/textures) and `fmaterial_init(...)`
3. Create an entity, add the renderable, and set a transform

Key entry points:

- Packing convenience: `pack_vertex_u16` / `pack_vertex_u32`
- Mesh: `fmesh_init`, `fmesh_release`
- Materials: `fmaterialconfig_init`, `fmc_set_option`, `fmc_set_texture`, `fmaterial_init`, `fmaterial_set_*`
- Lights: `flightconfig_init` (POINT/SPOT/DIRECTIONAL/SUN), `fs_add_light`
- Environment/skybox: `fenv_light_init_equirect`, `fs_set_environment_light`, `fs_set_skybox_color`

### Images and textures

- File decode: `fimg_probe`, `fimg_init_decode_file`
- Raw pixels: `fimg_init_raw` with `FImageRawDesc`
- Texture creation: `ftex_config_init` → `ftex_init`
- Sampler: `fsamp_init`, `fsamp_set_mag/min/wrap/aniso`

### Off‑axis and stereo

For proper off-axis rendering, a render surface plane should be specified.

```c
FScreenPlane plane = {
    .lower_left  = { -1.0, -1.0, 0.0 },
    .lower_right = {  1.0, -1.0, 0.0 },
    .upper_right = {  1.0,  1.0, 0.0 },
};
fconfig_set_offaxis_plane(cfg, &plane);
fconfig_set_stereo_eye(cfg, EYE_LEFT); // or EYE_RIGHT
```

Call `fs_update_head` to set the viewpoint.

## Object Lifetime

The API uses reference counting. Functions named `*_init*` or `*_acquire` 
increase references; `*_release`/`*_destroy` decrease them. 
Always release/destroy what you create.

## Project Layout

- Public API: `include/backfill/api.h`
- Demo: `examples/demo.cpp`
- Build configuration: `CMakeLists.txt`
- Installer script: `deploy.py`

## License

See repository for license information.

> Included assets modified from [AmbientCG](https://ambientcg.com)

