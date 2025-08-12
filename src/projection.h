#pragma once

#include <math/TVecHelpers.h>
#include <math/mat4.h>
#include <math/norm.h>

filament::math::mat4
compute_off_axis_projection(filament::math::mat4 const& world_to_screen_matrix,
                            filament::math::double3 const& position,
                            filament::math::quat const&    orientation,
                            bool                           left_eye,
                            float                          near,
                            float                          far);

void update_world_to_screen_matrix(
    filament::math::mat4& world_to_screen_matrix);
