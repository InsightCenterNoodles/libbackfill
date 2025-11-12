#include "projection.h"

#include <math/TQuatHelpers.h>

#include <spdlog/spdlog.h>

#include <cassert>


using namespace filament::math;

namespace proj {

void compute_off_axis_projection(ScreenDesc const& screen_desc,
                                 double3 const&    head_pos,
                                 quat const&       head_rot,
                                 bool              left_eye,
                                 float             near,
                                 float             far,
                                 filament::Camera* camera) {
    using namespace filament::math;
    assert(near > 0.0f && far > near);

    // 1) Head pose and stereo offset (world space)
    const mat4f H = mat4f::translation((float3)head_pos);

    const float  ipd  = 0.064f;
    const float  half = (left_eye ? 0.5f : -0.5f) * ipd;
    const float3 rCam = mat4f::project(mat4f(head_rot),normalize(float3 { H[0].x, H[0].y, H[0].z }));

    const mat4f  H_eye = mat4f::translation(half * rCam) * H;
    const float3 eyeW  = float3 { H_eye[3].x, H_eye[3].y, H_eye[3].z };

    // 2) Screen plane in world
    const float3 LL = (float3)screen_desc.lower_left;
    const float3 LR = (float3)screen_desc.lower_right;
    const float3 UR = (float3)screen_desc.upper_right;
    const float3 UL = LL + (UR - LR); // planar

    float3 vr = normalize(LR - LL); // screen right
    float3 vu = normalize(UL - LL); // screen up
    float3 vn =
        normalize(cross(vr, vu)); // screen normal (points out of screen)

    // Ensure normal faces the eye
    if (dot(vn, eyeW - LL) < 0.0f) vn = -vn;

    // 3) Build a VIEW whose axes align to the screen
    const float3 camX = -vr;
    const float3 camY = -vu;
    const float3 camZ = vn;

    // Column-major: columns are basis vectors and translation
    const mat4f V_worldFromEye = mat4f { float4 { camX, 0.0f },
                                         float4 { camY, 0.0f },
                                         float4 { camZ, 0.0f },
                                         float4 { eyeW, 1.0f } };
    camera->setModelMatrix(V_worldFromEye);

    // 4) Compute asymmetric frustum in *this* eye space
    const float3 va = LL - eyeW;
    const float3 vb = LR - eyeW;
    const float3 vc = UL - eyeW;

    const float d = dot(va, vn); // distance along normal (>0)
    // Project to near plane using the screen basis
    const float l = dot(vr, va) * near / d;
    const float r = dot(vr, vb) * near / d;
    const float b = dot(vu, va) * near / d;
    const float t = dot(vu, vc) * near / d;

    // 5) Use a *plain* asymmetric projection (no extra S)
    const mat4f P = mat4f::frustum(l, r, b, t, near, far);
    camera->setCustomProjection(mat4(P), near, far);
}

} // namespace proj
