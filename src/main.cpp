#include "core/Window.hpp"
#include "core/Context.hpp"
#include "core/Swapchain.hpp"
#include "core/Pipeline.hpp"
#include "core/Renderer.hpp"

#include <iostream>
#include <cstdlib>
#include <stdexcept>

int main() {
    try {
        std::cout << "=== Vulkan 1.4 Low-Poly Engine ===" << std::endl;

        // 1. Inizializzazione della Finestra (GLFW)
        Window window(1280, 720, "Vulkan 1.4 Low-Poly Engine");

        // 2. Inizializzazione del Contesto Vulkan (Istanza, Surface, Logical Device, VMA, Code)
        Context context(window);

        // 3. Creazione della Swapchain (Triple Buffering, Image Views, Depth Buffer, Present)
        Swapchain swapchain(context, window);

        // 4. Creazione della Graphics Pipeline 3D con Dynamic Rendering e Push Descriptors
        PipelineConfig pipelineConfig{};
        pipelineConfig.enableVertexInput = true;
        pipelineConfig.enableDepthTest = true;
        pipelineConfig.enableDepthWrite = true;
        pipelineConfig.enableWireframeVariant = true;
        pipelineConfig.cullMode = VK_CULL_MODE_BACK_BIT;
        pipelineConfig.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        pipelineConfig.depthCompareOp = VK_COMPARE_OP_LESS;
        pipelineConfig.debugName = "Low-Poly 3D Pipeline";

        Pipeline pipeline(
            context,
            swapchain.getImageFormat(),
            swapchain.getDepthFormat(),
            "shaders/lowpoly.vert.spv",
            "shaders/lowpoly.frag.spv",
            pipelineConfig
        );

        // 5. Creazione del Renderer 3D (Scena Low-Poly, Camera, Sync2, Push Descriptors)
        Renderer renderer(context, window, swapchain, pipeline);

        std::cout << "\n[OK] Low-Poly Engine inizializzato! Loop di rendering attivo." << std::endl;

        // 6. Loop Principale di rendering
        while (!window.shouldClose()) {
            window.pollEvents();
            renderer.renderFrame();
        }

        // 7. Attesa che la GPU termini prima della distruzione ordinata delle risorse
        context.waitIdle();

    } catch (const std::exception& e) {
        std::cerr << "\n[ERRORE CRITICO] " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
