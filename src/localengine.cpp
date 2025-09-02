#include "localengine.h"

#include <backend/platforms/VulkanPlatform.h>


#ifdef __APPLE__ß
constexpr bool is_apple = true;
#else
constexpr bool is_apple = false;
#endif

/// Allow the selection of vulkan adapters
class CustomVulkanPlatform : public filament::backend::VulkanPlatform {
    VulkanPlatform::Customization m_customization;

public:
    CustomVulkanPlatform(int device_index) {
        VulkanPlatform::Customization::GPUPreference pref;

        pref.index = std::clamp(device_index, -1, 255);

        m_customization = { .gpu = pref };
    }

    virtual VulkanPlatform::Customization
    getCustomization() const noexcept override {
        return m_customization;
    }
};

LocalEngine::LocalEngine(FConfig const& config) {
    auto backend = is_apple ? filament::backend::Backend::METAL
                            : filament::backend::Backend::VULKAN;

    // Can use the engine config system to add in stereo
    // This could be interesting if we use HW stereo to projectors.

    auto builder = filament::Engine::Builder().backend(backend).featureLevel(
        filament::backend::FeatureLevel::FEATURE_LEVEL_3);


    if (backend == filament::backend::Backend::VULKAN &&
        config.device.has_value()) {

        m_custom_backend = new CustomVulkanPlatform(*config.device);

        builder.platform(m_custom_backend);
    }

    m_pointer = builder.build();
    expect(!!m_pointer, "Unable to initialize engine");
}

LocalEngine::~LocalEngine() {
    filament::Engine::destroy(m_pointer);
    if (m_custom_backend) delete m_custom_backend;
}
