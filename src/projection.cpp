#include "projection.h"

#include <math/TQuatHelpers.h>


using namespace filament::math;

// Addition for quat * vec4
template <typename T, typename U>
inline constexpr details::TVec4<details::arithmetic_result_t<T, U>>
operator*(const details::TQuaternion<T>& q, const details::TVec4<U>& v4) {
    using R = details::arithmetic_result_t<T, U>;
    // Rotate the xyz part via q * p * inverse(q), with p = (v, 0).
    const auto rotated =
        imaginary(q * details::TQuaternion<U>(v4.xyz, U(0)) * inverse(q));
    return details::TVec4<R>(rotated, R(v4.w)); // keep w as-is
}


namespace proj {

ScreenDesc& get_screen_desc() {
    static ScreenDesc ret;

    return ret;
}

void init(ScreenDesc screen_info) {
    get_screen_desc() = screen_info;
}

mat4 compute_off_axis_projection(mat4 const&    world_to_screen_matrix,
                                 double3 const& position,
                                 quat const&    orientation,
                                 bool           left_eye,
                                 float          near,
                                 float          far) {
    assert(near > 0);
    assert(far > 0);

    ScreenDesc desc = get_screen_desc();

    double4 E(0, 0, 0, 1.0);
    double4 L(desc.lower_left, 1.0);
    double4 H(desc.upper_right, 1.0);


    const float iod     = 0.06;
    const float eye_sep = (left_eye ? iod : -iod);

    E[0] += eye_sep / 2.0;

    E = orientation * E;
    E = double4(position, 0) + E;

    E = world_to_screen_matrix * E;
    H = world_to_screen_matrix * H;
    L = world_to_screen_matrix * L;

    const float width  = H[0] - L[0];
    const float height = H[1] - L[1];


    const float F = E[2] - far;
    const float B = E[2] - near;

    const float depth = B - F;

    double4 c0 = double4((2.0 * E[2]) / width, 0, 0, 0);
    double4 c1 = double4(0, (2.0 * E[2]) / height, 0, 0);
    double4 c2 = double4((H[0] + L[0] - 2 * E[0]) / width,
                         (H[1] + L[1] - 2 * E[1]) / height,
                         (B + F - 2 * E[2]) / depth,
                         -1);

    double4 c3 = double4((-E[2] * (H[0] + L[0])) / width,
                         (-E[2] * (H[1] + L[1])) / height,
                         B - E[2] - (B * (B + F - 2 * E[2]) / depth),
                         E[2]);

    auto projection = mat4 { c0, c1, c2, c3 };

    return projection;
}

mat4 compute_world_to_screen_matrix() {
    ScreenDesc desc = get_screen_desc();

    auto x_axis = normalize(desc.lower_right - desc.lower_left);

    auto y_axis = normalize(desc.upper_right - desc.lower_right);

    auto z_axis = normalize(cross(x_axis, y_axis));

    auto world_to_screen_matrix = mat4 {
        double4(x_axis, 0),
        double4(y_axis, 0),
        double4(z_axis, 0),
        double4(desc.lower_left, 1),
    };

    world_to_screen_matrix = inverse(world_to_screen_matrix);

    return world_to_screen_matrix;
}

} // namespace proj
