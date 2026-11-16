#version 460

// Vertici del triangolo hardcodati nello shader (Vulkan coordinate: Y verso il basso)
const vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),  // Vertice superiore
    vec2(0.5, 0.5),   // Vertice inferiore destro
    vec2(-0.5, 0.5)   // Vertice inferiore sinistro
);

const vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0), // Rosso
    vec3(0.0, 1.0, 0.0), // Verde
    vec3(0.0, 0.0, 1.0)  // Blu
);

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}
