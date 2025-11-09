#include "localengine.h"

#include "localplatform.h"

#include <backend/platforms/VulkanPlatform.h>

#include <SDL3/SDL_vulkan.h>

/// Allow the selection of vulkan adapters
class CustomVulkanPlatform : public filament::backend::VulkanPlatform {
    SDL_Window*                   m_window;
    VulkanPlatform::Customization m_customization;

public:
    CustomVulkanPlatform(SDL_Window* window, std::optional<int> device_index)
        : m_window(window) {
        VulkanPlatform::Customization::GPUPreference pref;

        if (device_index.has_value()) {
            pref.index = std::clamp(*device_index, -1, 255);
            spdlog::debug("Using device at index {}", pref.index);
            m_customization.gpu = pref;
        }
    }

    VulkanPlatform::Customization getCustomization() const noexcept override {
        return m_customization;
    }

    ExtensionSet getRequiredInstanceExtensions() override {
        uint32_t           instance_count = 0;
        const char* const* exts =
            SDL_Vulkan_GetInstanceExtensions(&instance_count);

        if (!exts) {
            spdlog::critical("Unable to discover required instance extensions");
        }

        ExtensionSet ret;

        for (uint32_t i = 0; i < instance_count; i++) {
            ret.insert(utils::CString(exts[i]));
        }

        return ret;
    }

    SurfaceBundle createVkSurfaceKHR(void*      nativeWindow,
                                     VkInstance instance,
                                     uint64_t   flags) const noexcept override {
        // if null, delegate to superior
        if (!nativeWindow) {
            return filament::backend::VulkanPlatform::createVkSurfaceKHR(
                nativeWindow, instance, flags);
        }

        VkSurfaceKHR surface;
        VkExtent2D   extent;

        SDL_Vulkan_CreateSurface(m_window, instance, nullptr, &surface);

        int32_t width, height;
        SDL_GetWindowSizeInPixels(m_window, &width, &height);

        extent.width  = width;
        extent.height = height;

        return std::make_tuple(surface, extent);
    }
};

LocalEngine::LocalEngine(LocalPlatform const& platform, FConfig const& config) {

    auto backend = config.renderer;

    // Can use the engine config system to add in stereo
    // This could be interesting if we use HW stereo to projectors.

    auto builder = filament::Engine::Builder().backend(backend).featureLevel(
        filament::backend::FeatureLevel::FEATURE_LEVEL_3);


    if (backend == filament::backend::Backend::VULKAN) {

        m_custom_backend =
            new CustomVulkanPlatform(platform.window_pointer(), config.device);

        builder.platform(m_custom_backend);
    }

    filament::Engine::Config engine_config;

    engine_config.jobSystemThreadCount = config.thread_count;

    builder.config(&engine_config);

    m_pointer = builder.build();
    expect(!!m_pointer, "Unable to initialize engine");
}

LocalEngine::~LocalEngine() {
    filament::Engine::destroy(m_pointer);
    if (m_custom_backend) delete m_custom_backend;
}
