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

    // Camera Pose
    const mat4f H = mat4f::translation((float3)head_pos);

    // +/- IPD along camera-right (from H’s rotation columns)
    const float  ipd  = 0.064f;
    const float  half = (left_eye ? 0.5f : -0.5f) * ipd;
    const float3 rCam =
        normalize(
            mat4f::project(
                mat4f(head_rot), 
                float3 { H[0].x, H[0].y, H[0].z }
            )
        ); // camera right in world

    const mat4f H_eye = mat4f::translation(half * rCam) * H;
    camera->setModelMatrix(H_eye);

    // Eye/world
    // const mat4f  V    = inverse(H_eye);
    const float3 eyeW = { H_eye[3].x, H_eye[3].y, H_eye[3].z };

    // Screen geometry and basis (vr, vu, vn) in WORLD space
    const float3 LL = (float3)screen_desc.lower_left;
    const float3 LR = (float3)screen_desc.lower_right;
    const float3 UR = (float3)screen_desc.upper_right;
    const float3 UL = LL + (UR - LR); // We only support planes

    float3 vr = normalize(LR - LL);       // screen-right
    float3 vu = normalize(UL - LL);       // screen-up
    float3 vn = normalize(cross(vr, vu)); // screen normal

    // Ensure normal faces the eye
    if (dot(vn, eyeW - LL) < 0.0f) vn = -vn;

    // Vectors from eye to three corners, WORLD space
    const float3 va = LL - eyeW;
    const float3 vb = LR - eyeW;
    const float3 vc = UL - eyeW;

    // Distances and projections in the screen basis
    const float d = dot(va, vn); // > 0 if eye is in front of screen
    const float l = dot(vr, va) * near / d;
    const float r = dot(vr, vb) * near / d;
    const float b = dot(vu, va) * near / d;
    const float t = dot(vu, vc) * near / d;

    // Build a frustum IN SCREEN-ORIENTED EYE SPACE
    const mat4f F = mat4f::frustum(l, r, b, t, near, far);

    // Align current camera eye space to screen-oriented eye space
    const float3 camX =
        normalize(float3 { H_eye[0].x, H_eye[0].y, H_eye[0].z });
    const float3 camY =
        normalize(float3 { H_eye[1].x, H_eye[1].y, H_eye[1].z });
    const float3 camZ =
        normalize(float3 { H_eye[2].x, H_eye[2].y, H_eye[2].z });

    vr = -vr;
    vu = -vu;

    // Rows of S are the screen basis dotted with camera axes.
    const mat4f S =
        mat4f { float4 { dot(vr, camX), dot(vr, camY), dot(vr, camZ), 0.0f },
                float4 { dot(vu, camX), dot(vu, camY), dot(vu, camZ), 0.0f },
                float4 { dot(vn, camX), dot(vn, camY), dot(vn, camZ), 0.0f },
                float4 { 0.0f, 0.0f, 0.0f, 1.0f } };

    // Final projection expects camera-space, aligns to screen, then applies
    // frustum.
    const mat4f P = F * S;

    camera->setCustomProjection(mat4(P), near, far);
}

/*
void compute_off_axis_projection(ScreenDesc const& screen_desc,
                                 double3 const&    head_pos,
                                 quat const&       head_rot,
                                 bool              left_eye,
                                 float             near,
                                 float             far,
                                 filament::Camera* camera) {
    assert(near > 0 && far > near);

 auto H = mat4(head_rot) * mat4::translation(head_pos);

 const float  ipd       = 0.064f; // typical 64 mm; tune as needed
 const float  half      = (left_eye ? -0.5f : +0.5f) * ipd;
 const float3 cam_right = normalize(float3 { H[0].x, H[0].y, H[0].z });

 auto H_eye = mat4f::translation(half * cam_right) * H;

 // Set the camera transform
 camera->setModelMatrix(H_eye);

 // Build off-axis projection from world to eye-space corners:
 auto H_inv = inverse(H_eye);

 // Screen corners in world space

 float3 upper_left =
     ((float3)screen_desc.upper_right - (float3)screen_desc.lower_right) +
     (float3)screen_desc.lower_left;

 float4 LLw = {
     screen_desc.lower_left.x,
     screen_desc.lower_left.y,
     screen_desc.lower_left.z,
     1.0f,
 };
 float4 LRw = {
     screen_desc.lower_right.x,
     screen_desc.lower_right.y,
     screen_desc.lower_right.z,
     1.0f,
 };
 float4 ULw = {
     upper_left.x,
     upper_left.y,
     upper_left.z,
     1.0f,
 };
 float4 URw = {
     screen_desc.upper_right.x,
     screen_desc.upper_right.y,
     screen_desc.upper_right.z,
     1.0f,
 };

 // Transform to eye space
 auto LLe = H_inv * LLw;
 auto LRe = H_inv * LRw;
 auto ULe = H_inv * ULw;
 auto URe = H_inv * URw;

 // Compute l/r/b/t in eye space at the near plane
 auto toNear = [&](float4 p) {
     float z = -p.z;
     constexpr float eps = 1e-5f;
     if (std::abs(z) < eps) { z = eps; }
     float s = near / z;
     return float2 { p.x * s, p.y * s };
 };

 auto LLn = toNear(LLe);
 auto LRn = toNear(LRe);
 auto ULn = toNear(ULe);
 auto URn = toNear(URe);

 // float left   = std::min(Ln.x, Rn.x);
 // float right  = std::max(Ln.x, Rn.x);
 // float bottom = std::min(Ln.y, Un.y);
 // float top    = std::max(Ln.y, Un.y);

 float left   = std::min({ LLn.x, LRn.x, ULn.x, URn.x });
 float right  = std::max({ LLn.x, LRn.x, ULn.x, URn.x });
 float bottom = std::min({ LLn.y, LRn.y, ULn.y, URn.y });
 float top    = std::max({ LLn.y, LRn.y, ULn.y, URn.y });

 // Build a standard frustum matrix from l/r/b/t/n/f
 auto P = mat4::frustum(left, right, bottom, top, near, far);

 camera->setCustomProjection(P, near, far);
}
 */

} // namespace proj
