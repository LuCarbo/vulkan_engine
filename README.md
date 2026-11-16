# Vulkan 1.4 Low-Poly Engine

Motore di rendering 3D real-time basato su **Vulkan 1.4** e **C++20**, progettato per dimostrare un'architettura moderna priva di costrutti legacy.

## Caratteristiche Architetturali
- **Dynamic Rendering nativo**: Nessun `VkRenderPass` o `VkFramebuffer`. Tutte le passate (Shadow Depth Pass e Main Low-Poly Pass + ImGui) usano `vkCmdBeginRendering` / `vkCmdEndRendering`.
- **Synchronization 2 nativa**: Barriere di memoria, layout transition, timestamp GPU (`vkCmdWriteTimestamp2`) e queue submission gestite esclusivamente con `vkCmdPipelineBarrier2`, `VkImageMemoryBarrier2` e `vkQueueSubmit2`.
- **Push Descriptors (`VK_KHR_push_descriptor` / Vulkan 1.4 Core)**: Binding dinamico di `FrameUBO` e `sampler2DShadow` tramite `vkCmdPushDescriptorSet` senza boilerplate di descriptor pool per-frame (con fallback automatico su GPU prive dell'estensione).
- **Vulkan Memory Allocator (VMA)**: Gestione RAII di buffer e immagini device-local/staging con telemetria in tempo reale (`vmaCalculateStatistics`).
- **Directional Shadow Mapping (3x3 PCF)**: Shadow map `2048x2048` `D32_SFLOAT` con comparison sampler e dynamic depth bias.
- **RenderDoc & Validation Layer Ready**: `VK_EXT_debug_utils` object naming e `ScopedCmdLabel` per passata (`Shadow Depth Pass`, `Main Low-Poly Pass`, `ImGui Telemetry Overlay`) con zero errori dei Khronos Validation Layers.
- **Vulkan Backend Inspector (Dear ImGui)**: Pannello live con GPU info, FPS, tempi GPU per passata (`VkQueryPool`), draw calls, triangoli, statistiche VMA e controlli interattivi.

## Controlli Interattivi
- `1` – `4`: Switch live dei **Debug View** (`Final Lit`, `Flat Normals RGB`, `Shadow Factor`, `Depth / Elevation`)
- `F`: Toggle **Wireframe** (`VK_POLYGON_MODE_LINE`)
- `C`: Switch modalità camera **Orbit** / **Fly**
- `WASD` + `Q`/`E` + **Click Destro Mouse (RMB)**: Navigazione 3D / orbita manuale

## Struttura del Progetto

```text
vulkan-renderer/
├── CMakeLists.txt            # FetchContent (GLFW, tinyobjloader, Dear ImGui) + SDK GLM/VMA + CompileShaders
├── assets/
│   ├── pine_tree.obj         # Albero low-poly con vertex colors
│   └── rock.obj              # Roccia low-poly sfaccettata
├── shaders/
│   ├── lowpoly.vert / .frag  # Shading flat-derivative, onde acqua, skybox, PCF 3x3
│   ├── shadow.vert           # Vertex shader depth-only per shadow map
│   └── triangle.vert / .frag # Shader Hello Triangle iniziale
└── src/
    ├── main.cpp              # Orchestratore e render loop
    ├── core/
    │   ├── Window.hpp/.cpp       # Finestra GLFW e VkSurfaceKHR
    │   ├── DebugUtils.hpp/.cpp   # VK_EXT_debug_utils object naming & ScopedCmdLabel
    │   ├── Context.hpp/.cpp      # Vulkan 1.4 Instance/Device, VmaAllocator, Push Descriptors
    │   ├── Buffer.hpp/.cpp       # Buffer VMA + staging upload
    │   ├── Image.hpp/.cpp        # Immagini VMA + barriere Synchronization2
    │   ├── Swapchain.hpp/.cpp    # Triple buffering + Depth D32_SFLOAT
    │   ├── Pipeline.hpp/.cpp     # Pipeline Dynamic Rendering (fill/wireframe/depth-only)
    │   ├── ImGuiLayer.hpp/.cpp   # Dear ImGui Dynamic Rendering & Inspector UI
    │   └── Renderer.hpp/.cpp     # Multi-pass render graph, Timestamp QueryPool, VMA stats
    └── scene/
        ├── Camera.hpp/.cpp       # Camera prospettica Orbit / Fly (GLM zero-to-one, Y-flip)
        └── Mesh.hpp/.cpp         # Terreno procedurale, acqua, sky dome e loader .obj
```

## Compilazione ed Esecuzione

### Tramite Visual Studio / CMake (`x64-Debug`)
1. Apri la cartella `vulkan-renderer` in Visual Studio 2022/2026 oppure usa il Developer Command Prompt (`vcvars64.bat`):
   ```powershell
   cmake -S . -B out/build/x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build out/build/x64-Debug
   ```
2. Avvia `out/build/x64-Debug/VulkanRenderer.exe` dalla root del progetto (o premi **F5** in Visual Studio).
