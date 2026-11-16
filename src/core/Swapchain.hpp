#pragma once

#include "core/Image.hpp"

#include <vulkan/vulkan.h>
#include <vector>
#include <utility>

class Context;
class Window;

/**
 * @brief Informazioni sulle capacità e formati supportati per la swapchain.
 */
struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

/**
 * @brief Gestisce il ciclo di vita della Swapchain (Double/Triple Buffering),
 * delle immagini a video, delle VkImageView e del Depth Buffer associato.
 *
 * Principio: High Cohesion nella gestione della presentazione e del buffering delle immagini.
 * Low Coupling: riceve il Context e la Window senza dipendere da logiche di shader o render pass.
 */
class Swapchain {
public:
    Swapchain(const Context& context, const Window& window);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    Swapchain(Swapchain&& other) noexcept;
    Swapchain& operator=(Swapchain&& other) noexcept;

    [[nodiscard]] VkSwapchainKHR getHandle() const noexcept { return m_swapchain; }
    [[nodiscard]] VkFormat getImageFormat() const noexcept { return m_imageFormat; }
    [[nodiscard]] VkFormat getDepthFormat() const noexcept { return m_depthFormat; }
    [[nodiscard]] VkExtent2D getExtent() const noexcept { return m_extent; }
    [[nodiscard]] VkPresentModeKHR getPresentMode() const noexcept { return m_presentMode; }
    [[nodiscard]] const char* getPresentModeName() const noexcept;
    [[nodiscard]] size_t getImageCount() const noexcept { return m_images.size(); }
    [[nodiscard]] const std::vector<VkImage>& getImages() const noexcept { return m_images; }
    [[nodiscard]] VkImage getImage(size_t index) const { return m_images.at(index); }
    [[nodiscard]] const std::vector<VkImageView>& getImageViews() const noexcept { return m_imageViews; }
    [[nodiscard]] VkImageView getImageView(size_t index) const { return m_imageViews.at(index); }
    [[nodiscard]] VkImage getDepthImage() const noexcept { return m_depthImage.getHandle(); }
    [[nodiscard]] VkImageView getDepthImageView() const noexcept { return m_depthImage.getView(); }

    /**
     * @brief Acquisisce l'indice della prossima immagine disponibile per il rendering.
     */
    VkResult acquireNextImage(VkSemaphore semaphore, uint32_t& imageIndex, uint64_t timeout = UINT64_MAX);

    /**
     * @brief Invia l'immagine renderizzata alla coda di presentazione.
     */
    VkResult present(VkSemaphore waitSemaphore, uint32_t imageIndex);

    /**
     * @brief Ricrea la swapchain e il depth buffer in caso di ridimensionamento finestra.
     */
    void recreate(const Window& window);

    static SwapchainSupportDetails querySupport(VkPhysicalDevice device, VkSurfaceKHR surface);

private:
    const Context& m_context;
    VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    VkFormat m_imageFormat{VK_FORMAT_UNDEFINED};
    VkFormat m_depthFormat{VK_FORMAT_D32_SFLOAT};
    VkExtent2D m_extent{0, 0};
    VkPresentModeKHR m_presentMode{VK_PRESENT_MODE_FIFO_KHR};
    Image m_depthImage;

    void init(const Window& window, VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);
    void cleanup();

    static VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    static VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    static VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, const Window& window);
};
