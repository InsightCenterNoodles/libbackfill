#include "renderstate.h"

#include <filament/Camera.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <utils/EntityManager.h>


RenderState::RenderState(FConfig const& config)
    : m_platform(config),
      m_engine(m_platform, config),
      m_renderer(m_engine, m_platform),
      m_manager(utils::EntityManager::get()) {

    m_scene = m_engine->createScene();

    m_main_camera = m_manager.create();

    m_camera = m_engine->createCamera(m_main_camera);

    m_camera->setExposure(16.0f, 1.0 / 125.0f, 100.0f);

    auto [width, height] = m_platform.frame_size();

    auto aspect_ratio = double(width) / height;

    m_camera->setProjection(
        45.0, aspect_ratio, 0.0625, 4096, filament::Camera::Fov::VERTICAL);

    m_camera->lookAt({ 15, 15, 15 }, { 0, 0, 0 }, { 0, 1, 0 });

    m_view = m_engine->createView();
    m_view->setViewport({ 0, 0, width, height });

    m_view->setScene(m_scene);
    m_view->setCamera(m_camera);
}

RenderState::~RenderState() {
    m_engine->destroy(m_view);
    m_engine->destroyCameraComponent(m_main_camera);
    m_engine->destroy(m_scene);
}
