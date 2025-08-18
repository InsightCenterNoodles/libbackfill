#pragma once


#include "localengine.h"
#include "localplatform.h"
#include "localrenderer.h"

#include <utils/Entity.h>


class RenderState {
    LocalPlatform         m_platform;
    LocalEngine           m_engine;
    LocalRenderer         m_renderer;
    utils::EntityManager& m_manager;

    filament::Scene* m_scene = nullptr;

    utils::Entity     m_main_camera;
    filament::Camera* m_camera = nullptr;

    filament::View* m_view = nullptr;

public:
    DISABLE_MOVE_COPY(RenderState);

    RenderState(Config const& config);

    ~RenderState();

    LocalPlatform const&  platform() { return m_platform; };
    LocalEngine const&    engine() { return m_engine; };
    LocalRenderer const&  renderer() { return m_renderer; };
    utils::EntityManager& manager() { return m_manager; };
    filament::Scene*      scene() { return m_scene; }
    filament::View*       view() { return m_view; }
    filament::Camera*     camera() { return m_camera; }
};
