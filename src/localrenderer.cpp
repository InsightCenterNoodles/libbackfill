#include "localrenderer.h"

#include <filament/SwapChain.h>


LocalRenderer::LocalRenderer(LocalEngine const& le, LocalPlatform const& lp) {
    m_engine = le;

    auto swap_flags = filament::SwapChain::CONFIG_HAS_STENCIL_BUFFER;

    m_swap_chain = m_engine->createSwapChain(lp.native_window(), swap_flags);

    m_renderer = m_engine->createRenderer();
}

LocalRenderer::~LocalRenderer() {
    m_engine->destroy(m_renderer);
    m_engine->destroy(m_swap_chain);
}
