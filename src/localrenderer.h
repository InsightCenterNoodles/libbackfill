#pragma once

#include "localengine.h"
#include "localplatform.h"
#include "utility.h"


namespace filament {
class Engine;
class SwapChain;
class Renderer;
} // namespace filament


class LocalRenderer {
    filament::Engine*    m_engine;
    filament::SwapChain* m_swap_chain;
    filament::Renderer*  m_renderer;

public:
    DISABLE_MOVE_COPY(LocalRenderer);

    LocalRenderer(LocalEngine const& le, LocalPlatform const& lp);

    ~LocalRenderer();

    void rebuild_swapchain(LocalPlatform const& lp);

    filament::SwapChain* swap_chain() const { return m_swap_chain; }
    filament::Renderer*  renderer() const { return m_renderer; }
};
