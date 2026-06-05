#pragma once

// ---------------------------------------------------------------------------
// View3DPanelInternal — header privado para el split de View3DPanel.cpp
// (etapa 6K.E).
//
// View3DPanel se partió en 4 .cpp por responsabilidad — todos comparten
// las mismas constantes de viewport / cámara / motor procedural y unos
// pocos forward decls de helpers privados.  Centralizamos acá para que
// cualquier .cpp del split los pueda usar sin redeclarar.
//
// Este header NO es API pública del panel — sólo lo deben incluir los
// archivos `View3DPanel*.cpp`.  Los headers públicos (`View3DPanel.hpp`)
// no lo arrastran.
// ---------------------------------------------------------------------------

#include "../core/DeviceAsset.hpp"

#include <imgui.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace scinodes::ui::view3d_detail {

// ===========================================================================
// Camera & projection
// ===========================================================================
inline constexpr float kCameraFovDeg          = 45.f;        // perspective FOV vertical
inline constexpr float kCameraNear            = 0.1f;
inline constexpr float kCameraFar             = 100.f;
inline constexpr float kCameraFocalDistance   = 3.0f;        // proyección manual ASCII-3D
inline constexpr float kCameraSceneZ          = 2.5f;        // offset z de la escena
inline constexpr float kMinCameraDepthDz      = 0.01f;       // umbral para evitar /0 cerca de la cámara
inline constexpr float kMinFocalDepth         = 0.001f;
inline constexpr float kZoomMin               = 0.05f;
inline constexpr float kZoomMax               = 20.0f;
inline constexpr float kZoomWheelSensitivity  = 0.12f;       // delta por tick de rueda

// ===========================================================================
// Layout de viewport y gizmo
// ===========================================================================
inline constexpr float kViewportScaleFraction = 0.30f;       // 30% del mínimo (w, h) para encajar el modelo
inline constexpr float kGizmoScale            = 28.0f;       // tamaño base del gizmo de ejes
inline constexpr float kMotorGridDivisor      = 28.f;        // escala del grid del motor (proporción de panel)
inline constexpr float kAmbientLighting       = 0.30f;       // intensidad ambiental en shade procedural

// ===========================================================================
// Geometría procedural del motor (cuando no hay asset glTF cargado)
// ===========================================================================
inline constexpr int   kMotorMeshSegments         = 12;      // segments por anillo de cilindro
inline constexpr float kMotorShaftZ               = 0.65f;
inline constexpr float kMotorShaftOdSmall         = 0.18f;   // OD del eje
inline constexpr float kStatorBackIronFraction    = 0.6f;    // hierro estator = 60% del radio rotor
inline constexpr float kShaftRadiusFraction       = 0.30f;   // radio eje = 30% del rotor
inline constexpr float kMinShaftRadius            = 0.005f;  // 5 mm absoluto
inline constexpr float kMinAirgap                 = 0.001f;  // 1 mm absoluto
inline constexpr float kAirgapFraction            = 0.01f;   // o 1% del bore (mayor de los dos)
inline constexpr float kShaftDepthFractionA       = 0.7f;
inline constexpr float kShaftDepthFractionB       = 0.6f;

// ===========================================================================
// Visualización térmica
// ===========================================================================
inline constexpr double kDefaultColdTemp        = 290.0;     // K (17 °C)
inline constexpr double kDefaultHotTemp         = 390.0;     // K (117 °C)
inline constexpr float  kThermalIndicatorRadius = 0.30f;     // proporción del viewport

// ===========================================================================
// Default upstream (cuando una entrada queda desconectada)
// ===========================================================================
inline constexpr double kDefaultUpstreamOmega   = 150.0;     // rad/s

// ===========================================================================
// Formato binario del sidecar 3D (motor heatmap)
// ===========================================================================
// Header de 84 bytes (cabecera estándar v0.9) + N bins de 50 bytes.
// Si fsize == 84 + binCount·50, el archivo es válido.
inline constexpr std::size_t kMotor3dHeaderSize = 84;
inline constexpr std::size_t kMotor3dBinSize    = 50;

// ===========================================================================
// 3-D math helpers compartidos por todos los renderers (etapa 6K.E).
// ===========================================================================
//
// V3 (tres floats), rotaciones por eje y proyección perspectiva manual.
// Antes vivían como `static` file-scope en View3DPanel.cpp; ahora `inline`
// en el header para que los .cpp del split (Motor + Asset) los compartan
// sin duplicar definiciones.
//
// Convención de cámara: la escena vive en z=kCameraSceneZ con foco en
// kCameraFocalDistance.  Para evitar divisiones cerca de la cámara,
// dz < kMinCameraDepthDz se reemplaza por kMinFocalDepth.
struct V3 { float x, y, z; };

inline V3 rotY(V3 v, float r) {
    float c = std::cos(r), s = std::sin(r);
    return { c * v.x + s * v.z, v.y, -s * v.x + c * v.z };
}
inline V3 rotX(V3 v, float r) {
    float c = std::cos(r), s = std::sin(r);
    return { v.x, c * v.y - s * v.z, s * v.y + c * v.z };
}
inline ImVec2 project(V3 v, ImVec2 ctr, float scale) {
    const float dz = v.z + kCameraSceneZ;
    const float d  = (dz > kMinCameraDepthDz) ? kCameraFocalDistance / dz
                                              : kMinFocalDepth;
    return { ctr.x + v.x * scale * d, ctr.y - v.y * scale * d };
}

// ===========================================================================
// SharedAssetBBox + fwd decls de helpers para asset rendering
// ===========================================================================
//
// Antes vivían como statics file-scope arriba de drawContent (que los
// usa).  Centralizando acá para que drawContent (en View3DPanel.cpp) y
// las implementaciones (en View3DPanelAsset.cpp) las compartan sin
// duplicar declaraciones.
struct SharedAssetBBox {
    float cx = 0, cy = 0, cz = 0;
    float halfExt = 1.f;
};

void accumulateAssetBBox(const scinodes::DeviceAsset& asset,
                         const std::string& partFilter,
                         float lo[3], float hi[3]);

void flattenAssetForVulkan(const scinodes::DeviceAsset& asset,
                           float                  shaftAngle,
                           std::vector<float>&    outPositions,
                           std::vector<float>&    outNormals,
                           std::vector<uint32_t>& outIndices,
                           const std::string&     partFilter = "",
                           bool                   rotateAll  = false,
                           bool                   appendMode = false,
                           const SharedAssetBBox* sharedBBox = nullptr,
                           const std::array<float,3>& xyzRotation    = {0.f,0.f,0.f},
                           const std::array<float,3>& xyzTranslation = {0.f,0.f,0.f},
                           const std::array<float,3>& xyzScale       = {1.f,1.f,1.f},
                           const std::array<float,3>& xyzPivot       = {0.f,0.f,0.f});

}  // namespace scinodes::ui::view3d_detail
