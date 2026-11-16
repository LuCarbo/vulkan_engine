# MASTER EXECUTION PLAN & CHECKLIST: Vulkan 1.4 Low-Poly Engine (C++20)

> **Lead Architect & Orchestrator:** Argon | **Implementation & Verification Worker:** Hydrogen  
> **Target Deliverable:** Real-time Low-Poly 3D Engine showcasing Vulkan 1.4 Dynamic Rendering, Synchronization2, Push Descriptors, VMA, PCF Shadow Mapping, RenderDoc `VK_EXT_debug_utils`, and live Dear ImGui backend telemetry.

---

## 1. Strict Architectural Constraints (Codebase Guardrails)

All modules MUST strictly conform to the existing repository conventions:
1. **Namespaces & Directory Layout:**
   - Core Vulkan subsystems live in `src/core/` (global namespace; `DebugUtils` uses `namespace DebugUtils` + global `ScopedCmdLabel`).
   - Scene subsystems live in `src/scene/` (`Camera`, `Mesh`, `Vertex`).
   - Includes are relative to `src/` (e.g., `#include "core/Context.hpp"`, `#include "scene/Camera.hpp"`).
2. **C++20 & MSVC Clean Build:**
   - Standard C++20 (`CMAKE_CXX_STANDARD 20`), zero warnings under MSVC `/W4 /permissive-`.
   - Use `[[nodiscard]]`, `noexcept`, `m_` member prefixes, and designated initializers where appropriate.
   - Define `GLM_FORCE_RADIANS`, `GLM_FORCE_DEPTH_ZERO_TO_ONE`, and `NOMINMAX` at target level.
3. **RAII & Ownership:**
   - All Vulkan/VMA resource-owning classes disable copying (`= delete`).
   - Movable resource classes (`Window`, `Swapchain`, `Pipeline`, `Buffer`, `Image`, `Mesh`) implement `noexcept` move constructors/assignments that zero/nullify source handles (`VK_NULL_HANDLE` / `nullptr`).
   - Non-movable orchestrators (`Context`, `Renderer`) delete both copy and move operations; all GPU resources clean up deterministically before `Context` teardown.
4. **Vulkan 1.3 / 1.4 Modern API Only:**
   - **NO** `VkRenderPass`, `VkFramebuffer`, `vkCmdPipelineBarrier` (v1), or `vkQueueSubmit` (v1).
   - **ONLY** Dynamic Rendering (`vkCmdBeginRendering` / `vkCmdEndRendering`, `VkPipelineRenderingCreateInfo` with `renderPass = VK_NULL_HANDLE`).
   - **ONLY** `Synchronization2` (`vkCmdPipelineBarrier2`, `VkImageMemoryBarrier2`, `VkDependencyInfo`, `vkQueueSubmit2`, `VkSubmitInfo2`).
   - **Push Descriptors:** Use `VK_KHR_push_descriptor` (`vkCmdPushDescriptorSet` / `VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT`) for per-frame UBOs and shadow map samplers.
5. **Coordinate System & Winding:**
   - GLM configured with `GLM_FORCE_RADIANS` and `GLM_FORCE_DEPTH_ZERO_TO_ONE` (depth range `[0, 1]`, clear depth `1.0f`, compare `VK_COMPARE_OP_LESS`).
   - Invert Y in projection (`proj[1][1] *= -1.0f`) and use `VK_FRONT_FACE_COUNTER_CLOCKWISE` + `VK_CULL_MODE_BACK_BIT` for 3D geometry.
6. **Zero Validation Errors:**
   - Zero `VK_LAYER_KHRONOS_validation` warnings or errors across startup, resizing/minimizing, runtime, and shutdown.

---

## 2. Dependency-Ordered Execution Roadmap (Phases 1–4)

### Phase 1: Memory (VMA), Depth Buffer & Debug Instrumentation
**Lead:** Argon (Interfaces & Integration) | **Worker:** Hydrogen (Implementation & Build Verification)

- [x] **1.1 Build System Updates (`CMakeLists.txt`)**
  - [x] Integrate **VMA** (`<vma/vk_mem_alloc.h>`) and **GLM** (`<glm/glm.hpp>`) using installed Vulkan SDK headers when present, with `FetchContent` fallback.
  - [x] Add `GLM_FORCE_RADIANS`, `GLM_FORCE_DEPTH_ZERO_TO_ONE`, `NOMINMAX` compile definitions to `VulkanRenderer`.
  - [x] Register `src/core/Buffer.hpp/.cpp`, `src/core/Image.hpp/.cpp`, `src/core/DebugUtils.hpp/.cpp` in `SOURCES`.
- [x] **1.2 RenderDoc Instrumentation (`src/core/DebugUtils.hpp`, `src/core/DebugUtils.cpp`)**
  - [x] Load `vkSetDebugUtilsObjectNameEXT`, `vkCmdBeginDebugUtilsLabelEXT`, `vkCmdEndDebugUtilsLabelEXT` via `vkGetInstanceProcAddr` in `DebugUtils::init(VkInstance)`.
  - [x] Implement `DebugUtils::setObjectName(...)` overloads (safe no-op if extension is unavailable).
  - [x] Implement RAII `ScopedCmdLabel` (non-copyable, non-movable) for RenderDoc pass grouping.
- [x] **1.3 `Context` Extensions (`src/core/Context.hpp`, `src/core/Context.cpp`)**
  - [x] Enable `VK_EXT_DEBUG_UTILS_EXTENSION_NAME` whenever supported by the instance (including Release builds for RenderDoc) and initialize `DebugUtils`.
  - [x] Query and enable `fillModeNonSolid` and `samplerAnisotropy` alongside `dynamicRendering`, `synchronization2`, `pushDescriptor`, and `dynamicRenderingLocalRead`.
  - [x] Initialize `VmaAllocator` (`VMA_IMPLEMENTATION`, `vulkanApiVersion = VK_API_VERSION_1_4`).
  - [x] Add a transient `VkCommandPool` and `beginSingleTimeCommands()` / `endSingleTimeCommands(VkCommandBuffer)` using `vkQueueSubmit2` + fence wait.
  - [x] Delete move operations on `Context` (safe references in `Swapchain`/`Renderer`) and destroy `m_commandPool` -> `vmaDestroyAllocator` -> `vkDestroyDevice` in `~Context()`.
- [x] **1.4 GPU Buffer & Image Wrappers (`src/core/Buffer.hpp/.cpp`, `src/core/Image.hpp/.cpp`)**
  - [x] Implement RAII movable `Buffer` wrapping `VkBuffer` + `VmaAllocation`, `upload(...)`, and `Buffer::createDeviceLocal(...)` via staging buffer + `vkCmdCopyBuffer`.
  - [x] Implement RAII movable `Image` wrapping `VkImage` + `VmaAllocation` + `VkImageView`, plus `Image::transitionLayout(...)` using `vkCmdPipelineBarrier2` / `VkImageMemoryBarrier2`.
- [x] **1.5 Swapchain Depth Buffer & Pipeline/Renderer Depth Wiring**
  - [x] Add `VK_FORMAT_D32_SFLOAT` `Image m_depthImage` to `Swapchain`, created in `init()`, destroyed in `cleanup()`, and auto-recreated in `recreate()`.
  - [x] Update `Pipeline` to accept `VkFormat depthFormat` (`VkPipelineRenderingCreateInfo::depthAttachmentFormat`) and enable `VkPipelineDepthStencilStateCreateInfo` (`VK_COMPARE_OP_LESS`).
  - [x] Update `Renderer::renderFrame()` to wrap passes in `ScopedCmdLabel`, transition swapchain depth image (`UNDEFINED` -> `DEPTH_ATTACHMENT_OPTIMAL`), and attach `pDepthAttachment` to `VkRenderingInfo`.
  - [x] **Verify:** Configure & build with CMake/Ninja -> zero errors, zero `/W4 /permissive-` warnings.

---

### Phase 2: 3D Camera, Mesh Pipeline & Low-Poly Shading
**Lead:** Argon (Pipeline/Descriptor Layout) | **Worker:** Hydrogen (Geometry & Shaders)

- [x] **2.1 Dependency & Build Setup (`CMakeLists.txt`)**
  - [x] Add `tinyobjloader` (or `fastgltf`) via `FetchContent` for `.obj`/`.gltf` prop loading.
  - [x] Add `src/scene/Camera.hpp/.cpp` and `src/scene/Mesh.hpp/.cpp` to `SOURCES`.
  - [x] Register `shaders/lowpoly.vert` and `shaders/lowpoly.frag` in `CompileShaders`.
- [x] **2.2 `Vertex` & 3D `Pipeline` Upgrades (`src/core/Pipeline.hpp/.cpp`)**
  - [x] Define `Vertex` (`glm::vec3 pos`, `glm::vec3 color`, `glm::vec2 uv`) with `getBindingDescription()` and `getAttributeDescriptions()`.
  - [x] Define `PushConstants` (`glm::mat4 model`, `int32_t debugView`, padding as needed) and `FrameUBO` (`view`, `proj`, `viewProj`, `lightSpaceMatrix`, `sunDir`, `sunColor`, `cameraPos`, `time`).
  - [x] Support configurable `PipelineConfig` (vertex input toggle, `VK_CULL_MODE_BACK_BIT` + `VK_FRONT_FACE_COUNTER_CLOCKWISE`, `VK_POLYGON_MODE_FILL` vs `VK_POLYGON_MODE_LINE` wireframe pipeline when `hasNonSolidFill()` is true, push descriptor set layout + push constant range).
- [x] **2.3 3D Camera (`src/scene/Camera.hpp/.cpp`)**
  - [x] Implement perspective projection (`GLM_FORCE_DEPTH_ZERO_TO_ONE`, `proj[1][1] *= -1.0f`).
  - [x] Implement dual control modes: **Fly (WASD + right-mouse look)** and **Orbit** around terrain center, plus delta-time smoothing.
- [x] **2.4 Scene Geometry & Asset Loading (`src/scene/Mesh.hpp/.cpp`)**
  - [x] Implement RAII movable `Mesh` owning device-local vertex and index `Buffer`s + `draw(VkCommandBuffer)`.
  - [x] Procedural Low-Poly Terrain generator (multi-octave displaced heightmap grid with vertex color elevation bands: deep water, sand, grass, rock, snow).
  - [x] Animated Low-Poly Water plane mesh (vertex wave displacement driven by `time` UBO or dynamic vertices).
  - [x] `.obj`/`.gltf` loader (`tinyobjloader` / `fastgltf`) to instantiate low-poly trees/rocks/props placed deterministically across terrain above water level.
- [x] **2.5 Low-Poly Shaders (`shaders/lowpoly.vert`, `shaders/lowpoly.frag`)**
  - [x] Implement `#version 460` vertex shader transforming world/clip positions and passing `fragWorldPos`, `fragColor`, `fragUV`, and `fragLightSpacePos`.
  - [x] Compute crisp flat-shaded normals in fragment shader without duplicating vertices:
        `vec3 flatNormal = normalize(cross(dFdx(fragWorldPos), dFdy(fragWorldPos)));`
  - [x] Implement directional sun diffuse + hemisphere ambient sky/ground fill + specular highlights on water + gradient skybox background pass/shader.
  - [x] Implement `debugView` push-constant switch: `0` = Final Lit, `1` = Flat Normals RGB, `2` = Shadow Factor, `3` = Depth/Elevation Debug.
  - [x] **Verify:** Build passes cleanly; 3D low-poly scene renders with depth testing, camera navigation, and live wireframe/debug mode switching.

---

### Phase 3: Directional Shadow Mapping (Multi-Pass Dynamic Rendering)
**Lead:** Argon (Synchronization2 Barriers & Push Descriptors) | **Worker:** Hydrogen (Shadow Shaders & Pass Wiring)

- [x] **3.1 Shadow Map Resources**
  - [x] Allocate `2048x2048` `VK_FORMAT_D32_SFLOAT` shadow depth `Image` (`VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT`).
  - [x] Create comparison `VkSampler` (`VK_FILTER_LINEAR`, `VK_COMPARE_OP_LESS_OR_EQUAL`, `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER`, white border).
  - [x] Compute directional light orthographic `lightSpaceMatrix` (`glm::ortho` covering scene bounds, `[0, 1]` depth, Y-flipped).
- [x] **3.2 Shadow Pipeline & Shader (`shaders/shadow.vert`)**
  - [x] Write `shaders/shadow.vert` (`#version 460`) outputting `lightSpaceMatrix * model * vec4(inPos, 1.0)` and register in `CompileShaders`.
  - [x] Build depth-only `Pipeline` (`colorAttachmentCount = 0`, `depthAttachmentFormat = VK_FORMAT_D32_SFLOAT`, `depthBiasEnable = VK_TRUE` + `vkCmdSetDepthBias` to eliminate shadow acne).
- [x] **3.3 Two-Pass Dynamic Rendering Graph (`Renderer::renderFrame()`)**
  - [x] **Pass 1 (`ScopedCmdLabel("Shadow Depth Pass")`):**
    - [x] Barrier shadow `Image`: `UNDEFINED` -> `DEPTH_ATTACHMENT_OPTIMAL` (`Synchronization2`).
    - [x] `vkCmdBeginRendering` (depth-only, clear `1.0f`), bind shadow pipeline, draw shadow-casting scene meshes, `vkCmdEndRendering`.
    - [x] Barrier shadow `Image`: `DEPTH_ATTACHMENT_OPTIMAL` -> `SHADER_READ_ONLY_OPTIMAL` (`EARLY_FRAGMENT_TESTS` / `LATE_FRAGMENT_TESTS` -> `FRAGMENT_SHADER_BIT`).
  - [x] **Pass 2 (`ScopedCmdLabel("Main Low-Poly Pass")`):**
    - [x] Barrier swapchain color (`UNDEFINED` -> `COLOR_ATTACHMENT_OPTIMAL`) and swapchain depth (`UNDEFINED` -> `DEPTH_ATTACHMENT_OPTIMAL`).
    - [x] `vkCmdBeginRendering` (color + depth).
    - [x] Bind Frame UBO and shadow `combinedImageSampler` via `vkCmdPushDescriptorSet` (`VK_KHR_push_descriptor`).
    - [x] Draw skybox + terrain + water + props with 3x3 PCF soft shadow filtering in `lowpoly.frag`.
    - [x] Render Dear ImGui overlay (Phase 4) inside the main rendering pass before `vkCmdEndRendering`.
    - [x] Barrier swapchain color (`COLOR_ATTACHMENT_OPTIMAL` -> `PRESENT_SRC_KHR`).
  - [x] **Verify (Build):** `shadow.vert.spv` and `lowpoly.frag.spv` compile and `VulkanRenderer.exe` links cleanly (live RenderDoc capture verified with final Phase 4 run).

---

### Phase 4: Backend Telemetry Overlay & Video Polish
**Lead:** Hydrogen (ImGui & GPU Queries) | **Worker:** Argon (Validation & Leak Audit)

- [x] **4.1 Dear ImGui Integration (`CMakeLists.txt`, `src/core/ImGuiLayer.hpp/.cpp` or `Renderer`)**
  - [x] Add `ocornut/imgui` via `FetchContent` (`imgui_impl_glfw` + `imgui_impl_vulkan`).
  - [x] Initialize `ImGui_ImplVulkan_InitInfo` with `.UseDynamicRendering = true` and `VkPipelineRenderingCreateInfo` (`.colorAttachmentCount = 1`, `.pColorAttachmentFormats = &colorFormat`, `.depthAttachmentFormat = depthFormat`), plus small dedicated ImGui descriptor pool.
- [x] **4.2 Live GPU Timestamp Queries (`Renderer`)**
  - [x] Create per-frame `VkQueryPool` (`VK_QUERY_TYPE_TIMESTAMP`, 4 timestamps per frame: frame start, after shadow pass, after main pass / frame end).
  - [x] Convert tick deltas to milliseconds using `VkPhysicalDeviceLimits::timestampPeriod`.
- [x] **4.3 "Vulkan Backend Inspector" Telemetry UI**
  - [x] Display GPU Name, Vulkan API Version (`1.4`), Present Mode, Swapchain Extent.
  - [x] Display live FPS, frame time, `Shadow Pass ms`, and `Main Pass ms`.
  - [x] Display per-frame Draw Calls and Rendered Triangle Count.
  - [x] Query and display live VMA memory statistics via `vmaCalculateStatistics` (Device-Local vs. Host-Visible allocated MB & allocation count).
  - [x] Interactive controls: Sun Azimuth & Elevation sliders, Water Wave Speed slider, Camera Mode toggle (Fly / Orbit), Wireframe toggle, and `debugView` selector (`0..3`).
- [x] **4.4 Final Validation & Leak Audit**
  - [x] Verified zero `VK_LAYER_KHRONOS_validation` warnings or errors during live startup and active 3-second render loop (`NVIDIA GeForce RTX 5070 Laptop GPU`, Debug build).
  - [x] Audited deterministic RAII destruction chain (`~Renderer` -> `~Pipeline` -> `~Swapchain` -> `~Context` -> `vmaDestroyAllocator` -> `~Window`) ensuring all VMA buffers/images, ImGui resources, query pools, and Vulkan handles are released before device destruction.
  - [ ] Manual interactive check (screen recording / RenderDoc): verify window resize, minimize/restore, mode toggles, and clean exit interactively.
