#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>

/**
 * @brief Camera navigation mode.
 */
enum class CameraMode {
    Orbit,
    Fly
};

/**
 * @brief 3D camera supporting Orbit showcase and free Fly navigation for Vulkan rendering.
 */
class Camera {
public:
    explicit Camera(float fovDegrees = 55.0f, float nearPlane = 0.2f, float farPlane = 600.0f);

    void update(GLFWwindow* window, float deltaTime);
    void setMode(CameraMode mode) noexcept;
    void toggleMode() noexcept;

    [[nodiscard]] CameraMode getMode() const noexcept { return m_mode; }
    [[nodiscard]] glm::mat4 getViewMatrix() const noexcept;
    [[nodiscard]] glm::mat4 getProjectionMatrix(float aspectRatio) const noexcept;

    [[nodiscard]] glm::vec3 getPosition() const noexcept { return m_position; }
    [[nodiscard]] glm::vec3 getForward() const noexcept { return m_forward; }
    [[nodiscard]] float getFovDegrees() const noexcept { return m_fovDegrees; }
    [[nodiscard]] float getNearPlane() const noexcept { return m_nearPlane; }
    [[nodiscard]] float getFarPlane() const noexcept { return m_farPlane; }

private:
    CameraMode m_mode{CameraMode::Orbit};

    float m_fovDegrees{55.0f};
    float m_nearPlane{0.2f};
    float m_farPlane{600.0f};

    glm::vec3 m_position{0.0f, 0.0f, 0.0f};
    glm::vec3 m_forward{0.0f, 0.0f, -1.0f};
    glm::vec3 m_right{1.0f, 0.0f, 0.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};
    glm::vec3 m_target{0.0f, 2.5f, 0.0f};

    float m_orbitDistance{46.0f};
    float m_orbitPitch{24.0f};
    float m_orbitYaw{35.0f};

    float m_flyYaw{-145.0f};
    float m_flyPitch{-24.0f};

    bool m_firstMouse{true};
    double m_lastMouseX{0.0};
    double m_lastMouseY{0.0};

    void updateOrbit(GLFWwindow* window, float deltaTime, bool rmbPressed, float mouseDeltaX, float mouseDeltaY) noexcept;
    void updateFly(GLFWwindow* window, float deltaTime, bool rmbPressed, float mouseDeltaX, float mouseDeltaY) noexcept;
    void updateOrbitVectors() noexcept;
    void updateFlyVectors() noexcept;
};
