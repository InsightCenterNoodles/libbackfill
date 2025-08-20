#pragma once

#include "config.h"

#include <filament/Camera.h>

#include <math/TVecHelpers.h>
#include <math/mat4.h>
#include <math/norm.h>

namespace proj {

void compute_off_axis_projection(ScreenDesc const&              screen_desc,
                                 filament::math::double3 const& head_pos,
                                 filament::math::quat const&    head_rot,
                                 bool                           left_eye,
                                 float                          near,
                                 float                          far,
                                 filament::Camera*              camera);

} // namespace proj
