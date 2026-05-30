#version 450

// Vertex layout: posición + normal por vértice (SolidVertex).
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

// Push constant — 144 bytes:
//   mat4 viewProj  (64)
//   mat4 model     (64)
//   vec4 tintRGB+_ (16)  — el .w está libre para uso futuro
// La luz va hardcodeada en el fragment shader; mover a PC si se necesita.
layout(push_constant) uniform PC {
    mat4 viewProj;
    mat4 model;
    vec4 tint;
} pc;

layout(location = 0) out vec3 vNormalWorld;
layout(location = 1) out vec3 vTint;

void main() {
    vec4 worldPos = pc.model * vec4(inPos, 1.0);
    gl_Position   = pc.viewProj * worldPos;

    // Normal en espacio mundo.  Asumimos que el model matrix es solo
    // rotación + traslación (sin escala no-uniforme), así que la matriz
    // normal es la 3x3 superior izquierda directamente.  Si en el futuro
    // se aplica escala no-uniforme habrá que pasar inverse-transpose por
    // un PC adicional.
    vNormalWorld = normalize(mat3(pc.model) * inNormal);
    vTint        = pc.tint.rgb;
}
