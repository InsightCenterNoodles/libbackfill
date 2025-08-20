#include "projection.h"

#include <math/TQuatHelpers.h>


using namespace filament::math;

namespace proj {


void compute_off_axis_projection(ScreenDesc const& screen_desc,
                                 double3 const&    head_pos,
                                 quat const&       head_rot,
                                 bool              left_eye,
                                 float             near,
                                 float             far,
                                 filament::Camera* camera) {
    assert(near > 0);
    assert(far > 0);

    auto H = mat4(head_rot) * mat4::translation(head_pos);

    float iod = left_eye ? 0.06f : -0.06f;

    auto cam_right = normalize(float3 { H[0].x, H[0].y, H[0].z });

    auto H_eye = mat4f::translation(+0.5f * iod * cam_right) * H;

    // Set the camera transform
    camera->setModelMatrix(H_eye);

    // Build off-axis projection from world to eye-space corners:
    auto H_inv = inverse(H_eye);

    // Screen corners in world space

    float4 Lw = {
        screen_desc.lower_left.x,
        screen_desc.lower_left.y,
        screen_desc.lower_left.z,
        1.0f,
    };
    float4 Rw = {
        screen_desc.lower_right.x,
        screen_desc.lower_right.y,
        screen_desc.lower_right.z,
        1.0f,
    };
    float4 Uw = {
        screen_desc.upper_right.x,
        screen_desc.upper_right.y,
        screen_desc.upper_right.z,
        1.0f,
    };

    // Transform to eye space
    auto Le = H_inv * Lw;
    auto Re = H_inv * Rw;
    auto Ue = H_inv * Uw;

    // Compute l/r/b/t in eye space at the near plane
    auto toNear = [&](float4 p) {
        float z = -p.z;
        float s = near / z;
        return float2 { p.x * s, p.y * s };
    };

    auto Ln = toNear(Le);
    auto Rn = toNear(Re);
    auto Un = toNear(Ue);

    float left   = std::min(Ln.x, Rn.x);
    float right  = std::max(Ln.x, Rn.x);
    float bottom = std::min(Ln.y, Un.y);
    float top    = std::max(Ln.y, Un.y);

    // Build a standard frustum matrix from l/r/b/t/n/f
    auto P = mat4::frustum(left, right, bottom, top, near, far);

    camera->setCustomProjection(P, near, far);
}

} // namespace proj
