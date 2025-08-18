#include "localengine.h"


#ifdef __APPLE__
constexpr bool is_apple = true;
#else
constexpr bool is_apple = false;
#endif

LocalEngine::LocalEngine() {
    auto backend = is_apple ? filament::backend::Backend::METAL
                            : filament::backend::Backend::VULKAN;

    // Can use the engine config system to add in stereo
    // This could be interesting if we use HW stereo to projectors.

#if 0
      // set GPU
        if (backend == filament::backend::Backend::VULKAN &&
            config.device.has_value()) {
            filament::backend::Platform::
        }
        VulkanPlatform::Customization::GPUPreference pref;
        // Check to see if it is an integer, if so turn it into an index.
        if (std::all_of(gpuHint.begin(), gpuHint.end(), ::isdigit)) {
            char* p_end {};
            pref.index =
                static_cast<int8_t>(std::strtol(gpuHint.c_str(), &p_end, 10));
        } else {
            pref.deviceName = gpuHint;
        }
        mCustomization = { .gpu = pref };

#endif
    m_pointer =
        filament::Engine::Builder()
            .backend(backend)
            .featureLevel(filament::backend::FeatureLevel::FEATURE_LEVEL_3)
            .build();
    expect(!!m_pointer, "Unable to initialize engine");
}

LocalEngine::~LocalEngine() {
    filament::Engine::destroy(m_pointer);
}
