#pragma once

#include <optional>
#include <string>

struct Config {
    std::string        title;
    std::string        display;
    std::optional<int> device = std::nullopt;

    int w = 1024;
    int h = 768;
};
