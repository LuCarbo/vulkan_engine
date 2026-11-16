cmake_minimum_required(VERSION 3.22)
project(VulkanRenderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Trova l'SDK Vulkan (richiede variabile d'ambiente VULKAN_SDK)
find_package(Vulkan REQUIRED)

# Scarica e configura GLFW automaticamente tramite FetchContent
include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# -------------------------------------------------------------
# GLM & VMA (usa gli header dell'SDK Vulkan se presenti, altrimenti FetchContent)
# -------------------------------------------------------------
if(EXISTS "${Vulkan_INCLUDE_DIRS}/glm/glm.hpp")
    message(STATUS "Trovato GLM nell'SDK Vulkan: ${Vulkan_INCLUDE_DIRS}/glm")
else()
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
    )
    FetchContent_MakeAvailable(glm)
endif()

if(EXISTS "${Vulkan_INCLUDE_DIRS}/vma/vk_mem_alloc.h")
    message(STATUS "Trovato VMA nell'SDK Vulkan: ${Vulkan_INCLUDE_DIRS}/vma")
else()
    FetchContent_Declare(
        VulkanMemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.2.1
    )
    FetchContent_MakeAvailable(VulkanMemoryAllocator)
endif()

# -------------------------------------------------------------
# tinyobjloader per il caricamento di asset 3D (.obj)
# -------------------------------------------------------------
FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG v2.0.0rc13
)
set(TINYOBJLOADER_BUILD_TEST_APP OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tinyobjloader)

# -------------------------------------------------------------
# Dear ImGui per telemetria backend e controlli live
# -------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.9
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${Vulkan_INCLUDE_DIRS}
)
target_link_libraries(imgui PUBLIC
    Vulkan::Vulkan
    glfw
)
if(MSVC)
    target_compile_options(imgui PRIVATE /W0)
endif()

# -------------------------------------------------------------
# Compilazione automatica degli Shader GLSL -> SPIR-V (.spv)
# -------------------------------------------------------------
find_program(GLSLC_EXECUTABLE glslc HINTS "$ENV{VULKAN_SDK}/bin" "${Vulkan_INCLUDE_DIRS}/../bin")
if(GLSLC_EXECUTABLE)
    message(STATUS "Trovato compilatore shader glslc: ${GLSLC_EXECUTABLE}")

    set(SHADER_SOURCES
        shaders/triangle.vert
        shaders/triangle.frag
        shaders/lowpoly.vert
        shaders/lowpoly.frag
        shaders/shadow.vert
    )

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shaders)

    set(SPV_OUTPUTS "")
    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(SPV_OUT_BIN "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
        set(SPV_OUT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER_NAME}.spv")

        add_custom_command(
            OUTPUT ${SPV_OUT_BIN} ${SPV_OUT_SRC}
            COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER} -o ${SPV_OUT_BIN}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SPV_OUT_BIN} ${SPV_OUT_SRC}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}
            COMMENT "Compilazione shader SPIR-V: ${SHADER}"
            VERBATIM
        )
        list(APPEND SPV_OUTPUTS ${SPV_OUT_BIN} ${SPV_OUT_SRC})
    endforeach()

    add_custom_target(CompileShaders ALL DEPENDS ${SPV_OUTPUTS})
else()
    message(WARNING "Compilatore glslc non trovato nel PATH o nel VULKAN_SDK!")
endif()

# -------------------------------------------------------------
# Sorgenti del progetto (Architettura modulare)
# -------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/core/Window.hpp
    src/core/Window.cpp
    src/core/DebugUtils.hpp
    src/core/DebugUtils.cpp
    src/core/Context.hpp
    src/core/Context.cpp
    src/core/Buffer.hpp
    src/core/Buffer.cpp
    src/core/Image.hpp
    src/core/Image.cpp
    src/core/Swapchain.hpp
    src/core/Swapchain.cpp
    src/core/Pipeline.hpp
    src/core/Pipeline.cpp
    src/core/ImGuiLayer.hpp
    src/core/ImGuiLayer.cpp
    src/core/Renderer.hpp
    src/core/Renderer.cpp
    src/scene/Camera.hpp
    src/scene/Camera.cpp
    src/scene/Mesh.hpp
    src/scene/Mesh.cpp
)

# Eseguibile principale
add_executable(VulkanRenderer ${SOURCES})

if(TARGET CompileShaders)
    add_dependencies(VulkanRenderer CompileShaders)
endif()

# Include directories
target_include_directories(VulkanRenderer PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    src
)

if(DEFINED tinyobjloader_SOURCE_DIR)
    target_include_directories(VulkanRenderer PRIVATE ${tinyobjloader_SOURCE_DIR})
endif()

# Definizioni globali per GLM e Windows
target_compile_definitions(VulkanRenderer PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    NOMINMAX
)

# Librerie di collegamento
target_link_libraries(VulkanRenderer PRIVATE
    Vulkan::Vulkan
    glfw
    imgui
)

if(TARGET tinyobjloader)
    target_link_libraries(VulkanRenderer PRIVATE tinyobjloader)
endif()

if(TARGET glm::glm)
    target_link_libraries(VulkanRenderer PRIVATE glm::glm)
endif()

if(TARGET GPUOpen::VulkanMemoryAllocator)
    target_link_libraries(VulkanRenderer PRIVATE GPUOpen::VulkanMemoryAllocator)
endif()

# Flag per Visual Studio su Windows
if(MSVC)
    target_compile_options(VulkanRenderer PRIVATE /W4 /permissive-)
endif()
