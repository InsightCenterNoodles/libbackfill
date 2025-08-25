#pragma once

#include <math/vec3.h>

#include <optional>
#include <string>

struct ScreenDesc {
    filament::math::double3 lower_left;
    filament::math::double3 lower_right;
    filament::math::double3 upper_right;
};


struct FConfig {
    std::string        title;
    std::string        display;
    std::optional<int> device = std::nullopt;

    int w = 1024;
    int h = 768;

    bool log_debug = false;

    std::optional<ScreenDesc> screen_info;
};
