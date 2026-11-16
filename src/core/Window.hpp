#pragma once

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>
#include <string_view>
#include <utility>

/**
 * @brief Gestisce il ciclo di vita della finestra GLFW e l'integrazione con la superficie Vulkan.
 * 
 * Principio: High Cohesion per il windowing, Low Coupling con l'API Vulkan interna.
 * Si limita a esporre la creazione della VkSurfaceKHR senza conoscere i dettagli di device o rendering.
 */
class Window {
public:
    Window(uint32_t width, uint32_t height, std::string_view title);
    ~Window();

    // Classe non copiabile (gestione RAII della risorsa GLFWwindow)
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Movibile
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    [[nodiscard]] bool shouldClose() const;
    void pollEvents() const;

    [[nodiscard]] VkSurfaceKHR createSurface(VkInstance instance) const;
    [[nodiscard]] GLFWwindow* getHandle() const noexcept { return m_window; }
    [[nodiscard]] std::pair<uint32_t, uint32_t> getFramebufferSize() const;

    [[nodiscard]] uint32_t getWidth() const noexcept { return m_width; }
    [[nodiscard]] uint32_t getHeight() const noexcept { return m_height; }
    [[nodiscard]] bool wasResized() const noexcept { return m_framebufferResized; }
    void resetResizedFlag() noexcept { m_framebufferResized = false; }

private:
    uint32_t m_width{0};
    uint32_t m_height{0};
    std::string m_title;
    GLFWwindow* m_window{nullptr};
    bool m_framebufferResized{false};

    void initWindow();
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
};
