#include "localrenderer.h"

#include <filament/SwapChain.h>

#include <unistd.h>


LocalRenderer::LocalRenderer(LocalEngine const& le, LocalPlatform const& lp) {
    m_engine = le;

    auto swap_flags = filament::SwapChain::CONFIG_HAS_STENCIL_BUFFER;

    spdlog::debug("{} Create initial swapchain", getpid());
    m_swap_chain = m_engine->createSwapChain(lp.native_window(), swap_flags);

    m_renderer = m_engine->createRenderer();
}

LocalRenderer::~LocalRenderer() {
    m_engine->destroy(m_renderer);
    m_engine->destroy(m_swap_chain);
}

void LocalRenderer::rebuild_swapchain(LocalPlatform const& lp) {
    // old should always exist
    expect(!!m_swap_chain, "Old swapchain expired");

    m_engine->destroy(m_swap_chain);

    auto swap_flags = filament::SwapChain::CONFIG_HAS_STENCIL_BUFFER;

    spdlog::debug("{} Create new swapchain", getpid());
    m_swap_chain = m_engine->createSwapChain(lp.native_window(), swap_flags);
}
