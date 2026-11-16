# Project Master Plan: Vulkan 1.4 Low-Poly Engine (Video & Backend Showcase)

## 1. Objective & Exam Deliverable
Extend the existing Vulkan 1.4 C++20 `VulkanRenderer` (currently rendering a Dynamic Rendering Hello Triangle) into a **real-time Low-Poly 3D Engine** tailored for a computer graphics exam graded via **screen recording + live backend inspection** (no oral defense).

The final build must visually prove modern Vulkan backend mastery through:
1. A stylized low-poly 3D scene (procedural terrain/water + imported `.gltf`/`.obj` low-poly props, directional sun lighting, PCF shadow mapping, and a gradient skybox).
2. Live **Dear ImGui** telemetry (GPU name, frame times, draw call/triangle counts, VMA memory usage, and real-time lighting/camera controls).
3. Live **Push-Constant Shader Debug Modes** (Final Lit, Flat Geometric Normals, Shadow Factor, Wireframe).
4. **RenderDoc** compatibility with `VK_EXT_debug_utils` object naming and scoped command buffer pass labels, plus zero Validation Layer errors on startup, resize, and shutdown.

---

## 2. Strict Architectural Constraints (Preserve Existing Codebase Standards)
All agents (**Argon** & **Hydrogen**) MUST adhere to the existing architecture:
- **Language & Build:** C++20 (`CMAKE_CXX_STANDARD 20`), CMake >= 3.22 (`FetchContent` for new third-party dependencies).
- **Vulkan 1.3/1.4 Core Only:**
  - **NO** legacy `VkRenderPass` or `VkFramebuffer`. Use `vkCmdBeginRendering` / `vkCmdEndRendering` and `VkPipelineRenderingCreateInfo`.
  - **NO** legacy `vkCmdPipelineBarrier` or `vkQueueSubmit`. Use `Synchronization2` (`vkCmdPipelineBarrier2`, `VkImageMemoryBarrier2`, `vkQueueSubmit2`) exclusively.
  - **Push Descriptors:** Prefer `VK_KHR_push_descriptor` (`vkCmdPushDescriptorSet` / `VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT`) enabled in `Context` for per-frame UBOs and shadow map samplers to avoid descriptor pool boilerplate.
- **Coordinate System:** GLM configured with `#define GLM_FORCE_RADIANS` and `#define GLM_FORCE_DEPTH_ZERO_TO_ONE`. Invert Y in the projection matrix (`proj[1][1] *= -1.0f`) and use `VK_FRONT_FACE_COUNTER_CLOCKWISE` with back-face culling (`VK_CULL_MODE_BACK_BIT`) for 3D geometry.
- **RAII & Ownership:** Vulkan-owning classes disable copy (`= delete`), support move semantics (except `Renderer`), and clean up all resources deterministically before `Context` destruction.

---

## 3. Dependencies to Add via `CMakeLists.txt` (`FetchContent`)
1. **GLM** (`g-truc/glm`) — Vector/matrix math (`GLM_FORCE_DEPTH_ZERO_TO_ONE`).
2. **Vulkan Memory Allocator (VMA)** (`GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator`) — GPU buffer and image memory management.
3. **tinyobjloader** (`tinyobjloader/tinyobjloader`) or **fastgltf** (`spnda/fastgltf`) — Loading low-poly 3D assets.
4. **Dear ImGui** (`ocornut/imgui`, docking or master branch) — Integrated with `imgui_impl_glfw` + `imgui_impl_vulkan` (configured with `UseDynamicRendering = true` so no `VkRenderPass` is needed).

---

## 4. Module Roadmap & Agent Orchestration Split

### Phase 1: Memory (VMA), Depth Buffer & Debug Instrumentation
- **Lead:** Argon (Architecture) | **Worker:** Hydrogen (Implementation & Build Verification)
- **Tasks:**
  1. Add `VmaAllocator` initialization and RAII teardown to `Context` (passing `vulkanApiVersion = VK_API_VERSION_1_4`).
  2. Create `src/core/Buffer.hpp/.cpp` and `src/core/Image.hpp/.cpp` wrapping `VmaAllocation` with staging-buffer upload helpers (using a one-time submit command buffer on the graphics queue).
  3. Add a `VK_FORMAT_D32_SFLOAT` depth `Image` to `Swapchain` (recreated automatically inside `Swapchain::recreate()`).
  4. Create `src/core/DebugUtils.hpp/.cpp` wrapping `VK_EXT_debug_utils` (`vkSetDebugUtilsObjectNameEXT` + an RAII `ScopedCmdLabel` class using `vkCmdBeginDebugUtilsLabelEXT` / `vkCmdEndDebugUtilsLabelEXT`) for RenderDoc inspection.

### Phase 2: 3D Camera, Mesh Pipeline & Low-Poly Shading
- **Lead:** Argon (Pipeline/Descriptor Layout) | **Worker:** Hydrogen (Geometry & Shaders)
- **Tasks:**
  1. Define `Vertex` struct (`glm::vec3 pos`, `glm::vec3 color`, `glm::vec2 uv`) and update `Pipeline` to accept vertex input descriptions, depth testing (`VK_COMPARE_OP_LESS`), `VK_POLYGON_MODE_LINE` toggle support (enable `fillModeNonSolid` in `Context` physical device features), and push constants (`glm::mat4 model` + `int debugView`).
  2. Create `src/scene/Camera.hpp/.cpp` (perspective projection + WASD/mouse fly & orbit controls).
  3. Create `src/scene/Mesh.hpp/.cpp` with:
     - Procedural low-poly terrain generator (displaced grid with vertex color elevation bands: water, sand, grass, rock, snow) + animated low-poly water plane.
     - `.obj`/`.gltf` model loader for placing low-poly trees/rocks/props on the terrain.
  4. Write `shaders/lowpoly.vert` and `shaders/lowpoly.frag` (`#version 460`):
     - Compute crisp flat-shaded normals in the fragment shader without vertex duplication:
       `vec3 flatNormal = normalize(cross(dFdx(fragWorldPos), dFdy(fragWorldPos)));`
     - Implement directional sun lighting + hemisphere ambient + specular water highlights.
     - Implement `debugView` push-constant switch: `0` = Final Lit, `1` = Flat Normals RGB, `2` = Shadow Factor, `3` = Depth/Elevation Debug.

### Phase 3: Directional Shadow Mapping (Multi-Pass Dynamic Rendering)
- **Lead:** Argon (Synchronization2 Barriers) | **Worker:** Hydrogen (Shadow Shaders)
- **Tasks:**
  1. Allocate a `2048x2048` `VK_FORMAT_D32_SFLOAT` shadow depth `Image` with a comparison sampler (`VK_COMPARE_OP_LESS_OR_EQUAL`).
  2. Add `shaders/shadow.vert` + depth-only `Pipeline` (with depth bias enabled to prevent shadow acne).
  3. Update `Renderer::renderFrame()` into a clean two-pass graph wrapped in RenderDoc debug labels:
     - **Pass 1 (`"Shadow Depth Pass"`):** Barrier shadow image to `DEPTH_ATTACHMENT_OPTIMAL` $\rightarrow$ `vkCmdBeginRendering` (depth-only) $\rightarrow$ draw scene casters $\rightarrow$ `vkCmdEndRendering` $\rightarrow$ barrier shadow image to `SHADER_READ_ONLY_OPTIMAL`.
     - **Pass 2 (`"Main Low-Poly Pass"`):** Barrier swapchain color image to `COLOR_ATTACHMENT_OPTIMAL` & swapchain depth to `DEPTH_ATTACHMENT_OPTIMAL` $\rightarrow$ `vkCmdBeginRendering` $\rightarrow$ bind shadow map via `vkCmdPushDescriptorSet` $\rightarrow$ draw skybox + low-poly scene (3x3 PCF soft shadows in `lowpoly.frag`) $\rightarrow$ render Dear ImGui overlay $\rightarrow$ `vkCmdEndRendering` $\rightarrow$ barrier color image to `PRESENT_SRC_KHR`.

### Phase 4: Backend Telemetry Overlay & Video Polish
- **Lead:** Hydrogen (ImGui & GPU Queries) | **Worker:** Argon (Validation & Leak Audit)
- **Tasks:**
  1. Integrate Dear ImGui (`ImGui_ImplVulkan_InitInfo` with `.UseDynamicRendering = true`).
  2. Add `VkQueryPool` (`VK_QUERY_TYPE_TIMESTAMP`) in `Renderer` to measure live GPU frame/pass times in milliseconds.
  3. Build the **"Vulkan Backend Inspector"** ImGui window displaying:
     - Device Name & Vulkan API Version (`1.4`), Present Mode, Swapchain Extent.
     - Live GPU Pass Timings (`Shadow Pass ms`, `Main Pass ms`) & FPS.
     - Draw Calls & Rendered Triangle Count.
     - Live VMA Memory Stats (`vmaCalculateStatistics` showing Device-Local vs. Host-Visible allocated MB).
     - Interactive Controls: Sun Azimuth/Elevation sliders, Water Wave Speed slider, Camera Mode toggle, Wireframe toggle, and `debugView` selector.
  4. Verify zero `VK_LAYER_KHRONOS_validation` warnings/errors across startup, multiple window resizes, and exit.

---

## 5. Instructions for the Orchestrator (Argon)
1. Review the existing files (`CMakeLists.txt`, `src/core/*`) to confirm exact class signatures and namespaces.
2. Break **Phase 1 through Phase 4** into concrete, dependency-ordered tasks in `agents/task.md`.
3. For every phase, generate the architectural interfaces first, then dispatch parallel subagent tasks to **Hydrogen** for implementation, shader compilation (`CompileShaders`), and CMake build verification.