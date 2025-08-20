#pragma once

#include "config.h"
#include "utility.h"

class SDL_Window;

class LocalPlatform {
    SDL_Window* m_window_pointer;
    void*       m_native_window;

public:
    DISABLE_MOVE_COPY(LocalPlatform);

    LocalPlatform(FConfig const& config);

    ~LocalPlatform();

    void* native_window() const { return m_native_window; }

    std::array<uint32_t, 2> frame_size() const;
};
