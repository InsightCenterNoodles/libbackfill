#pragma once

#include "config.h"

#include <math/TVecHelpers.h>
#include <math/mat4.h>
#include <math/norm.h>

namespace proj {

void init(ScreenDesc);

filament::math::mat4
compute_off_axis_projection(filament::math::mat4 const& world_to_screen_matrix,
                            filament::math::double3 const& position,
                            filament::math::quat const&    orientation,
                            bool                           left_eye,
                            float                          near,
                            float                          far);

filament::math::mat4 compute_world_to_screen_matrix();

} // namespace proj
