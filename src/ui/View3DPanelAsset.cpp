#include "View3DPanel.hpp"
#include "View3DPanelInternal.hpp"
#include "../app/Vulkan3DRenderer.hpp"
#include "../core/I18n.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// View3DPanelAsset — split de View3DPanel.cpp (etapa 6K.E).  Rendering del
// path moderno: DeviceAsset (parts + joints + anchors) como wireframe ImDraw
// o como triangle-mesh subido a Vulkan.  Incluye accumulateAssetBBox y
// flattenAssetForVulkan (declarados en View3DPanelInternal.hpp para que
// drawContent en View3DPanel.cpp los pueda llamar), renderAsset y el
// gizmo de ejes.
// ---------------------------------------------------------------------------

using namespace scinodes::ui::view3d_detail;

// ===========================================================================
// flattenAssetForVulkan — convierte un DeviceAsset (mapa de parts) en tres
// arrays planos (positions, normals, indices) listos para
// Vulkan3DRenderer::uploadAssetMesh.  Si una parte carece de normales, se
// calculan flat (una por triángulo) para que el shader Lambert tenga
// dirección.  Las parts cuyo nombre coincide con el `child` de algún joint
// con `driven_by` no vacío reciben una rotación Rodrigues por shaftAngle
// alrededor del eje del joint (en su origen).  Vive en View3DPanel porque
// es una decisión de presentación, no del modelo.
// ===========================================================================
// accumulateAssetBBox — agranda (lo, hi) con las posiciones de las
// parts del asset filtradas por partFilter.  Sin clear inicial:
// llamadas sucesivas acumulan.  Compara con identidad -1e30/1e30 para
// detectar el caso "ningún item aportó nada".
namespace scinodes::ui::view3d_detail {

void accumulateAssetBBox(const scinodes::DeviceAsset& asset,
                         const std::string& partFilter,
                         float lo[3], float hi[3]) {
    auto partAllowed = [&](const std::string& name) {
        return partFilter.empty() || name == partFilter;
    };
    for (const auto& [name, mesh] : asset.parts) {
        if (!partAllowed(name)) continue;
        const size_t vC = mesh.positions.size() / 3;
        for (size_t i = 0; i < vC; ++i) {
            for (int k = 0; k < 3; ++k) {
                float v = mesh.positions[3*i + k];
                if (v < lo[k]) lo[k] = v;
                if (v > hi[k]) hi[k] = v;
            }
        }
    }
}

void flattenAssetForVulkan(const scinodes::DeviceAsset& asset,
                                  float                shaftAngle,
                                  std::vector<float>&    outPositions,
                                  std::vector<float>&    outNormals,
                                  std::vector<uint32_t>& outIndices,
                                  const std::string&     partFilter,
                                  bool                   rotateAll,
                                  bool                   appendMode,
                                  const SharedAssetBBox* sharedBBox,
                                  const std::array<float,3>& xyzRotation) {
    if (!appendMode) {
        outPositions.clear();
        outNormals.clear();
        outIndices.clear();
    }

    // Helper: dado partFilter, decide si una part del asset entra al
    // render.  Vacío = todas las parts (modo "objeto completo").  No
    // empty = sólo la part cuyo nombre coincide exactamente.
    auto partAllowed = [&](const std::string& name) {
        return partFilter.empty() || name == partFilter;
    };

    // Mapa partName → (origin, axis) para las parts que un joint mueve.
    struct DrivenXform {
        std::array<float, 3> origin;
        std::array<float, 3> axis;
    };
    std::unordered_map<std::string, DrivenXform> driven;
    for (const auto& [jname, jf] : asset.joints) {
        if (jf.driven_by.empty() || jf.child.empty()) continue;
        driven[jf.child] = { jf.origin, jf.axis };
    }

    // Helper Rodrigues: rota `v` alrededor de `axis` (normalizado) por
    // `angle` radianes.  Lo usamos tanto para posiciones (con origin
    // restado/sumado) como para normales (con origin = 0).
    auto rotateRodrigues = [](const float v[3],
                              const std::array<float, 3>& axis,
                              float angle,
                              float out[3]) {
        const float ax = axis[0], ay = axis[1], az = axis[2];
        const float c  = std::cos(angle);
        const float s  = std::sin(angle);
        const float oc = 1.0f - c;
        // R * v + cross(axis, v)*s + axis*(axis·v)*(1-c)
        const float dot = ax*v[0] + ay*v[1] + az*v[2];
        out[0] = v[0]*c + (ay*v[2] - az*v[1])*s + ax*dot*oc;
        out[1] = v[1]*c + (az*v[0] - ax*v[2])*s + ay*dot*oc;
        out[2] = v[2]*c + (ax*v[1] - ay*v[0])*s + az*dot*oc;
    };

    // Primera pasada: AABB global para normalizar la escala.  El asset
    // glTF viene en metros (motor ~5 cm); la cámara Vulkan está tuneada
    // para mallas en escala unitaria.  Sin esto el motor se ve como un
    // punto a la distancia.
    // Bbox: o bien (a) viene compartida desde el caller (multi-item
    // path, paso 5d), o (b) se computa internamente como antes.
    float cx, cy, cz, halfExt;
    if (sharedBBox) {
        cx = sharedBBox->cx;
        cy = sharedBBox->cy;
        cz = sharedBBox->cz;
        halfExt = sharedBBox->halfExt;
    } else {
        float minP[3] = {  1e30f,  1e30f,  1e30f };
        float maxP[3] = { -1e30f, -1e30f, -1e30f };
        accumulateAssetBBox(asset, partFilter, minP, maxP);
        cx = 0.5f * (minP[0] + maxP[0]);
        cy = 0.5f * (minP[1] + maxP[1]);
        cz = 0.5f * (minP[2] + maxP[2]);
        halfExt = std::max({
            0.5f * (maxP[0] - minP[0]),
            0.5f * (maxP[1] - minP[1]),
            0.5f * (maxP[2] - minP[2]),
            1e-6f
        });
    }
    const float scale = 1.0f / halfExt;

    auto remap = [cx, cy, cz, scale](const float in[3], float out[3]) {
        out[0] = (in[0] - cx) * scale;
        out[1] = (in[1] - cy) * scale;
        out[2] = (in[2] - cz) * scale;
    };

    for (const auto& [name, mesh] : asset.parts) {
        if (!partAllowed(name)) continue;
        if (mesh.positions.empty()) continue;
        const uint32_t base = static_cast<uint32_t>(outPositions.size() / 3);

        // ¿Esta parte está accionada por un joint?  (Path A: el asset
        // declara joints vía contrato.)  PATH B (rotateAll=true): si
        // no hay joint en el asset, sintetizamos uno por default
        // (axis Z+, origen 0,0,0).  Sin esto, los .gltf cargados via
        // loadCatalog (contract-less) quedan estáticos aunque el
        // TransformObject mande rotación.
        auto drIt = driven.find(name);
        const bool isDriven = (drIt != driven.end());
        const bool applyRot = isDriven || rotateAll;

        const size_t vC = mesh.positions.size() / 3;
        outPositions.resize(outPositions.size() + vC * 3);

        if (!applyRot) {
            // Path fijo: aplica el remap (centrado + escala) sólo.
            for (size_t i = 0; i < vC; ++i) {
                float remapped[3];
                remap(&mesh.positions[3*i], remapped);
                outPositions[3*(base + i) + 0] = remapped[0];
                outPositions[3*(base + i) + 1] = remapped[1];
                outPositions[3*(base + i) + 2] = remapped[2];
            }
        } else if (rotateAll) {
            // PATH B (etapa 4): Euler XYZ extrínseco usando xyzRotation
            // como vec(3) de ángulos (rad).  Aplica Rx, Ry, Rz en ese
            // orden — equivalente a la convención XYZ Euler de Blender.
            // Rota alrededor del origen world (0,0,0); para parts cuyo
            // centroide local NO está en el origen, el usuario debe
            // centrar su geometría en el modelado.
            const std::array<float,3> kAxX { 1.f, 0.f, 0.f };
            const std::array<float,3> kAxY { 0.f, 1.f, 0.f };
            const std::array<float,3> kAxZ { 0.f, 0.f, 1.f };
            for (size_t i = 0; i < vC; ++i) {
                float a[3], b[3], c[3];
                rotateRodrigues(&mesh.positions[3*i], kAxX, xyzRotation[0], a);
                rotateRodrigues(a,                    kAxY, xyzRotation[1], b);
                rotateRodrigues(b,                    kAxZ, xyzRotation[2], c);
                float remapped[3];
                remap(c, remapped);
                outPositions[3*(base + i) + 0] = remapped[0];
                outPositions[3*(base + i) + 1] = remapped[1];
                outPositions[3*(base + i) + 2] = remapped[2];
            }
        } else {
            // PATH A original: Rodrigues alrededor del joint axis con
            // shaftAngle scalar (compat con .scn 0.4 y View3DSink legacy).
            std::array<float, 3> org   = isDriven
                ? drIt->second.origin
                : std::array<float, 3>{ 0.f, 0.f, 0.f };
            std::array<float, 3> axisN = { 0.f, 1.f, 0.f };
            if (isDriven) {
                const float* a = drIt->second.axis.data();
                float alen = std::sqrt(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
                if (alen > 1e-8f) axisN = { a[0]/alen, a[1]/alen, a[2]/alen };
            }

            for (size_t i = 0; i < vC; ++i) {
                float local[3] = {
                    mesh.positions[3*i + 0] - org[0],
                    mesh.positions[3*i + 1] - org[1],
                    mesh.positions[3*i + 2] - org[2],
                };
                float rotated[3];
                rotateRodrigues(local, axisN, shaftAngle, rotated);
                float worldP[3] = {
                    rotated[0] + org[0],
                    rotated[1] + org[1],
                    rotated[2] + org[2],
                };
                float remapped[3];
                remap(worldP, remapped);
                outPositions[3*(base + i) + 0] = remapped[0];
                outPositions[3*(base + i) + 1] = remapped[1];
                outPositions[3*(base + i) + 2] = remapped[2];
            }
        }

        // Normales: si vienen, copia (rotándolas también si isDriven);
        // si no, espacio reservado para calcular flat normals abajo.
        const bool hasNormals =
            (mesh.normals.size() == mesh.positions.size());
        if (hasNormals) {
            if (!applyRot) {
                outNormals.insert(outNormals.end(),
                                  mesh.normals.begin(), mesh.normals.end());
            } else if (rotateAll) {
                // PATH B: rotar la normal con la misma composición Euler
                // XYZ que las posiciones (sin offset, las normales sólo
                // tienen orientación).
                const std::array<float,3> kAxX { 1.f, 0.f, 0.f };
                const std::array<float,3> kAxY { 0.f, 1.f, 0.f };
                const std::array<float,3> kAxZ { 0.f, 0.f, 1.f };
                const size_t vC = mesh.normals.size() / 3;
                outNormals.resize(outNormals.size() + vC * 3);
                for (size_t i = 0; i < vC; ++i) {
                    float a[3], b[3], c[3];
                    rotateRodrigues(&mesh.normals[3*i], kAxX, xyzRotation[0], a);
                    rotateRodrigues(a,                  kAxY, xyzRotation[1], b);
                    rotateRodrigues(b,                  kAxZ, xyzRotation[2], c);
                    outNormals[3*(base + i) + 0] = c[0];
                    outNormals[3*(base + i) + 1] = c[1];
                    outNormals[3*(base + i) + 2] = c[2];
                }
            } else {
                // PATH A: joint axis Rodrigues con shaftAngle scalar.
                std::array<float, 3> axisN = { 0.f, 1.f, 0.f };
                if (isDriven) {
                    const float* a = driven[name].axis.data();
                    float alen = std::sqrt(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
                    if (alen > 1e-8f) axisN = { a[0]/alen, a[1]/alen, a[2]/alen };
                }
                const size_t vC = mesh.normals.size() / 3;
                outNormals.resize(outNormals.size() + vC * 3);
                for (size_t i = 0; i < vC; ++i) {
                    float n[3] = {
                        mesh.normals[3*i + 0],
                        mesh.normals[3*i + 1],
                        mesh.normals[3*i + 2],
                    };
                    float rn[3];
                    rotateRodrigues(n, axisN, shaftAngle, rn);
                    outNormals[3*(base + i) + 0] = rn[0];
                    outNormals[3*(base + i) + 1] = rn[1];
                    outNormals[3*(base + i) + 2] = rn[2];
                }
            }
        } else {
            outNormals.insert(outNormals.end(), mesh.positions.size(), 0.0f);
        }

        // Índices: si vienen, sumar offset y copiar; si no, asumir
        // triángulos no-indexados (cada 3 vértices consecutivos).
        std::vector<uint32_t> partIndices;
        if (!mesh.indices.empty()) {
            partIndices.reserve(mesh.indices.size());
            for (uint32_t i : mesh.indices) partIndices.push_back(base + i);
        } else {
            const uint32_t n = static_cast<uint32_t>(mesh.positions.size() / 3);
            partIndices.reserve(n);
            for (uint32_t i = 0; i < n; ++i) partIndices.push_back(base + i);
        }

        // Si la malla no traía normales, calcular flat normal por
        // triángulo y promediar en cada vértice tocado (suma sin
        // normalización final dentro del loop; se normaliza al cierre).
        if (!hasNormals) {
            const size_t triCount = partIndices.size() / 3;
            for (size_t t = 0; t < triCount; ++t) {
                uint32_t i0 = partIndices[3*t + 0];
                uint32_t i1 = partIndices[3*t + 1];
                uint32_t i2 = partIndices[3*t + 2];
                float ax = outPositions[3*i0+0], ay = outPositions[3*i0+1], az = outPositions[3*i0+2];
                float bx = outPositions[3*i1+0], by = outPositions[3*i1+1], bz = outPositions[3*i1+2];
                float cx = outPositions[3*i2+0], cy = outPositions[3*i2+1], cz = outPositions[3*i2+2];
                float ux = bx - ax, uy = by - ay, uz = bz - az;
                float vx = cx - ax, vy = cy - ay, vz = cz - az;
                float nx = uy*vz - uz*vy;
                float ny = uz*vx - ux*vz;
                float nz = ux*vy - uy*vx;
                outNormals[3*i0+0] += nx; outNormals[3*i0+1] += ny; outNormals[3*i0+2] += nz;
                outNormals[3*i1+0] += nx; outNormals[3*i1+1] += ny; outNormals[3*i1+2] += nz;
                outNormals[3*i2+0] += nx; outNormals[3*i2+1] += ny; outNormals[3*i2+2] += nz;
            }
        }

        outIndices.insert(outIndices.end(),
                          partIndices.begin(), partIndices.end());
    }

    // Normalización final de las normales (incluso las que vinieron en
    // el glTF — algunas las traen sin normalizar).
    const size_t vN = outNormals.size() / 3;
    for (size_t i = 0; i < vN; ++i) {
        float nx = outNormals[3*i+0], ny = outNormals[3*i+1], nz = outNormals[3*i+2];
        float len = std::sqrt(nx*nx + ny*ny + nz*nz);
        if (len > 1e-8f) {
            outNormals[3*i+0] = nx / len;
            outNormals[3*i+1] = ny / len;
            outNormals[3*i+2] = nz / len;
        }
    }
}

}  // namespace scinodes::ui::view3d_detail

// ===========================================================================
// renderAsset — dibuja un DeviceAsset (parts + joints + anchors) como
// wireframe usando ImDrawList.  Las parts que son `child` de un joint
// `revolute` aplican rotación alrededor del eje del joint (Rodrigues)
// usando shaftAngle.  El resto queda estático.
//
// Anchors se dibujan como puntos de color según su kind
// (electrical = verde, thermal_zone = naranja).  Útil para localizar
// dónde el modelo declara que va el terminal A+, etc.
// ===========================================================================
void View3DPanel::renderAsset(ImDrawList* dl, ImVec2 pos, ImVec2 size,
                              const scinodes::DeviceAsset& asset,
                              float shaftAngle,
                              const std::string& partFilter,
                              bool rotateAll,
                              const std::array<float,3>& xyzRotation) {
    auto partAllowed = [&](const std::string& name) {
        return partFilter.empty() || name == partFilter;
    };
    // Defensa contra tamaños degenerados (ImGui puede entregar 0×0 durante
    // transiciones de dock/maximize/detach).  Sin esto, scale=0 más
    // proyección 2D acaba en NaNs que crashean ImDrawList en algunos drivers.
    if (size.x < 4.f || size.y < 4.f) return;

    // Orbit + zoom — mismo patrón que renderMotor.
    bool hov = ImGui::IsWindowHovered();
    if (hov) {
        float w = ImGui::GetIO().MouseWheel;
        if (w != 0.f)
            m_zoom = std::clamp(m_zoom * (1.0f + w * kZoomWheelSensitivity), kZoomMin, kZoomMax);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) m_orbiting = true;
    }
    if (m_orbiting) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            m_azimuth   += d.x * 0.5f;
            m_elevation -= d.y * 0.5f;
            m_elevation  = std::clamp(m_elevation, -89.0f, 89.0f);
        } else m_orbiting = false;
    }

    // Fondo punteado.
    for (float x = 0; x < size.x; x += 22.f)
        for (float y = 0; y < size.y; y += 22.f)
            dl->AddCircleFilled({pos.x+x, pos.y+y}, 0.9f,
                                IM_COL32(45,48,58,130), 4);

    const float D2R  = 3.14159265f / 180.0f;
    const float azR  = m_azimuth   * D2R;
    const float elR  = m_elevation * D2R;

    // ---- Auto-fit: el asset viene en unidades del mundo (metros típicamente),
    // que para un motor son ~0.05–0.06.  Sin renormalizar quedaría diminuto
    // y "lejos" como reportó el usuario.  Calculamos el bbox de todas las
    // partes + anchors y derivamos un factor que mapee el lado mayor a ~1.5,
    // que es el rango cómodo del project() de este panel (focal=3, dz+2.5).
    float lo[3] = {  1e30f,  1e30f,  1e30f };
    float hi[3] = { -1e30f, -1e30f, -1e30f };
    for (const auto& [pn, mesh] : asset.parts) {
        if (!partAllowed(pn)) continue;
        for (size_t i = 0; i + 2 < mesh.positions.size(); i += 3) {
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], mesh.positions[i+k]);
                hi[k] = std::max(hi[k], mesh.positions[i+k]);
            }
        }
    }
    for (const auto& [an, a] : asset.anchors)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], a.position[k]);
            hi[k] = std::max(hi[k], a.position[k]);
        }
    const float span = std::max({ hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2],
                                  1e-6f });
    const float normFactor = (lo[0] < 1e29f) ? (1.5f / span) : 1.0f;
    const float cxw = (lo[0]+hi[0])*0.5f;
    const float cyw = (lo[1]+hi[1])*0.5f;
    const float czw = (lo[2]+hi[2])*0.5f;

    const float scale = std::min(size.x, size.y) * kViewportScaleFraction * m_zoom;
    const ImVec2 ctr = { pos.x + size.x*0.5f, pos.y + size.y*0.5f };

    // ---- Render mode (wireframe / solid / both) ----
    // Acumulamos triángulos en `tris` durante el recorrido por partes y
    // los rasterizamos al final con painter's algorithm — ordenamos por
    // depth medio (en cam-space tras rotación) de lejos a cerca, así
    // ImDrawList los dibuja sin Z-buffer real pero respeta oclusión para
    // mallas convexas o casi convexas.  Si el modo es Wireframe puro,
    // el shading Lambert se omite y sólo se dibujan aristas.
    struct Tri { ImVec2 p[3]; ImU32 fill, edge; float depth; };
    std::vector<Tri> tris;
    tris.reserve(2048);
    const bool wantSolid =
        m_renderMode == RenderMode::Solid || m_renderMode == RenderMode::Both;
    const bool wantWire  =
        m_renderMode == RenderMode::Wireframe || m_renderMode == RenderMode::Both;
    // Dirección de luz fija (cam-space), normalizada: arriba-izquierda
    // hacia la cámara → ilumina el frente del objeto en la pose default.
    const V3 lightDir = { -0.40f, -0.40f, -0.82f };

    for (const auto& [partName, mesh] : asset.parts) {
        if (!partAllowed(partName)) continue;
        // Buscar un joint que tenga esta part como child.
        const scinodes::AssetJointFrame* drv = nullptr;
        for (const auto& [jname, jf] : asset.joints) {
            if (jf.child == partName) { drv = &jf; break; }
        }
        // PATH B (rotateAll=true): si no hay joint en el asset, se
        // sintetiza una rotación Z+ con origen (0,0,0).  PATH A
        // (rotateAll=false): sólo rotan las parts driven por joint.
        const bool applyRot = (drv && drv->type == "revolute") || rotateAll;

        // Rodrigues setup.  Si rotateAll (PATH B): aplicamos Euler XYZ
        // extrínseco usando xyzRotation como vector vec(3) de ángulos
        // (rad).  Si no (PATH A): rotación por joint axis con shaftAngle
        // scalar.  Origin para PATH A viene del joint declarado;
        // PATH B rota alrededor del origen world (0,0,0).
        V3 axis    = {0.f, 1.f, 0.f};
        V3 origin  = {0.f, 0.f, 0.f};
        float cT   = 1.0f, sT = 0.0f;
        if (applyRot && !rotateAll) {
            // PATH A — joint scalar.
            if (drv && drv->type == "revolute") {
                axis   = { drv->axis[0], drv->axis[1], drv->axis[2] };
                float n = std::sqrt(axis.x*axis.x + axis.y*axis.y + axis.z*axis.z);
                if (n > 1e-9f) { axis.x/=n; axis.y/=n; axis.z/=n; }
                origin = { drv->origin[0], drv->origin[1], drv->origin[2] };
            }
            cT = std::cos(shaftAngle);
            sT = std::sin(shaftAngle);
        }

        // PATH B: precomputamos cos/sin de las 3 rotaciones Euler.
        const float cx = std::cos(xyzRotation[0]), sx = std::sin(xyzRotation[0]);
        const float cy = std::cos(xyzRotation[1]), sy = std::sin(xyzRotation[1]);
        const float cz = std::cos(xyzRotation[2]), sz = std::sin(xyzRotation[2]);

        auto applyJoint = [&](V3 v) -> V3 {
            if (!applyRot) return v;
            if (rotateAll) {
                // Euler XYZ extrínseco: Rx, después Ry, después Rz.
                // Rx
                float ay =  v.y*cx - v.z*sx;
                float az =  v.y*sx + v.z*cx;
                // Ry sobre (v.x, ay, az)
                float bx =  v.x*cy + az*sy;
                float bz = -v.x*sy + az*cy;
                // Rz sobre (bx, ay, bz)
                return V3{ bx*cz - ay*sz,
                           bx*sz + ay*cz,
                           bz };
            }
            V3 t = { v.x - origin.x, v.y - origin.y, v.z - origin.z };
            V3 k = { axis.y*t.z - axis.z*t.y,
                     axis.z*t.x - axis.x*t.z,
                     axis.x*t.y - axis.y*t.x };
            float dot = axis.x*t.x + axis.y*t.y + axis.z*t.z;
            V3 r = {
                t.x*cT + k.x*sT + axis.x*dot*(1.0f - cT),
                t.y*cT + k.y*sT + axis.y*dot*(1.0f - cT),
                t.z*cT + k.z*sT + axis.z*dot*(1.0f - cT)
            };
            return { r.x + origin.x, r.y + origin.y, r.z + origin.z };
        };

        // Proyectar todos los vértices.  Guardamos también la posición
        // cam-space (vCS) post-rotación para el painter's sort + el
        // shading Lambert.
        const int nVerts = static_cast<int>(mesh.positions.size() / 3);
        if (nVerts == 0) continue;
        std::vector<ImVec2> proj(nVerts);
        std::vector<V3>     vCS(nVerts);
        for (int i = 0; i < nVerts; ++i) {
            V3 v = { mesh.positions[i*3],
                     mesh.positions[i*3+1],
                     mesh.positions[i*3+2] };
            v = applyJoint(v);
            v.x = (v.x - cxw) * normFactor;
            v.y = (v.y - cyw) * normFactor;
            v.z = (v.z - czw) * normFactor;
            v = rotX(rotY(v, azR), elR);
            vCS[i]  = v;
            proj[i] = project(v, ctr, scale);
        }

        // Color base de la part (RGB float).  Estática usa el tinte
        // térmico (azul → rojo); rotativa siempre ámbar para que el ojo
        // identifique el shaft incluso cuando el housing se tiñe.
        const float baseR = drv ? 1.00f : std::clamp(m_meshTintR, 0.f, 1.f);
        const float baseG = drv ? 0.78f : std::clamp(m_meshTintG, 0.f, 1.f);
        const float baseB = drv ? 0.35f : std::clamp(m_meshTintB, 0.f, 1.f);
        const ImU32 edgeCol = IM_COL32(
            (int)(baseR * 255.f),
            (int)(baseG * 255.f),
            (int)(baseB * 255.f),
            220);

        auto addTriangle = [&](int ia, int ib, int ic) {
            const V3& a = vCS[ia]; const V3& b = vCS[ib]; const V3& c = vCS[ic];
            // Normal en cam-space.
            V3 e1 = { b.x-a.x, b.y-a.y, b.z-a.z };
            V3 e2 = { c.x-a.x, c.y-a.y, c.z-a.z };
            V3 n  = { e1.y*e2.z - e1.z*e2.y,
                      e1.z*e2.x - e1.x*e2.z,
                      e1.x*e2.y - e1.y*e2.x };
            float nl = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
            if (nl < 1e-9f) return;
            n.x /= nl; n.y /= nl; n.z /= nl;
            // Lambert doble-cara (abs del dot) — mallas open pueden tener
            // triángulos orientados al revés.
            float ndotl = std::fabs(n.x*lightDir.x + n.y*lightDir.y + n.z*lightDir.z);
            const float ambient = kAmbientLighting;
            float I = ambient + (1.f - ambient) * std::clamp(ndotl, 0.f, 1.f);
            ImU32 fill = IM_COL32(
                (int)(std::clamp(baseR * I, 0.f, 1.f) * 255.f),
                (int)(std::clamp(baseG * I, 0.f, 1.f) * 255.f),
                (int)(std::clamp(baseB * I, 0.f, 1.f) * 255.f),
                235);
            Tri t;
            t.p[0] = proj[ia]; t.p[1] = proj[ib]; t.p[2] = proj[ic];
            t.fill = fill;
            t.edge = edgeCol;
            t.depth = (a.z + b.z + c.z) * (1.0f/3.0f);
            tris.push_back(t);
        };

        if (!mesh.indices.empty()) {
            for (size_t k = 0; k + 2 < mesh.indices.size(); k += 3) {
                uint32_t a = mesh.indices[k];
                uint32_t b = mesh.indices[k+1];
                uint32_t c = mesh.indices[k+2];
                if ((int)a >= nVerts || (int)b >= nVerts || (int)c >= nVerts) continue;
                addTriangle((int)a, (int)b, (int)c);
            }
        } else {
            for (int i = 0; i + 2 < nVerts; i += 3) addTriangle(i, i+1, i+2);
        }
    }

    // ---- Painter's algorithm: sort por depth DESC (lejos → cerca) y
    //      rasterizar.  Sin Z-buffer real, pero suficiente para mallas
    //      simples (motor, asset 3-D de la tesis).
    std::sort(tris.begin(), tris.end(),
              [](const Tri& a, const Tri& b) { return a.depth > b.depth; });
    for (const Tri& t : tris) {
        if (wantSolid)
            dl->AddTriangleFilled(t.p[0], t.p[1], t.p[2], t.fill);
        if (wantWire) {
            dl->AddLine(t.p[0], t.p[1], t.edge, 0.7f);
            dl->AddLine(t.p[1], t.p[2], t.edge, 0.7f);
            dl->AddLine(t.p[2], t.p[0], t.edge, 0.7f);
        }
    }

    // ---- Anchors: puntos de color, etiquetados ----
    for (const auto& [aname, anchor] : asset.anchors) {
        V3 p = { (anchor.position[0] - cxw) * normFactor,
                 (anchor.position[1] - cyw) * normFactor,
                 (anchor.position[2] - czw) * normFactor };
        p = rotX(rotY(p, azR), elR);
        ImVec2 px = project(p, ctr, scale);

        ImU32 col = IM_COL32(255, 230, 80, 255);  // default amarillo
        if      (anchor.kind == "electrical")   col = IM_COL32( 80, 220,  80, 255);
        else if (anchor.kind == "thermal_zone") col = IM_COL32(220, 100,  60, 255);
        else if (anchor.kind == "mount")        col = IM_COL32(180, 180, 220, 255);
        dl->AddCircleFilled(px, 3.5f, col, 12);
        dl->AddText({ px.x + 5.f, px.y - 7.f },
                    IM_COL32(200, 215, 230, 200), aname.c_str());
    }

    // ---- Gizmo de ejes ----
    renderAxisGizmo(dl, pos, size, azR, elR);

    // ---- HUD con el tipo y conteos ----
    char hud[160];
    std::snprintf(hud, sizeof(hud),
        "Asset: %s   parts=%zu  joints=%zu  anchors=%zu",
        asset.deviceType.c_str(),
        asset.parts.size(), asset.joints.size(), asset.anchors.size());
    // El HUD se reubica abajo a la izquierda para no chocar con los
    // botones del toggle wire/solid/both que ahora viven en la esquina
    // superior izquierda.
    dl->AddText({pos.x + 10, pos.y + size.y - 22.0f},
                IM_COL32(180, 195, 215, 220), hud);
}

// ===========================================================================
// renderAxisGizmo — tres flechas X/Y/Z en la esquina inferior izquierda,
// rotadas con la misma cámara que la escena.  Convención de colores CAD
// estándar: X rojo, Y verde, Z azul.  Sin perspectiva (proyección ortográfica
// directa) para que las longitudes de las flechas sigan siendo comparables
// entre ellas sin importar el zoom de la escena principal.
// ===========================================================================
void View3DPanel::renderAxisGizmo(ImDrawList* dl, ImVec2 /*pos*/, ImVec2 /*size*/,
                                  float azR, float elR) {
    // Anclado a la posición del child window actual para que siga al panel
    // si se redimensiona.  Esquina inferior izquierda, padding 14 px.
    const ImVec2 winPos  = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();
    const float  gscale  = kGizmoScale;
    const ImVec2 gctr    = { winPos.x + gscale + 14.f,
                             winPos.y + winSize.y - gscale - 14.f };

    auto P = [&](V3 v) {
        V3 r = rotX(rotY(v, azR), elR);
        return ImVec2{ gctr.x + r.x * gscale, gctr.y - r.y * gscale };
    };

    const ImVec2 o  = P({0, 0, 0});
    const ImVec2 xT = P({1, 0, 0});
    const ImVec2 yT = P({0, 1, 0});
    const ImVec2 zT = P({0, 0, 1});

    // Fondo semitransparente del gizmo (un pequeño "bastidor" oscuro).
    dl->AddCircleFilled(gctr, gscale + 8.f, IM_COL32(0, 0, 0, 90), 20);

    const ImU32 colX = IM_COL32(220,  60,  60, 255);
    const ImU32 colY = IM_COL32( 80, 200,  80, 255);
    const ImU32 colZ = IM_COL32( 80, 140, 230, 255);

    dl->AddLine(o, xT, colX, 2.0f);
    dl->AddLine(o, yT, colY, 2.0f);
    dl->AddLine(o, zT, colZ, 2.0f);

    // "Cabezas" de las flechas — un pequeño círculo en la punta.
    dl->AddCircleFilled(xT, 2.5f, colX, 8);
    dl->AddCircleFilled(yT, 2.5f, colY, 8);
    dl->AddCircleFilled(zT, 2.5f, colZ, 8);

    // Etiquetas X / Y / Z al lado de cada flecha.
    dl->AddText({ xT.x + 4.f, xT.y - 7.f }, colX, "X");
    dl->AddText({ yT.x + 4.f, yT.y - 7.f }, colY, "Y");
    dl->AddText({ zT.x + 4.f, zT.y - 7.f }, colZ, "Z");
}
