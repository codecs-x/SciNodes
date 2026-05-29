#pragma once
#include "../app/FileDialog.hpp"
#include "../app/Vulkan3DRenderer.hpp"
#include "../core/DeviceAsset.hpp"
#include "../core/NodeGraph.hpp"
#include "../core/ScilabBridge.hpp"
#include <array>
#include <cmath>
#include <imgui.h>
#include <string>
#include <unordered_map>
#include <vector>

class VulkanContext;

// -----------------------------------------------------------------------
// Mesh3D — loaded geometry normalised to [-1, 1] bounding cube.
// -----------------------------------------------------------------------
struct Mesh3D {
    std::vector<float>              verts;      // x,y,z triples
    std::vector<std::array<int,2>>  edges;      // unique wireframe edges
    int         faceCount = 0;
    bool        loaded    = false;
    std::string filename;
    std::string error;
};

// -----------------------------------------------------------------------
// MachineGeometry — output of analytical sizing, consumed by the 3-D
// panel to procedurally generate a rotor / stator wireframe.
// -----------------------------------------------------------------------
struct MachineGeometry {
    float boreD     = 0.10f;   // rotor outer diameter (m)
    float stackL    = 0.12f;   // axial stack length   (m)
    int   slotCount = 12;      // stator slot count
    int   poleCount = 4;       // rotor pole count

    bool operator==(const MachineGeometry& o) const {
        // Hash-equal under typical UI drag precision (~1 mm / 0.1 mm).
        auto neq = [](float a, float b) { return std::fabs(a - b) > 1e-5f; };
        if (neq(boreD,  o.boreD))  return false;
        if (neq(stackL, o.stackL)) return false;
        return slotCount == o.slotCount && poleCount == o.poleCount;
    }
    bool operator!=(const MachineGeometry& o) const { return !(*this == o); }
};

// -----------------------------------------------------------------------
// View3DPanel — interactive 3-D viewport.
//
// Two display modes coexist:
//
//   • Loaded mesh   .obj/.stl picked by the user (existing path).
//   • Procedural motor  built once at startup, animated by the most
//                       recent value of a View3DSink in the graph
//                       (or by wall-clock time when none is wired).
//
// File picker  : native OS dialog (zenity / kdialog) in a thread
// Controls     : LMB-drag to orbit, scroll to zoom
// -----------------------------------------------------------------------
class View3DPanel {
public:
    // Set up the Vulkan offscreen renderer. Safe to call once after
    // AppWindow finishes ImGui's Vulkan backend init.
    void initVulkan(VulkanContext& ctx) { m_useVulkan = m_vkRenderer.init(ctx); }

    // Release Vulkan resources. AppWindow MUST call this before tearing
    // down VulkanContext so the renderer's destruction doesn't try to
    // touch dead device handles.
    void releaseVulkan() { m_vkRenderer.shutdown(); m_useVulkan = false; }

    // Called once per frame. `graph`/`bridge` give access to la simulación
    // (ω por el View3DSink). `assets` permite que el panel dibuje los
    // modelos 3D cargados en los nodos Device en vez de la malla
    // procedural hardcodeada — fallback al procedural cuando no hay
    // ningún asset válido en el grafo.
    void draw(const NodeGraph& graph,
              const ScilabBridge& bridge,
              const std::unordered_map<int, scinodes::DeviceAsset>& assets);

private:
    // ---- file handling ----
    void tryLoad();
    bool parseOBJ(const std::string& path);
    bool parseSTL(const std::string& path);
    void buildEdges(const std::vector<int>& tris);
    void normalizeMesh();

    // ---- procedural motor ----
    void buildMotor();   // legacy: hardcoded DC-motor cylinders
    void buildMotorFromGeometry(const MachineGeometry& g);   // v0.8 PMSM

    // Scan `graph` for a PMSMSizing node and reconstruct its rotor / stator
    // geometry analytically (same cube-root formula as ScilabCodeGen, but
    // evaluated in C++ so the panel doesn't have to wait for the bridge).
    // Inputs T and omega are pulled from an upstream DesignTemplate's
    // params when present; otherwise defaults are used.
    //
    // Returns true and fills `out` when a PMSMSizing was found.
    bool computeGeometryFromGraph(const NodeGraph& graph,
                                  MachineGeometry& out) const;

    // ---- rendering ----
    void renderViewport(ImDrawList* dl, ImVec2 pos, ImVec2 size);
    void renderMotor   (ImDrawList* dl, ImVec2 pos, ImVec2 size,
                        float shaftAngle);

    // Render de un DeviceAsset como wireframe.  Itera cada part, aplica
    // la transformación del joint que la mueve (si lo es), proyecta en
    // 2D con la misma cámara orbit de renderMotor.  Las partes que son
    // `parent` de joints quedan estáticas.  TODO: por ahora un solo
    // joint a la vez (shaftAngle único).
    void renderAsset   (ImDrawList* dl, ImVec2 pos, ImVec2 size,
                        const scinodes::DeviceAsset& asset,
                        float shaftAngle);
    void renderPlaceholder(ImDrawList* dl, ImVec2 pos, ImVec2 size);

    // Mini-gizmo en la esquina inferior izquierda: tres flechas
    // X (rojo) / Y (verde) / Z (azul) rotadas con la misma cámara
    // (azR/elR) que la escena.  Sin perspectiva — su única función
    // es darle al usuario referencia angular para entender en qué
    // dirección está mirando, tipo CAD.
    void renderAxisGizmo(ImDrawList* dl, ImVec2 pos, ImVec2 size,
                         float azR, float elR);

    // Read the latest angular velocity (rad/s) from the View3DSink in the
    // graph and integrate it over wall-clock time to produce a shaft angle.
    // If no sink is wired, fall back to 2π rad/s (= 1 Hz) so the motor
    // still spins as a "panel alive" hint.
    //
    // NON-const: mutates m_shaftAngle / m_lastShaftSampleTime accumulators.
    float currentShaftAngle(const NodeGraph& graph,
                            const ScilabBridge& bridge);

    // Find a View3DThermalSink and read its latest temperature sample.
    // Returns true on hit and fills [out] {temperature, cold, hot}
    // (Cold/Hot copied from the sink's params).
    bool currentThermalReading(const NodeGraph& graph,
                               const ScilabBridge& bridge,
                               float& outT, float& outCold,
                               float& outHot) const;

    // Find a View3DDeformationSink and return the (frequency, mode,
    // amplitude) tuple from its three channels. Returns true if the
    // sink exists and has at least one recorded sample.
    bool currentDeformation(const NodeGraph& graph,
                            const ScilabBridge& bridge,
                            float& outFreq, float& outMode,
                            float& outAmp) const;

    // ---- state ----
    Mesh3D m_mesh;          // user-loaded OBJ/STL (if any)
    Mesh3D m_motor;         // procedural stator/rotor wireframe
    char   m_pathBuf[1024] = {};

    float  m_azimuth   =  30.0f;
    float  m_elevation =  20.0f;
    float  m_zoom      =   1.0f;
    bool   m_orbiting  = false;

    FileDialog m_fileDialog;

    // Vulkan offscreen path. Falls back to the CPU projection if init fails.
    Vulkan3DRenderer m_vkRenderer;
    bool             m_useVulkan = false;

    // Acumulador del ángulo de eje. currentShaftAngle integra ω·dt cada
    // frame; m_lastShaftSampleTime = 0 marca "primer frame", donde no hay
    // delta y solo se inicializa.
    // m_lastSeenWriteIdx detecta resets del bridge: si el writeIndex del
    // sink cae respecto al frame anterior, sabemos que arrancó una nueva
    // sesión y reseteamos el ángulo para no arrastrar la posición vieja.
    float  m_shaftAngle          = 0.0f;
    double m_lastShaftSampleTime = 0.0;
    int    m_lastSeenWriteIdx    = 0;
    bool   m_wasActive           = false;   // bridge activo frame anterior

    // Procedural-mesh state — non-zero defaults so the change-detection
    // comparison still triggers a build on the first frame.
    MachineGeometry m_lastGeom{};
    bool            m_lastGeomValid = false;

    // Thermal-tint cache. The mesh is re-uploaded with a fresh colour
    // only when |T - m_lastTintTemp| >= 1 K (or the geometry just
    // changed), keeping VBO traffic well under one upload per second
    // for typical thermal time constants.
    float m_lastTintTemp   = -1.0e9f;   // sentinel: definitely triggers
    float m_meshTintR      = 0.45f;     // bright rotor-blue default
    float m_meshTintG      = 0.73f;
    float m_meshTintB      = 1.00f;

    // Deformation overlay (Stage v1.0 Phase 2) — driven from a
    // View3DDeformationSink in the graph. Applied per-vertex on the
    // CPU rendering path; the Vulkan renderer reads the same values
    // via setDeformation() and rewrites the VBO before each submit.
    bool  m_deformActive = false;
    float m_deformFreq   = 0.0f;
    float m_deformMode   = 2.0f;
    float m_deformAmp    = 0.0f;
};
