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


        VkSurfaceKHR surface = nullptr;
        VkExtent2D   extent;

        if (!nativeWindow) {

#if defined(__linux__) && defined(FILAMENT_SUPPORTS_WAYLAND)
            wl* ptrval    = reinterpret_cast<wl*>(nativeWindow);
            extent.width  = ptrval->width;
            extent.height = ptrval->height;

            VkWaylandSurfaceCreateInfoKHR const createInfo = {
                .sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
                .pNext   = NULL,
                .flags   = 0,
                .display = ptrval->display,
                .surface = ptrval->surface,
            };
            VkResult const result = vkCreateWaylandSurfaceKHR(
                instance, &createInfo, VKALLOC, (VkSurfaceKHR*)&surface);
            FILAMENT_CHECK_POSTCONDITION(result == VK_SUCCESS)
                << "vkCreateWaylandSurfaceKHR error.";
#elif defined(LINUX_OR_FREEBSD) && defined(FILAMENT_SUPPORTS_X11)
            if (g_x11_vk.library == nullptr) {
                g_x11_vk.library = dlopen(LIBRARY_X11, RTLD_LOCAL | RTLD_NOW);
                FILAMENT_CHECK_PRECONDITION(g_x11_vk.library)
                    << "Unable to open X11 library.";
#    if defined(FILAMENT_SUPPORTS_XCB)
                g_x11_vk.xcbConnect =
                    (XCB_CONNECT)dlsym(g_x11_vk.library, "xcb_connect");
                int screen;
                g_x11_vk.connection = g_x11_vk.xcbConnect(nullptr, &screen);
#    endif
#    if defined(FILAMENT_SUPPORTS_XLIB)
                g_x11_vk.openDisplay =
                    (X11_OPEN_DISPLAY)dlsym(g_x11_vk.library, "XOpenDisplay");
                g_x11_vk.display = g_x11_vk.openDisplay(NULL);
                FILAMENT_CHECK_PRECONDITION(g_x11_vk.display)
                    << "Unable to open X11 display.";
#    endif
            }
#    if defined(FILAMENT_SUPPORTS_XCB) || defined(FILAMENT_SUPPORTS_XLIB)
            bool useXcb = false;
#    endif
#    if defined(FILAMENT_SUPPORTS_XCB)
#        if defined(FILAMENT_SUPPORTS_XLIB)
            useXcb = (flags & SWAP_CHAIN_CONFIG_ENABLE_XCB) != 0;
#        else
            useXcb = true;
#        endif
            if (useXcb) {
                FILAMENT_CHECK_POSTCONDITION(vkCreateXcbSurfaceKHR)
                    << "Unable to load vkCreateXcbSurfaceKHR function.";

                VkXcbSurfaceCreateInfoKHR const createInfo = {
                    .sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
                    .connection = g_x11_vk.connection,
                    .window =
                        (xcb_window_t) reinterpret_cast<uint64_t>(nativeWindow),
                };
                VkResult const result = vkCreateXcbSurfaceKHR(
                    instance, &createInfo, VKALLOC, (VkSurfaceKHR*)&surface);
                FILAMENT_CHECK_POSTCONDITION(result == VK_SUCCESS)
                    << "vkCreateXcbSurfaceKHR error="
                    << static_cast<int32_t>(result);
            }
#    endif
#    if defined(FILAMENT_SUPPORTS_XLIB)
            if (!useXcb) {
                FILAMENT_CHECK_POSTCONDITION(vkCreateXlibSurfaceKHR)
                    << "Unable to load vkCreateXlibSurfaceKHR function.";

                VkXlibSurfaceCreateInfoKHR const createInfo = {
                    .sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
                    .dpy    = g_x11_vk.display,
                    .window = (Window)nativeWindow,
                };
                VkResult const result = vkCreateXlibSurfaceKHR(
                    instance, &createInfo, VKALLOC, (VkSurfaceKHR*)&surface);
                FILAMENT_CHECK_POSTCONDITION(result == VK_SUCCESS)
                    << "vkCreateXlibSurfaceKHR error="
                    << static_cast<int32_t>(result);
            }
#    endif
#endif

            if (!surface) {
                spdlog::error("No custom surface fpr this platform");
                // There is no way we can continue
                abort();
            }


            return std::make_tuple(surface, extent);
        }


        bool vkok =
            SDL_Vulkan_CreateSurface(m_window, instance, nullptr, &surface);

        if (!vkok or !surface) {
            std::string error = SDL_GetError();

            spdlog::error("Unable to create vulkan context: {}", error);
            // There is no way we can continue
            abort();
        }

        int32_t width, height;
        SDL_GetWindowSizeInPixels(m_window, &width, &height);

        extent.width  = width;
        extent.height = height;

        return std::make_tuple(surface, extent);
    }

    ExtensionSet getSwapchainInstanceExtensions() const override {
        VulkanPlatform::ExtensionSet const ret = {
#if defined(__linux__) && defined(FILAMENT_SUPPORTS_WAYLAND)
            VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#elif defined(LINUX_OR_FREEBSD) && defined(FILAMENT_SUPPORTS_X11)
#    if defined(FILAMENT_SUPPORTS_XCB)
            VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#    endif
#    if defined(FILAMENT_SUPPORTS_XLIB)
            VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#    endif
#endif
#ifdef __APPLE__
            VK_EXT_METAL_SURFACE_EXTENSION_NAME,
#endif
        };


        return ret;
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
