#include "View3DPanel.hpp"
#include "View3DPanelInternal.hpp"
#include "../core/I18n.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

// Las constantes del visualizador 3D (cámara, viewport, motor procedural,
// thermal, sidecar binario) viven en View3DPanelInternal.hpp.  Centralizadas
// para que el split (etapa 6K.E) las comparta sin duplicar.
using namespace scinodes::ui::view3d_detail;

// 3-D math helpers (V3 + rotX/rotY/project) viven en View3DPanelInternal.hpp
// (etapa 6K.E) — compartidos con View3DPanelAsset.cpp.

// ===========================================================================
// renderViewport
// ===========================================================================
void View3DPanel::renderViewport(ImDrawList* dl, ImVec2 pos, ImVec2 size) {
    // Mouse interaction
    bool hov = ImGui::IsWindowHovered();
    if (hov) {
        float w = ImGui::GetIO().MouseWheel;
        if (w != 0.f)
            m_zoom = std::clamp(m_zoom * (1.0f + w * kZoomWheelSensitivity), kZoomMin, kZoomMax);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_orbiting = true;
    }
    if (m_orbiting) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            m_azimuth   += d.x * 0.5f;
            m_elevation -= d.y * 0.5f;
            m_elevation  = std::clamp(m_elevation, -89.0f, 89.0f);
        } else {
            m_orbiting = false;
        }
    }

    // Dot grid background
    for (float x = 0; x < size.x; x += 22.f)
        for (float y = 0; y < size.y; y += 22.f)
            dl->AddCircleFilled({pos.x+x, pos.y+y}, 0.9f, IM_COL32(45,48,58,130), 4);

    // Projection
    const float D2R  = 3.14159265f / 180.0f;
    float azR  = m_azimuth   * D2R;
    float elR  = m_elevation * D2R;
    float scale = std::min(size.x, size.y) * 0.38f * m_zoom;
    ImVec2 ctr  = { pos.x + size.x*0.5f, pos.y + size.y*0.5f };

    // Pre-project vertices
    int nVerts = (int)m_mesh.verts.size() / 3;
    std::vector<ImVec2> proj(nVerts);
    for (int i = 0; i < nVerts; ++i) {
        V3 v = { m_mesh.verts[i*3], m_mesh.verts[i*3+1], m_mesh.verts[i*3+2] };
        v = rotX(rotY(v, azR), elR);
        proj[i] = project(v, ctr, scale);
    }

    // Draw edges with LOD cap
    int nEdges = (int)m_mesh.edges.size();
    int step   = std::max(1, nEdges / 30000);
    for (int i = 0; i < nEdges; i += step) {
        int a = m_mesh.edges[i][0], b = m_mesh.edges[i][1];
        if (a < nVerts && b < nVerts)
            dl->AddLine(proj[a], proj[b], IM_COL32(90,155,225,170), 0.8f);
    }

    // Annotations
    char info[80];
    std::snprintf(info, sizeof(info), "%d verts   %d faces   %d edges",
                  nVerts, m_mesh.faceCount, nEdges);
    dl->AddText({pos.x+6.f, pos.y+size.y-18.f}, IM_COL32(120,120,135,210), info);

    if (step > 1) {
        char lod[32];
        std::snprintf(lod, sizeof(lod), "LOD 1/%d", step);
        dl->AddText({pos.x+6.f, pos.y+6.f}, IM_COL32(210,170,50,220), lod);
    }

    const char* hint = "LMB-drag: orbit   Scroll: zoom";
    ImVec2 hs = ImGui::CalcTextSize(hint);
    dl->AddText({pos.x+size.x-hs.x-6.f, pos.y+size.y-18.f},
                IM_COL32(90,92,108,170), hint);
}

// ===========================================================================
// renderPlaceholder — DC-motor schematic
// ===========================================================================
static void drawMotorSchematic(ImDrawList* dl, ImVec2 o, float s) {
    auto p = [&](float x, float y) -> ImVec2 { return { o.x+x*s, o.y+y*s }; };
    const ImU32 wire = IM_COL32( 90,200, 90,210);
    const ImU32 box  = IM_COL32(100,160,230,200);
    const ImU32 txt  = IM_COL32(210,210,210,200);
    const float lw   = 1.5f;
    dl->AddRect(p(-8,-1.5f), p(-5, 1.5f), box, 3.f,0,lw);
    dl->AddText(p(-7.8f,-0.5f), txt, "Vs");
    dl->AddLine(p(-5,0), p(-3.5f,0), wire, lw);
    dl->AddRect(p(-3.5f,-2), p(0,2), box, 3.f,0,lw);
    dl->AddText(p(-3.2f,-0.5f), txt, "Armature");
    dl->AddLine(p(0,0), p(1.8f,0), wire, lw);
    dl->AddCircle(p(3,0), 1.2f*s, box, 32, lw);
    dl->AddText(p(2.4f,-0.45f), txt, "M");
    dl->AddLine(p(4.2f,0), p(5.5f,0), wire, lw);
    dl->AddTriangleFilled(p(5.5f,-0.35f), p(5.5f,0.35f), p(6.f,0), wire);
    dl->AddText(p(5.8f,-0.5f), txt, "Load");
    dl->AddLine(p(-1.75f,2), p(-1.75f,3.5f), wire, lw);
    dl->AddRect(p(-3.5f,3.5f), p(0,5), box, 3.f,0,lw);
    dl->AddText(p(-3.1f,3.9f), txt, "Field");
    dl->AddLine(p(-1.75f,5), p(-1.75f,6.5f), wire, lw);
    dl->AddLine(p(-1.75f,6.5f), p(-6.5f,6.5f), wire, lw);
    dl->AddLine(p(-6.5f,6.5f), p(-6.5f,0), wire, lw);
    dl->AddLine(p(-6.5f,0), p(-8,0), wire, lw);
    dl->AddLine(p(3,-1.2f), p(3,-2.8f), wire, lw);
    dl->AddRect(p(1.4f,-4.5f), p(4.6f,-2.8f), box, 3.f,0,lw);
    dl->AddText(p(1.7f,-4.f), txt, "Encoder");
}

void View3DPanel::renderPlaceholder(ImDrawList* dl, ImVec2 pos, ImVec2 size) {
    for (float x = 0; x < size.x; x += 22.f)
        for (float y = 0; y < size.y; y += 22.f)
            dl->AddCircleFilled({pos.x+x, pos.y+y}, 0.9f, IM_COL32(45,48,58,130), 4);

    float sc    = std::min(size.x, size.y) / kMotorGridDivisor;
    ImVec2 orig = { pos.x + size.x*0.52f, pos.y + size.y*0.44f };
    drawMotorSchematic(dl, orig, sc);

    const char* lbl = "Click  Browse  to load a .obj or .stl model";
    ImVec2 ts = ImGui::CalcTextSize(lbl);
    dl->AddText({pos.x + (size.x-ts.x)*0.5f, pos.y + size.y - ts.y - 6.f},
                IM_COL32(90,90,100,180), lbl);
}

// ===========================================================================
// draw — top-level entry point called every frame
// ===========================================================================
// ===========================================================================
// Procedural DC motor — stator/rotor/shaft cylinders built as wireframe.
// Coordinate axis: z runs along the motor's rotation axis.
// ===========================================================================
static void appendRingCylinder(Mesh3D& m,
                               float cx, float cy, float cz,
                               float radius, float halfLen, int segments) {
    const int base = (int)m.verts.size() / 3;
    // Two rings: front face (z = cz + halfLen) and back face (cz - halfLen).
    for (int side = 0; side < 2; ++side) {
        float z = cz + (side ? -halfLen : halfLen);
        for (int i = 0; i < segments; ++i) {
            float a = 2.0f * 3.14159265358979323846f * i / segments;
            m.verts.push_back(cx + radius * std::cos(a));
            m.verts.push_back(cy + radius * std::sin(a));
            m.verts.push_back(z);
        }
    }
    // Circumferential edges on each face.
    for (int side = 0; side < 2; ++side) {
        int sb = base + side * segments;
        for (int i = 0; i < segments; ++i)
            m.edges.push_back({ sb + i, sb + (i + 1) % segments });
    }
    // Axial edges connecting corresponding vertices.
    for (int i = 0; i < segments; ++i)
        m.edges.push_back({ base + i, base + segments + i });
}

void View3DPanel::buildMotor() {
    m_motor = Mesh3D{};
    // Stator (housing): radius 1, length 2.
    appendRingCylinder(m_motor, 0.f, 0.f, 0.f,    1.0f, 1.0f, 32);
    // Rotor (inner core): radius 0.55, length 1.7.
    appendRingCylinder(m_motor, 0.f, 0.f, 0.f,    0.55f, 0.85f, 24);
    // Shaft (sticks out the front face): radius 0.18, length 1.3,
    // centred at z = +0.65 so its front edge lands at z = +1.3.
    appendRingCylinder(m_motor, 0.f, 0.f, kMotorShaftZ, kMotorShaftOdSmall, kMotorShaftZ, kMotorMeshSegments);

    m_motor.faceCount = (int)m_motor.edges.size();
    m_motor.filename  = "procedural DC motor";
    m_motor.loaded    = true;
}

// ===========================================================================
// computeGeometryFromGraph
// Finds a PMSMSizing node and reconstructs its rotor/stator geometry
// analytically (closed-form cube-root sizing equation, same one the
// Scilab driver evaluates each step). Inputs T and omega come from an
// upstream DesignTemplate when one is wired in; otherwise the defaults
// shipped with PMSMSizing are used.
// ===========================================================================
bool View3DPanel::computeGeometryFromGraph(const NodeGraph& graph,
                                            MachineGeometry& out) const {
    const NodeInstance* sizing = nullptr;
    for (const auto& n : graph.nodes())
        if (n.type == NodeType::PMSMSizing) { sizing = &n; break; }
    if (!sizing) return false;

    auto param = [&](const NodeInstance& n, const char* key, double fb) {
        auto it = n.params.find(key);
        return (it != n.params.end()) ? it->second : fb;
    };

    double B     = param(*sizing, "Magnetic Loading B",   0.85);
    double A     = param(*sizing, "Electric Loading A",   40000.0);
    double alpha = param(*sizing, "Aspect Ratio L/D",     1.2);
    int    Ns    = static_cast<int>(param(*sizing, "Slot Count", 12.0));
    int    Np    = static_cast<int>(param(*sizing, "Pole Count",  4.0));

    // Find inputs by walking edges. PMSMSizing input 0 = T, input 1 = omega.
    auto upstream = [&](int port) -> std::pair<int, int> {
        for (const auto& e : graph.edges()) {
            if (e.toNodeId == sizing->id && attrInputPort(e.toAttrId) == port) {
                return { e.fromNodeId, attrOutputPort(e.fromAttrId) };
            }
        }
        return { -1, -1 };
    };

    auto readUpstream = [&](int port, double fb) {
        auto [srcId, srcPort] = upstream(port);
        if (srcId < 0) return fb;
        const NodeInstance* up = graph.findNode(srcId);
        if (!up) return fb;
        if (up->type == NodeType::DesignTemplate) {
            static const char* kPortToParam[4] = {
                "Target Torque", "Target Speed", "Bus Voltage", "Cooling Class"
            };
            if (srcPort >= 0 && srcPort < 4)
                return param(*up, kPortToParam[srcPort], fb);
        }
        return fb;
    };

    double T     = readUpstream(0,  10.0);
    double omega = readUpstream(1, kDefaultUpstreamOmega);
    if (T <= 0.0 || B <= 0.0 || A <= 0.0 || alpha <= 0.0) return false;

    double D = std::cbrt(2.0 * T / (3.14159265358979323846 * B * A * alpha));
    double L = alpha * D;

    out.boreD     = static_cast<float>(D);
    out.stackL    = static_cast<float>(L);
    out.slotCount = std::max(3, Ns);
    out.poleCount = std::max(2, Np);
    (void)omega;   // unused for now; kept for future loss / heat estimates
    return true;
}

// ===========================================================================
// buildMotorFromGeometry — turns a MachineGeometry into a wireframe
// PMSM rotor/stator with slot teeth and rotor pole boundaries. The
// vertex coordinates are normalised to the [-1, 1] cube at the end so
// the existing camera and zoom logic continue to work unchanged.
// ===========================================================================
void View3DPanel::buildMotorFromGeometry(const MachineGeometry& g) {
    constexpr float kPi = 3.14159265358979323846f;
    m_motor = Mesh3D{};

    const float rRotor     = g.boreD * 0.5f;
    const float airgap     = std::max(kMinAirgap, static_cast<float>(g.boreD) * kAirgapFraction);
    const float rStatorIn  = rRotor + airgap;
    const float rStatorOut = rStatorIn + rRotor * kStatorBackIronFraction;     // back-iron ~30% of bore
    const float halfL      = g.stackL * 0.5f;
    const float rotorHalfL = halfL * 0.95f;
    const float shaftR     = std::max(kMinShaftRadius, rRotor * kShaftRadiusFraction);

    int segStator = std::clamp(g.slotCount * 4, 48, 256);
    int segRotor  = std::clamp(g.poleCount * 8, 48, 256);

    appendRingCylinder(m_motor, 0, 0, 0, rStatorOut, halfL, segStator);
    appendRingCylinder(m_motor, 0, 0, 0, rStatorIn,  halfL, segStator);
    appendRingCylinder(m_motor, 0, 0, 0, rRotor,     rotorHalfL, segRotor);
    appendRingCylinder(m_motor, 0, 0, halfL * kShaftDepthFractionA,  shaftR, halfL * kShaftDepthFractionB, kMotorMeshSegments);

    // Slot tooth outlines on the stator inner surface.
    int base = (int)m_motor.verts.size() / 3;
    for (int k = 0; k < g.slotCount; ++k) {
        float a = 2.0f * kPi * static_cast<float>(k) / g.slotCount;
        for (int side = 0; side < 2; ++side) {
            float z = side ? -halfL : halfL;
            m_motor.verts.insert(m_motor.verts.end(), {
                rStatorIn  * std::cos(a), rStatorIn  * std::sin(a), z,
                rStatorOut * std::cos(a), rStatorOut * std::sin(a), z
            });
        }
    }
    int v = base;
    for (int k = 0; k < g.slotCount; ++k) {
        m_motor.edges.push_back({v + 0, v + 1});   // front radial
        m_motor.edges.push_back({v + 2, v + 3});   // back  radial
        m_motor.edges.push_back({v + 0, v + 2});   // inner axial
        m_motor.edges.push_back({v + 1, v + 3});   // outer axial
        v += 4;
    }

    // Rotor pole boundary lines.
    base = (int)m_motor.verts.size() / 3;
    for (int k = 0; k < g.poleCount; ++k) {
        float a = 2.0f * kPi * static_cast<float>(k) / g.poleCount;
        m_motor.verts.insert(m_motor.verts.end(), {
            rRotor * std::cos(a), rRotor * std::sin(a),  rotorHalfL,
            rRotor * std::cos(a), rRotor * std::sin(a), -rotorHalfL
        });
    }
    v = base;
    for (int k = 0; k < g.poleCount; ++k) {
        m_motor.edges.push_back({v + 0, v + 1});
        v += 2;
    }

    // Normalise to the [-1, 1] cube so the orbit camera scale stays sane.
    float maxAbs = 0.0f;
    for (float c : m_motor.verts) maxAbs = std::max(maxAbs, std::fabs(c));
    if (maxAbs > 1e-6f) {
        float k = 1.0f / maxAbs;
        for (float& c : m_motor.verts) c *= k;
    }

    char fn[64];
    std::snprintf(fn, sizeof(fn),
                  "procedural PMSM (D=%.3fm, L=%.3fm, %d slot/%d pole)",
                  g.boreD, g.stackL, g.slotCount, g.poleCount);
    m_motor.filename  = fn;
    m_motor.faceCount = (int)m_motor.edges.size();
    m_motor.loaded    = true;
}

// ===========================================================================
// colorFromTemperature — 3-stop cool→hot gradient
//   t = clamp((T - cold) / (hot - cold), 0, 1)
//   t in [0,   0.5]: blue   → yellow
//   t in [0.5, 1.0]: yellow → red
// ===========================================================================
static void colorFromTemperature(float T, float cold, float hot,
                                  float& r, float& g, float& b) {
    float denom = std::max(1e-3f, hot - cold);
    float t = std::clamp((T - cold) / denom, 0.0f, 1.0f);
    auto lerp = [](float a, float b, float k) { return a + (b - a) * k; };
    if (t < 0.5f) {
        float k = t * 2.0f;
        r = lerp(0.20f, 0.95f, k);
        g = lerp(0.55f, 0.85f, k);
        b = lerp(0.95f, 0.30f, k);
    } else {
        float k = (t - 0.5f) * 2.0f;
        r = lerp(0.95f, 0.95f, k);
        g = lerp(0.85f, 0.30f, k);
        b = lerp(0.30f, 0.25f, k);
    }
}

// ===========================================================================
// currentThermalReading — scan the graph for the first View3DThermalSink
// and read its latest temperature sample. Returns false if no such sink
// exists or its ring buffer is empty (simulation hasn't started yet).
// ===========================================================================
bool View3DPanel::currentThermalReading(const NodeGraph& graph,
                                        const scinodes::ISimSession& bridge,
                                        float& outT, float& outCold,
                                        float& outHot) const {
    const NodeInstance* sink = nullptr;
    for (const auto& n : graph.nodes())
        if (n.type == NodeType::View3DThermalSink) { sink = &n; break; }
    if (!sink) return false;

    int wIdx = bridge.writeIndex(sink->id, 0);
    if (wIdx <= 0) return false;   // no samples yet
    auto buf = bridge.buffer(sink->id, 0);
    if (buf.empty()) return false;
    outT = buf.back();

    auto p = [&](const char* key, double fb) {
        auto it = sink->params.find(key);
        return static_cast<float>(it != sink->params.end() ? it->second : fb);
    };
    outCold = p("Cold Temperature", kDefaultColdTemp);
    outHot  = p("Hot Temperature",  kDefaultHotTemp);
    return true;
}

// ===========================================================================
// currentDeformation — first View3DDeformationSink in the graph + its
// three channel readings (frequency, mode order, amplitude).
// ===========================================================================
bool View3DPanel::currentDeformation(const NodeGraph& graph,
                                     const scinodes::ISimSession& bridge,
                                     float& outFreq, float& outMode,
                                     float& outAmp) const {
    const NodeInstance* sink = nullptr;
    for (const auto& n : graph.nodes())
        if (n.type == NodeType::View3DDeformationSink) { sink = &n; break; }
    if (!sink) return false;

    auto readCh = [&](int ch, float& out) -> bool {
        int w = bridge.writeIndex(sink->id, ch);
        if (w <= 0) return false;
        auto buf = bridge.buffer(sink->id, ch);
        if (buf.empty()) return false;
        out = buf.back();
        (void)w;
        return true;
    };
    return readCh(0, outFreq) && readCh(1, outMode) && readCh(2, outAmp);
}

// ===========================================================================
// renderMotor — draws the static body plus a radial indicator rotated
// by `shaftAngle` around the z-axis on the shaft's front face.
// ===========================================================================
void View3DPanel::renderMotor(ImDrawList* dl, ImVec2 pos, ImVec2 size,
                              float shaftAngle) {
    // Mouse interaction (same as renderViewport).
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

    for (float x = 0; x < size.x; x += 22.f)
        for (float y = 0; y < size.y; y += 22.f)
            dl->AddCircleFilled({pos.x+x, pos.y+y}, 0.9f, IM_COL32(45,48,58,130), 4);

    const float D2R  = 3.14159265f / 180.0f;
    float azR = m_azimuth   * D2R;
    float elR = m_elevation * D2R;
    float scale = std::min(size.x, size.y) * kViewportScaleFraction * m_zoom;
    ImVec2 ctr  = { pos.x + size.x*0.5f, pos.y + size.y*0.5f };

    // Project + draw the static body. Deformation (if active) is
    // applied here on the fly — the radial mode-shape displacement
    //   Δr(θ, t) = amplitude · cos(m·θ) · sin(2π·f·t)
    // scales (x, y) by (1 + Δr / r). Axis vertices (r ≈ 0) are left
    // untouched.
    float defEnvelope = 0.0f;
    if (m_deformActive) {
        float t = static_cast<float>(ImGui::GetTime());
        defEnvelope = std::sin(2.0f * 3.14159265f * m_deformFreq * t);
    }
    int nVerts = (int)m_motor.verts.size() / 3;
    std::vector<ImVec2> proj(nVerts);
    for (int i = 0; i < nVerts; ++i) {
        float x = m_motor.verts[i*3];
        float y = m_motor.verts[i*3+1];
        float z = m_motor.verts[i*3+2];
        if (m_deformActive) {
            float r = std::sqrt(x*x + y*y);
            if (r > 1e-3f) {
                float theta = std::atan2(y, x);
                float dr = m_deformAmp *
                           std::cos(m_deformMode * theta) * defEnvelope;
                float k = 1.0f + dr / r;
                x *= k;
                y *= k;
            }
        }
        V3 v = { x, y, z };
        v = rotX(rotY(v, azR), elR);
        proj[i] = project(v, ctr, scale);
    }
    ImU32 col = IM_COL32(
        static_cast<int>(m_meshTintR * 255.0f),
        static_cast<int>(m_meshTintG * 255.0f),
        static_cast<int>(m_meshTintB * 255.0f),
        200);
    for (const auto& e : m_motor.edges) {
        if (e[0] < nVerts && e[1] < nVerts)
            dl->AddLine(proj[e[0]], proj[e[1]], col, 0.9f);
    }

    // Rotating indicator on the shaft's front face (z = +1.3).
    const float indR = kThermalIndicatorRadius;
    V3 c   = { 0.0f, 0.0f, 1.30f };
    V3 tip = { indR * std::cos(shaftAngle), indR * std::sin(shaftAngle), 1.30f };
    V3 cR  = rotX(rotY(c,   azR), elR);
    V3 tR  = rotX(rotY(tip, azR), elR);
    dl->AddLine(project(cR, ctr, scale), project(tR, ctr, scale),
                IM_COL32(255, 200, 60, 255), 2.5f);
    dl->AddCircleFilled(project(tR, ctr, scale), 3.5f,
                        IM_COL32(255, 220, 90, 255), 12);

    // HUD.
    char info[80];
    std::snprintf(info, sizeof(info),
                  "DC motor (procedural)   shaft = %.3f rad   %.1f°",
                  shaftAngle, shaftAngle * 180.0f / 3.14159265f);
    dl->AddText({pos.x+6.f, pos.y+size.y-18.f},
                IM_COL32(160, 165, 180, 220), info);
    const char* hint = "LMB-drag: orbit   Scroll: zoom";
    ImVec2 hs = ImGui::CalcTextSize(hint);
    dl->AddText({pos.x+size.x-hs.x-6.f, pos.y+size.y-18.f},
                IM_COL32(90,92,108,170), hint);
}

// ===========================================================================
// currentShaftAngle — interpreta la entrada del View3DSink como velocidad
// angular (rad/s) e integra a ángulo en tiempo de pared.
//
// Bug histórico: la versión anterior leía el último valor del sink y lo
// usaba *directamente* como ángulo, así que cuando la simulación
// alcanzaba régimen permanente (ω constante), el ángulo era constante y
// la malla 3D se congelaba.  Lo correcto es ángulo = ∫ ω dt.
// ===========================================================================
float View3DPanel::currentShaftAngle(const NodeGraph& graph,
                                     const scinodes::ISimSession& bridge) {
    // 1) Localizar el View3DSink y leer la velocidad angular más reciente.
    //    Sin sink cableado, fallback a 1 Hz (= 2π rad/s) para que el
    //    panel siga vivo durante la edición del grafo.
    constexpr float TAU = 2.0f * 3.14159265f;
    float omega = 0.0f;   // sin sink o bridge inactivo → el motor no se mueve
    int   currentWi = 0;
    for (const auto& n : graph.nodes()) {
        if (n.type != NodeType::View3DSink) continue;
        int wi = bridge.writeIndex(n.id);
        currentWi = wi;
        if (wi == 0) break;
        const auto buf = bridge.buffer(n.id);
        if (buf.empty()) break;
        omega = buf.back();
        break;
    }

    // El bridge está activo solo en Ready o Running.  En cualquier otro
    // estado (Stopped, NotStarted, Error) los samples viejos siguen en
    // los buffers; ignorarlos y forzar ω = 0 evita que la malla mantenga
    // la velocidad de la corrida anterior.
    const auto status = bridge.status();
    const bool active = status == scinodes::ISimSession::Status::Ready ||
                        status == scinodes::ISimSession::Status::Running;
    if (!active) omega = 0.0f;

    // Detección de reset / cambio de sesión:
    //  (a) writeIndex bajó respecto al último frame → bridge.reset() pasó.
    //  (b) Acabamos de pasar de "activo" a "no activo" → Stop / Error.
    // En ambos casos limpiamos el acumulador para que la malla no arrastre
    // el ángulo de la corrida vieja.
    const bool justBecameInactive = m_wasActive && !active;
    if (currentWi < m_lastSeenWriteIdx || justBecameInactive) {
        m_shaftAngle          = 0.0f;
        m_lastShaftSampleTime = 0.0;
    }
    m_lastSeenWriteIdx = currentWi;
    m_wasActive        = active;

    // 2) Integrar ω·Δt sobre tiempo de pared entre llamadas.  Pause
    //    congela el acumulador; primer frame sin Δt solo inicializa.
    (void)TAU;
    const double now    = static_cast<double>(ImGui::GetTime());
    const bool   paused = bridge.isPaused();
    if (m_lastShaftSampleTime > 0.0 && !paused) {
        const double dt = now - m_lastShaftSampleTime;
        m_shaftAngle += static_cast<float>(static_cast<double>(omega) * dt);
    }
    m_lastShaftSampleTime = now;

    // 3) Mantener el ángulo en [0, 2π) para evitar pérdida de precisión
    //    cuando el motor lleva horas girando a 50 rad/s.
    m_shaftAngle = std::fmod(m_shaftAngle, TAU);
    if (m_shaftAngle < 0.0f) m_shaftAngle += TAU;
    return m_shaftAngle;
}

// Forward decl: la definición vive más abajo junto al renderAsset CPU
// porque toda la lógica de geometría/proyección está agrupada allá.
// `shaftAngle` (rad) se aplica a las parts cuyo nombre coincide con el
// `child` de algún joint con `driven_by` no vacío.  El resto queda fijo.
// Multi-item rendering helpers (paso 5d del refactor 3D).
//
// El SceneCollector puede emitir N renderables; el panel los rendera
// TODOS con escala compartida (sin esto, cada item se normalizaría a
// su propio bbox y housing+shaft saldrían en tamaño absurdo).
//
// Flujo:
//   1. Para cada item: `accumulateAssetBBox(...)` agranda (lo, hi)
//      con sus parts filtradas.
//   2. (cx, cy, cz, halfExt) = derivado del agregado.
//   3. Para cada item: `flattenAssetForVulkan(..., append=true,
//      sharedBBox=...)` apendea sus vértices al buffer global con la
//      escala compartida.
//   4. Una sola upload Vulkan.
// SharedAssetBBox + fwd decls de accumulateAssetBBox / flattenAssetForVulkan
// viven ahora en View3DPanelInternal.hpp (etapa 6K.E).  drawContent los
// usa sin qualifier gracias al `using namespace view3d_detail`.

void View3DPanel::drawContent(const NodeGraph& graph,
                              const scinodes::ISimSession& bridge,
                              const std::unordered_map<int, scinodes::DeviceAsset>& assets,
                              const scinodes::ISceneAssetResolver& sceneResolver) {
    // Pull machine geometry from the graph if a PMSMSizing node exists.
    // When found, rebuild the procedural mesh only on actual change (the
    // user dragging a slot count or bore diameter); otherwise the mesh
    // stays cached frame-to-frame.
    MachineGeometry geom{};
    const bool procedural = computeGeometryFromGraph(graph, geom);

    // Thermal tint — driven by a View3DThermalSink in the graph (if any).
    // Falls back to the cool default colour when no thermal source is wired.
    float T = 0;
    float Tcold = static_cast<float>(kDefaultColdTemp);
    float Thot  = static_cast<float>(kDefaultHotTemp);
    bool hasThermal = currentThermalReading(graph, bridge, T, Tcold, Thot);
    float newR = 0.45f, newG = 0.73f, newB = 1.00f;
    if (hasThermal) colorFromTemperature(T, Tcold, Thot, newR, newG, newB);

    bool tintChanged =
        hasThermal && std::fabs(T - m_lastTintTemp) >= 1.0f;
    if (tintChanged) {
        m_lastTintTemp = T;
        m_meshTintR = newR; m_meshTintG = newG; m_meshTintB = newB;
    } else if (!hasThermal &&
               (m_meshTintR != 0.45f || m_meshTintG != 0.73f ||
                m_meshTintB != 1.00f)) {
        m_meshTintR = 0.45f; m_meshTintG = 0.73f; m_meshTintB = 1.00f;
        m_lastTintTemp = -1.0e9f;
        tintChanged = true;
    }

    if (procedural) {
        bool geomChanged = !m_lastGeomValid || geom != m_lastGeom || !m_motor.loaded;
        if (geomChanged) {
            buildMotorFromGeometry(geom);
            m_lastGeom      = geom;
            m_lastGeomValid = true;
        }
        if ((geomChanged || tintChanged) && m_useVulkan)
            m_vkRenderer.uploadProceduralWireframe(
                m_motor.verts, m_motor.edges,
                m_meshTintR, m_meshTintG, m_meshTintB);

        // Read the latest deformation state from a View3DDeformationSink
        // (if any). Store on the panel so both the CPU renderer and the
        // Vulkan renderer consume the same values.
        float fq = 0, mo = 2, am = 0;
        bool deformActive = currentDeformation(graph, bridge, fq, mo, am)
                         && am > 0.0f;
        m_deformActive = deformActive;
        m_deformFreq   = fq;
        m_deformMode   = mo;
        m_deformAmp    = am;
        if (m_useVulkan)
            m_vkRenderer.setDeformation(deformActive, fq, mo, am);
    } else {
        // Reset the cache when the user removes the sizing node so a
        // re-add later regenerates from scratch.
        if (m_lastGeomValid && m_useVulkan)
            m_vkRenderer.rebuildLegacyMotor();
        m_lastGeomValid = false;
        if (!m_motor.loaded) buildMotor();
    }

    // Focus-follows-mouse estilo Blender (ver NodeCanvas para racional).
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsAnyItemActive()) {
        ImGui::SetWindowFocus();
    }

    // (Legacy: Load/Browse de archivos OBJ/STL fue removido — los
    // assets 3D ahora se cargan exclusivamente desde el panel del
    // nodo Device vía Cargar modelo 3D…  Esa ruta valida contra el
    // contrato y mantiene el binding sidecar.  La maquinaria
    // tryLoad/parseOBJ/parseSTL queda en el .cpp por si otros nodos
    // la requieren a futuro, pero la UI desaparece de aquí.)

    // ---- 3-D viewport -----------------------------------------------------
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 15, 20, 255));
    ImGui::BeginChild("##vp3d", avail, false,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos     = ImGui::GetWindowPos();
    ImVec2 wsize   = ImGui::GetWindowSize();

    // ---- Compute primary asset BEFORE drawing anything visible -----------
    //
    // Coexistencia temporal (paso 5b del refactor 3D):
    //   • PATH B (nuevo): collectScene() recorre los SceneOutput del
    //     grafo y produce SceneRenderables resueltos contra el catálogo
    //     by-name vía sceneResolver.  Si hay al menos uno con asset
    //     resuelto, lo usamos como primary — toma precedencia.
    //   • PATH A (viejo): scan de nodos Device → asset cached by
    //     nodeId.  Mantiene los .scn 0.4 funcionando sin migración.
    //
    // El renderer wireframe + Vulkan no sabe la diferencia: ambos
    // entregan un `const DeviceAsset*` que renderAsset / Vulkan
    // pipeline consumen tal cual.  Cuando PATH B aporta transforms
    // (paso 5c los leerá del bridge), se compondrán encima de
    // shaftAngle; por ahora son identidad y el rendering es idéntico.
    const auto sceneItems = scinodes::collectScene(graph, sceneResolver, &bridge);

    // Filtro: items con asset resuelto y válido.
    std::vector<const scinodes::SceneRenderable*> pathBItems;
    for (const auto& item : sceneItems) {
        if (item.asset && item.asset->valid())
            pathBItems.push_back(&item);
    }
    const bool pathB = !pathBItems.empty();

    // primary se conserva por compat con el bloque de fallback PATH A
    // (DeviceAsset por nodeId) y para `m_assetUploaded` que cachea el
    // último upload Vulkan.
    const scinodes::DeviceAsset* primary = nullptr;
    if (pathB) {
        primary = pathBItems.front()->asset;
    } else {
        for (const auto& n : graph.nodes()) {
            if (defOf(n).category != NodeCategory::Device) continue;
            auto it = assets.find(n.id);
            if (it == assets.end() || !it->second.valid()) continue;
            primary = &it->second;
            break;
        }
    }
    if (!primary && m_assetUploaded) {
        // Liberar la referencia antes de que el AssetService la evict.
        m_vkRenderer.clearAsset();
        m_assetUploaded = nullptr;
    }

    // ---- Background pass: rasterizar el contenido del viewport
    //      ANTES de los botones, para que los botones queden por encima.
    //      Decisión de path:
    //        • Asset + Vulkan vivo  → Vulkan (depth real + Lambert).
    //        • Asset sin Vulkan     → renderAsset CPU (painter's; artefactos).
    //        • Sin asset, .obj/.stl → renderViewport (path histórico).
    //        • Sin nada             → placeholder textual.
    bool renderedViaVulkan = false;
    const bool useVulkanForAsset =
        primary && m_useVulkan && wsize.x > 4 && wsize.y > 4;
    if (useVulkanForAsset) {
        m_vkRenderer.resize((uint32_t)wsize.x, (uint32_t)wsize.y);
        if (m_vkRenderer.ready()) {
            // Re-upload cada frame porque la rotación del eje (shaftAngle)
            // va cocida en las posiciones.  Mesh chica → costo trivial.
            std::vector<float>    positions;
            std::vector<float>    normals;
            std::vector<uint32_t> indices;

            if (pathB) {
                // Multi-item: bbox compartida → flatten append por
                // item.  Cada item lleva su propia rotación (ya
                // integrada upstream por TransformObject + bridge).
                float lo[3] = {  1e30f,  1e30f,  1e30f };
                float hi[3] = { -1e30f, -1e30f, -1e30f };
                for (auto* it : pathBItems)
                    accumulateAssetBBox(*it->asset, it->partName, lo, hi);
                SharedAssetBBox bb;
                bb.cx = 0.5f * (lo[0] + hi[0]);
                bb.cy = 0.5f * (lo[1] + hi[1]);
                bb.cz = 0.5f * (lo[2] + hi[2]);
                bb.halfExt = std::max({
                    0.5f * (hi[0] - lo[0]),
                    0.5f * (hi[1] - lo[1]),
                    0.5f * (hi[2] - lo[2]),
                    1e-6f });
                positions.clear(); normals.clear(); indices.clear();
                for (auto* it : pathBItems) {
                    flattenAssetForVulkan(*it->asset,
                                          /*shaftAngle=*/0.f,  // unused in PATH B
                                          positions, normals, indices,
                                          it->partName,
                                          /*rotateAll=*/true,
                                          /*appendMode=*/true,
                                          /*sharedBBox=*/&bb,
                                          /*xyzRotation=*/it->rotation);
                }
            } else {
                // PATH A: un único asset, sin partFilter, joints del
                // contrato deciden qué rota.  shaftAngle vía
                // integración wall-clock desde View3DSink.
                flattenAssetForVulkan(*primary, currentShaftAngle(graph, bridge),
                                      positions, normals, indices);
            }
            // shaftAngle "representativo" para la cámara y el HUD.
            const float shaftAngle = pathB
                                       ? pathBItems.front()->rotation[2]
                                       : currentShaftAngle(graph, bridge);
            m_vkRenderer.uploadAssetMesh(positions, normals, indices,
                                         m_meshTintR, m_meshTintG, m_meshTintB);
            m_assetUploaded = primary;
            m_assetUploadedTint[0] = m_meshTintR;
            m_assetUploadedTint[1] = m_meshTintG;
            m_assetUploadedTint[2] = m_meshTintB;
            // Propagar el modo (Wire/Solid/Both) del UI toggle.
            using ARM = Vulkan3DRenderer::AssetRenderMode;
            m_vkRenderer.setAssetRenderMode(
                m_renderMode == RenderMode::Wireframe ? ARM::Wire :
                m_renderMode == RenderMode::Solid     ? ARM::Solid :
                                                        ARM::Both);
            m_vkRenderer.render(shaftAngle,
                                m_azimuth, m_elevation, m_zoom);
            // dl->AddImage en lugar de ImGui::Image — así los siguientes
            // ImGui widgets (los botones de modo) se rasterizan ENCIMA.
            dl->AddImage(m_vkRenderer.imguiTextureId(),
                         pos, { pos.x + wsize.x, pos.y + wsize.y });
            // Gizmo de ejes XYZ — overlay ImGui sobre el frame Vulkan.
            // En el path CPU (renderAsset) se dibuja desde dentro; aquí
            // lo invocamos directamente para que ambos paths tengan la
            // referencia angular visible en la esquina inferior izq.
            constexpr float kD2R_local = 3.14159265f / 180.0f;
            renderAxisGizmo(dl, pos, wsize,
                            m_azimuth   * kD2R_local,
                            m_elevation * kD2R_local);
            renderedViaVulkan = true;
        }
    }
    if (!renderedViaVulkan) {
        if (primary) {
            // CPU path: renderea el PRIMER item (con su rotación y
            // partFilter) — multi-item con escala compartida en CPU es
            // trabajo a futuro.  El path Vulkan SÍ rendera todos.
            const bool   firstIsB    = pathB && !pathBItems.empty();
            const float  shaftAngle  = firstIsB
                                         ? pathBItems.front()->rotation[2]
                                         : currentShaftAngle(graph, bridge);
            const std::string filter = firstIsB
                                         ? pathBItems.front()->partName
                                         : std::string{};
            renderAsset(dl, pos, wsize, *primary,
                        shaftAngle, filter,
                        /*rotateAll=*/firstIsB);
        } else if (m_mesh.loaded) {
            renderViewport(dl, pos, wsize);
        } else {
            const std::string& msgS = scinodes::tr("view3d.no_geometry");
            const std::string& subS = scinodes::tr("view3d.no_geometry_hint");
            const char* msg = msgS.c_str();
            const char* sub = subS.c_str();
            ImVec2 ts1 = ImGui::CalcTextSize(msg);
            ImVec2 ts2 = ImGui::CalcTextSize(sub);
            float cx = pos.x + wsize.x * 0.5f;
            float cy = pos.y + wsize.y * 0.5f;
            dl->AddText({cx - ts1.x*0.5f, cy - ts1.y - 4.f},
                        IM_COL32(180, 190, 210, 230), msg);
            dl->AddText({cx - ts2.x*0.5f, cy + 4.f},
                        IM_COL32(130, 140, 160, 200), sub);
        }
    }

    // ---- Mouse interaction sobre el rect del viewport (solo Vulkan path,
    //      el CPU path ya maneja el suyo dentro de renderAsset).
    if (renderedViaVulkan) {
        const ImVec2 rectMax = { pos.x + wsize.x, pos.y + wsize.y };
        const bool hov = ImGui::IsMouseHoveringRect(pos, rectMax);
        if (hov) {
            float w = ImGui::GetIO().MouseWheel;
            if (w != 0.f)
                m_zoom = std::clamp(m_zoom * (1.0f + w * kZoomWheelSensitivity), kZoomMin, kZoomMax);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsAnyItemHovered())   // no robar clicks de los botones
                m_orbiting = true;
        }
        if (m_orbiting) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                ImVec2 d = ImGui::GetIO().MouseDelta;
                m_azimuth   += d.x * 0.5f;
                m_elevation -= d.y * 0.5f;
                m_elevation  = std::clamp(m_elevation, -89.0f, 89.0f);
            } else m_orbiting = false;
        }
    }

    // ---- Foreground pass: Toggle wireframe / solid / both en la esquina
    //      superior izquierda, sobre la imagen del Vulkan.  Botones
    //      regulares con padding generoso para que el texto sea legible
    //      sobre el fondo del viewport.
    {
        const float pad = 8.0f;
        ImGui::SetCursorScreenPos({ pos.x + pad, pos.y + pad });
        ImGui::PushStyleColor(ImGuiCol_Text,
            IM_COL32(235, 240, 250, 255));
        ImGui::PushStyleColor(ImGuiCol_Button,
            IM_COL32(35, 40, 52, 230));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            IM_COL32(60, 75, 100, 245));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            IM_COL32(80, 130, 200, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        auto modeBtn = [&](const char* label, RenderMode m) {
            const bool active = (m_renderMode == m);
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button,
                    IM_COL32(75, 130, 200, 255));
            if (ImGui::Button(label))
                m_renderMode = m;
            if (active) ImGui::PopStyleColor();
        };
        modeBtn(scinodes::tr("view3d.wire").c_str(),  RenderMode::Wireframe); ImGui::SameLine();
        modeBtn(scinodes::tr("view3d.solid").c_str(), RenderMode::Solid);     ImGui::SameLine();
        modeBtn(scinodes::tr("view3d.both").c_str(),  RenderMode::Both);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

