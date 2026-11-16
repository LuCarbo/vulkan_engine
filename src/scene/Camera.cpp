#include "scene/Camera.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float ORBIT_MIN_DISTANCE = 12.0f;
constexpr float ORBIT_MAX_DISTANCE = 140.0f;
constexpr float ORBIT_MIN_PITCH = -15.0f;
constexpr float ORBIT_MAX_PITCH = 80.0f;
constexpr float FLY_MAX_PITCH = 89.0f;
constexpr glm::vec3 WORLD_UP{0.0f, 1.0f, 0.0f};

[[nodiscard]] float wrapDegrees(float degrees) noexcept {
    degrees = std::fmod(degrees, 360.0f);
    if (degrees >= 180.0f) {
        degrees -= 360.0f;
    } else if (degrees < -180.0f) {
        degrees += 360.0f;
    }
    return degrees;
}

} // namespace

Camera::Camera(float fovDegrees, float nearPlane, float farPlane)
    : m_fovDegrees(fovDegrees), m_nearPlane(nearPlane), m_farPlane(farPlane) {
    updateOrbitVectors();
}

void Camera::update(GLFWwindow* window, float deltaTime) {
    deltaTime = std::clamp(deltaTime, 0.0f, 0.1f);

    if (!window) {
        if (m_mode == CameraMode::Orbit) {
            updateOrbitVectors();
        } else {
            updateFlyVectors();
        }
        return;
    }

    const bool rmbPressed = (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;

    if (rmbPressed) {
        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);

        if (m_firstMouse) {
            m_lastMouseX = mouseX;
            m_lastMouseY = mouseY;
            m_firstMouse = false;
        }

        mouseDeltaX = static_cast<float>(mouseX - m_lastMouseX);
        mouseDeltaY = static_cast<float>(mouseY - m_lastMouseY);
        m_lastMouseX = mouseX;
        m_lastMouseY = mouseY;
    } else {
        m_firstMouse = true;
    }

    if (m_mode == CameraMode::Orbit) {
        updateOrbit(window, deltaTime, rmbPressed, mouseDeltaX, mouseDeltaY);
    } else {
        updateFly(window, deltaTime, rmbPressed, mouseDeltaX, mouseDeltaY);
    }
}

void Camera::setMode(CameraMode mode) noexcept {
    if (m_mode == mode) {
        return;
    }

    m_mode = mode;
    m_firstMouse = true;

    if (m_mode == CameraMode::Fly) {
        // Derive Fly Euler angles from the current forward vector so the view does not snap.
        m_flyPitch = std::clamp(glm::degrees(std::asin(std::clamp(m_forward.y, -1.0f, 1.0f))),
                                -FLY_MAX_PITCH, FLY_MAX_PITCH);
        if (std::abs(m_forward.x) > 1e-5f || std::abs(m_forward.z) > 1e-5f) {
            m_flyYaw = wrapDegrees(glm::degrees(std::atan2(m_forward.z, m_forward.x)));
        }
        updateFlyVectors();
    } else {
        // Re-anchor spherical Orbit parameters around m_target from the current Fly position.
        const glm::vec3 offset = m_position - m_target;
        const float dist = glm::length(offset);
        m_orbitDistance = std::clamp(dist, ORBIT_MIN_DISTANCE, ORBIT_MAX_DISTANCE);

        if (dist > 1e-4f) {
            const glm::vec3 dir = offset / dist;
            m_orbitPitch = std::clamp(glm::degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f))),
                                      ORBIT_MIN_PITCH, ORBIT_MAX_PITCH);
            if (std::abs(dir.x) > 1e-5f || std::abs(dir.z) > 1e-5f) {
                m_orbitYaw = wrapDegrees(glm::degrees(std::atan2(dir.z, dir.x)));
            }
        }
        updateOrbitVectors();
    }
}

void Camera::toggleMode() noexcept {
    setMode(m_mode == CameraMode::Orbit ? CameraMode::Fly : CameraMode::Orbit);
}

glm::mat4 Camera::getViewMatrix() const noexcept {
    if (m_mode == CameraMode::Orbit) {
        return glm::lookAt(m_position, m_target, WORLD_UP);
    }
    return glm::lookAt(m_position, m_position + m_forward, m_up);
}

glm::mat4 Camera::getProjectionMatrix(float aspectRatio) const noexcept {
    aspectRatio = std::max(aspectRatio, 0.001f);
    glm::mat4 proj = glm::perspective(glm::radians(m_fovDegrees), aspectRatio, m_nearPlane, m_farPlane);
    proj[1][1] *= -1.0f; // Vulkan NDC Y points downward, opposite to OpenGL.
    return proj;
}

void Camera::updateOrbit(GLFWwindow* window, float deltaTime, bool rmbPressed,
                         float mouseDeltaX, float mouseDeltaY) noexcept {
    const bool keyLeft  = (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) ||
                          (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS);
    const bool keyRight = (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) ||
                          (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS);
    const bool keyIn    = (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) ||
                          (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS);
    const bool keyOut   = (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) ||
                          (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS);
    const bool keyDown  = (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS);
    const bool keyUp    = (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS);

    const bool hasOrbitInput = rmbPressed || keyLeft || keyRight || keyIn || keyOut || keyDown || keyUp;

    if (rmbPressed) {
        constexpr float mouseSensitivity = 0.22f;
        m_orbitYaw += mouseDeltaX * mouseSensitivity;
        m_orbitPitch += mouseDeltaY * mouseSensitivity;
    }

    constexpr float yawSpeed = 55.0f;
    constexpr float pitchSpeed = 40.0f;
    constexpr float zoomSpeed = 28.0f;

    if (keyLeft)  { m_orbitYaw -= yawSpeed * deltaTime; }
    if (keyRight) { m_orbitYaw += yawSpeed * deltaTime; }
    if (keyIn)    { m_orbitDistance -= zoomSpeed * deltaTime; }
    if (keyOut)   { m_orbitDistance += zoomSpeed * deltaTime; }
    if (keyDown)  { m_orbitPitch -= pitchSpeed * deltaTime; }
    if (keyUp)    { m_orbitPitch += pitchSpeed * deltaTime; }

    if (!hasOrbitInput) {
        m_orbitYaw += 8.0f * deltaTime;
    }

    m_orbitYaw = wrapDegrees(m_orbitYaw);
    m_orbitPitch = std::clamp(m_orbitPitch, ORBIT_MIN_PITCH, ORBIT_MAX_PITCH);
    m_orbitDistance = std::clamp(m_orbitDistance, ORBIT_MIN_DISTANCE, ORBIT_MAX_DISTANCE);

    updateOrbitVectors();
}

void Camera::updateFly(GLFWwindow* window, float deltaTime, bool rmbPressed,
                       float mouseDeltaX, float mouseDeltaY) noexcept {
    if (rmbPressed) {
        constexpr float mouseSensitivity = 0.15f;
        m_flyYaw = wrapDegrees(m_flyYaw + mouseDeltaX * mouseSensitivity);
        m_flyPitch = std::clamp(m_flyPitch - mouseDeltaY * mouseSensitivity, -FLY_MAX_PITCH, FLY_MAX_PITCH);
    }

    updateFlyVectors();

    glm::vec3 moveDir(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { moveDir += m_forward; }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { moveDir -= m_forward; }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { moveDir += m_right; }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { moveDir -= m_right; }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        moveDir += WORLD_UP;
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        moveDir -= WORLD_UP;
    }

    if (glm::dot(moveDir, moveDir) > 1e-6f) {
        const bool fast = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS);
        const float speed = fast ? 44.0f : 22.0f;
        m_position += glm::normalize(moveDir) * (speed * deltaTime);
    }
}

void Camera::updateOrbitVectors() noexcept {
    const float pitchRad = glm::radians(m_orbitPitch);
    const float yawRad = glm::radians(m_orbitYaw);
    const float cosPitch = std::cos(pitchRad);

    const glm::vec3 offset(
        m_orbitDistance * cosPitch * std::cos(yawRad),
        m_orbitDistance * std::sin(pitchRad),
        m_orbitDistance * cosPitch * std::sin(yawRad)
    );

    m_position = m_target + offset;
    m_forward = glm::normalize(m_target - m_position);
    m_right = glm::normalize(glm::cross(m_forward, WORLD_UP));
    m_up = glm::normalize(glm::cross(m_right, m_forward));
}

void Camera::updateFlyVectors() noexcept {
    const float yawRad = glm::radians(m_flyYaw);
    const float pitchRad = glm::radians(m_flyPitch);
    const float cosPitch = std::cos(pitchRad);

    m_forward = glm::normalize(glm::vec3(
        cosPitch * std::cos(yawRad),
        std::sin(pitchRad),
        cosPitch * std::sin(yawRad)
    ));
    m_right = glm::normalize(glm::cross(m_forward, WORLD_UP));
    m_up = glm::normalize(glm::cross(m_right, m_forward));
}
