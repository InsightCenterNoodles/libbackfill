#pragma once
#include <span>

namespace generated {
std::span<const unsigned char> get_primarylit_matbin();
std::span<const unsigned char> get_primaryinstancelit_matbin();
std::span<const unsigned char> get_bakedcolor_matbin();
}
