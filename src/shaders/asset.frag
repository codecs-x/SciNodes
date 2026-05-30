#version 450

layout(location = 0) in  vec3 vNormalWorld;
layout(location = 1) in  vec3 vTint;
layout(location = 0) out vec4 outColor;

// Lambert simple con luz direccional fija desde arriba-izquierda + un
// hemisférico chico (ambient azulado / amarillo desde abajo) para que
// las caras en sombra no queden negras.
void main() {
    vec3 N = normalize(vNormalWorld);
    vec3 L = normalize(vec3(-0.40, 0.85, 0.35));

    float lambert = max(dot(N, L), 0.0);
    // Hemispheric ambient: arriba azulado, abajo cálido.
    vec3 ambSky    = vec3(0.18, 0.22, 0.30);
    vec3 ambGround = vec3(0.16, 0.13, 0.10);
    float wUp = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambient = mix(ambGround, ambSky, wUp);

    vec3 diffuse = vTint * lambert;
    vec3 color   = ambient * vTint + diffuse;
    outColor     = vec4(color, 1.0);
}
