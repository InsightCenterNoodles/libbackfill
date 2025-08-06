#include "generated.h"
#include <cstddef>

namespace generated {
static constexpr unsigned char PRIMARYLIT_MATBIN[] = { 
#embed "../assets/generated/primaryLit.matbin"
};
std::span<const unsigned char> get_primarylit_matbin() { return PRIMARYLIT_MATBIN; }
static constexpr unsigned char BAKEDCOLOR_MATBIN[] = { 
#embed "../assets/generated/bakedColor.matbin"
};
std::span<const unsigned char> get_bakedcolor_matbin() { return BAKEDCOLOR_MATBIN; }
}
