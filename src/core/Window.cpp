#include "core/Window.hpp"

#include <stdexcept>
#include <iostream>

Window::Window(uint32_t width, uint32_t height, std::string_view title)
    : m_width(width), m_height(height), m_title(title) {
    initWindow();
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
}

Window::Window(Window&& other) noexcept
    : m_width(other.m_width),
      m_height(other.m_height),
      m_title(std::move(other.m_title)),
      m_window(other.m_window) {
    other.m_window = nullptr;
    other.m_width = 0;
    other.m_height = 0;
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        if (m_window) {
            glfwDestroyWindow(m_window);
        }
        m_width = other.m_width;
        m_height = other.m_height;
        m_title = std::move(other.m_title);
        m_window = other.m_window;

        other.m_window = nullptr;
        other.m_width = 0;
        other.m_height = 0;
    }
    return *this;
}

void Window::initWindow() {
    if (!glfwInit()) {
        throw std::runtime_error("Impossibile inizializzare GLFW!");
    }

    // Specifichiamo che GLFW NON deve creare un contesto OpenGL e abilitiamo il resize
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(
        static_cast<int>(m_width),
        static_cast<int>(m_height),
        m_title.c_str(),
        nullptr,
        nullptr
    );

    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Impossibile creare la finestra GLFW!");
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
}

void Window::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    (void)width;
    (void)height;
    auto* appWindow = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (appWindow) {
        appWindow->m_framebufferResized = true;
    }
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::pollEvents() const {
    glfwPollEvents();
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance, m_window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Impossibile creare la Window Surface Vulkan!");
    }
    return surface;
}

std::pair<uint32_t, uint32_t> Window::getFramebufferSize() const {
    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
    return { static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight) };
}
