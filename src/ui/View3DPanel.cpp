#include "View3DPanel.hpp"
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

namespace {

// ===========================================================================
// Constantes locales del visualizador 3D.  Antes vivían como literales
// dispersos en el archivo; centralizadas aquí para que el panel sea
// auditable de un vistazo (cada número con su unidad / justificación).
// ===========================================================================

// ---- Camera & projection ------------------------------------------------
constexpr float kCameraFovDeg          = 45.f;        // perspective FOV vertical
constexpr float kCameraNear            = 0.1f;
constexpr float kCameraFar             = 100.f;
constexpr float kCameraFocalDistance   = 3.0f;        // proyección manual ASCII-3D
constexpr float kCameraSceneZ          = 2.5f;        // offset z de la escena
constexpr float kMinCameraDepthDz      = 0.01f;       // umbral para evitar /0 cerca de la cámara
constexpr float kMinFocalDepth         = 0.001f;
constexpr float kZoomMin               = 0.05f;
constexpr float kZoomMax               = 20.0f;
constexpr float kZoomWheelSensitivity  = 0.12f;       // delta por tick de rueda

// ---- Layout de viewport y gizmo -----------------------------------------
constexpr float kViewportScaleFraction = 0.30f;       // 30% del mínimo (w, h) para encajar el modelo
constexpr float kGizmoScale            = 28.0f;       // tamaño base del gizmo de ejes
constexpr float kMotorGridDivisor      = 28.f;        // escala del grid del motor (proporción de panel)
constexpr float kAmbientLighting       = 0.30f;       // intensidad ambiental en shade procedural

// ---- Geometría procedural del motor -------------------------------------
// Cuando no hay asset glTF cargado, se dibuja un motor procedural con
// estas proporciones (relativas al radio del rotor o al panel).  Los
// valores se eligieron para que un motor "tipo cilindrico cerrado"
// quede visible y proporcionado a la escala default.
constexpr int   kMotorMeshSegments         = 12;      // segments por anillo de cilindro
constexpr float kMotorShaftZ               = 0.65f;
constexpr float kMotorShaftOdSmall         = 0.18f;   // OD del eje
constexpr float kStatorBackIronFraction    = 0.6f;    // hierro estator = 60% del radio rotor
constexpr float kShaftRadiusFraction       = 0.30f;   // radio eje = 30% del rotor
constexpr float kMinShaftRadius            = 0.005f;  // 5 mm absoluto
constexpr float kMinAirgap                 = 0.001f;  // 1 mm absoluto
constexpr float kAirgapFraction            = 0.01f;   // o 1% del bore (mayor de los dos)
constexpr float kShaftDepthFractionA       = 0.7f;
constexpr float kShaftDepthFractionB       = 0.6f;

// ---- Visualización térmica ----------------------------------------------
constexpr double kDefaultColdTemp        = 290.0;     // K (17 °C)
constexpr double kDefaultHotTemp         = 390.0;     // K (117 °C)
constexpr float  kThermalIndicatorRadius = 0.30f;     // proporción del viewport

// ---- Default upstream (cuando una entrada queda desconectada) -----------
constexpr double kDefaultUpstreamOmega   = 150.0;     // rad/s

// ---- Formato binario del sidecar 3D (motor heatmap) ---------------------
// Header de 84 bytes (cabecera estándar v0.9) + N bins de 50 bytes.
// Si fsize == 84 + binCount·50, el archivo es válido.
constexpr size_t kMotor3dHeaderSize = 84;
constexpr size_t kMotor3dBinSize    = 50;

}  // namespace

// ===========================================================================
// 3-D math helpers
// ===========================================================================
struct V3 { float x, y, z; };

static V3 rotY(V3 v, float r) {
    float c = cosf(r), s = sinf(r);
    return { c*v.x + s*v.z, v.y, -s*v.x + c*v.z };
}
static V3 rotX(V3 v, float r) {
    float c = cosf(r), s = sinf(r);
    return { v.x, c*v.y - s*v.z, s*v.y + c*v.z };
}
static ImVec2 project(V3 v, ImVec2 ctr, float scale) {
    const float dz = v.z + kCameraSceneZ;
    const float d  = (dz > kMinCameraDepthDz) ? kCameraFocalDistance / dz
                                              : kMinFocalDepth;
    return { ctr.x + v.x * scale * d, ctr.y - v.y * scale * d };
}

// ===========================================================================
// Mesh helpers
// ===========================================================================
void View3DPanel::normalizeMesh() {
    int n = (int)m_mesh.verts.size() / 3;
    if (n == 0) return;
    float lo[3] = { FLT_MAX,  FLT_MAX,  FLT_MAX };
    float hi[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], m_mesh.verts[i*3+k]);
            hi[k] = std::max(hi[k], m_mesh.verts[i*3+k]);
        }
    float cx = (lo[0]+hi[0])*0.5f, cy = (lo[1]+hi[1])*0.5f, cz = (lo[2]+hi[2])*0.5f;
    float range = std::max({ hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2] });
    float inv   = (range > 1e-9f) ? 2.0f / range : 1.0f;
    for (int i = 0; i < n; ++i) {
        m_mesh.verts[i*3  ] = (m_mesh.verts[i*3  ] - cx) * inv;
        m_mesh.verts[i*3+1] = (m_mesh.verts[i*3+1] - cy) * inv;
        m_mesh.verts[i*3+2] = (m_mesh.verts[i*3+2] - cz) * inv;
    }
}

void View3DPanel::buildEdges(const std::vector<int>& tris) {
    std::set<std::array<int,2>> es;
    for (int i = 0; i < (int)tris.size(); i += 3) {
        int a = tris[i], b = tris[i+1], c = tris[i+2];
        auto add = [&](int u, int v) {
            if (u > v) std::swap(u, v);
            es.insert({u, v});
        };
        add(a,b); add(b,c); add(c,a);
    }
    m_mesh.edges.clear();
    m_mesh.edges.reserve(es.size());
    for (const auto& e : es) m_mesh.edges.push_back({e[0], e[1]});
}

// ===========================================================================
// OBJ parser
// ===========================================================================
static int objVI(const char* tok, int nv) {
    int vi = std::atoi(tok);
    if (vi < 0) vi = nv + vi + 1;
    return vi - 1;
}

bool View3DPanel::parseOBJ(const std::string& path) {
    std::ifstream f(path);
    if (!f) { m_mesh.error = "Cannot open file."; return false; }

    std::vector<std::array<float,3>> rv;
    std::vector<int> tris;
    std::string line;

    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.size() >= 2 && line[0] == 'v' && line[1] == ' ') {
            float x=0, y=0, z=0;
            std::sscanf(line.c_str()+2, "%f %f %f", &x, &y, &z);
            rv.push_back({x, y, z});
        } else if (line.size() >= 2 && line[0] == 'f' && line[1] == ' ') {
            std::vector<int> fv;
            const char* p = line.c_str()+2;
            while (*p) {
                while (*p == ' ') ++p;
                if (!*p) break;
                fv.push_back(objVI(p, (int)rv.size()));
                while (*p && *p != ' ') ++p;
            }
            for (int i = 2; i < (int)fv.size(); ++i) {
                tris.push_back(fv[0]);
                tris.push_back(fv[i-1]);
                tris.push_back(fv[i]);
            }
        }
    }

    if (rv.empty()) { m_mesh.error = "No vertices found."; return false; }
    m_mesh.verts.clear();
    m_mesh.verts.reserve(rv.size()*3);
    for (const auto& v : rv) {
        m_mesh.verts.push_back(v[0]);
        m_mesh.verts.push_back(v[1]);
        m_mesh.verts.push_back(v[2]);
    }
    m_mesh.faceCount = (int)tris.size() / 3;
    normalizeMesh();
    buildEdges(tris);
    return true;
}

// ===========================================================================
// STL parser  (auto-detects ASCII vs binary by file-size check)
// ===========================================================================
bool View3DPanel::parseSTL(const std::string& path) {
    std::ifstream fb(path, std::ios::binary | std::ios::ate);
    if (!fb) { m_mesh.error = "Cannot open file."; return false; }
    std::streamsize fsize = fb.tellg();

    bool isBinary = false;
    std::uint32_t binCount = 0;
    if (fsize >= (std::streamsize)kMotor3dHeaderSize) {
        fb.seekg(kMotor3dHeaderSize - 4);   // last 4 bytes of header = bin count
        fb.read(reinterpret_cast<char*>(&binCount), 4);
        if (fsize == (std::streamsize)(kMotor3dHeaderSize + binCount * kMotor3dBinSize))
            isBinary = true;
    }

    m_mesh.verts.clear();
    std::vector<int> tris;

    if (isBinary) {
        if (binCount > 10'000'000) { m_mesh.error = "STL too large."; return false; }
        fb.seekg(kMotor3dHeaderSize);
        tris.reserve(binCount * 3);
        m_mesh.verts.reserve(binCount * 9);
        for (std::uint32_t i = 0; i < binCount; ++i) {
            float n3[3], v[3][3]; std::uint16_t attr;
            fb.read(reinterpret_cast<char*>(n3), 12);
            for (int j = 0; j < 3; ++j) {
                fb.read(reinterpret_cast<char*>(v[j]), 12);
                tris.push_back((int)m_mesh.verts.size() / 3);
                m_mesh.verts.push_back(v[j][0]);
                m_mesh.verts.push_back(v[j][1]);
                m_mesh.verts.push_back(v[j][2]);
            }
            fb.read(reinterpret_cast<char*>(&attr), 2);
        }
        m_mesh.faceCount = binCount;
    } else {
        std::ifstream fa(path);
        if (!fa) { m_mesh.error = "Cannot open file."; return false; }
        std::string line;
        while (std::getline(fa, line)) {
            size_t s = line.find_first_not_of(" \t\r");
            if (s == std::string::npos) continue;
            if (line.compare(s, 6, "vertex") == 0) {
                float x=0, y=0, z=0;
                std::sscanf(line.c_str()+s+7, "%f %f %f", &x, &y, &z);
                tris.push_back((int)m_mesh.verts.size() / 3);
                m_mesh.verts.push_back(x);
                m_mesh.verts.push_back(y);
                m_mesh.verts.push_back(z);
            }
        }
        m_mesh.faceCount = (int)m_mesh.verts.size() / 9;
    }

    if (m_mesh.verts.empty()) { m_mesh.error = "No geometry found."; return false; }
    normalizeMesh();
    buildEdges(tris);
    return true;
}

// ===========================================================================
// tryLoad
// ===========================================================================
void View3DPanel::tryLoad() {
    std::string path(m_pathBuf);
    while (!path.empty() &&
           (path.back() == '\n' || path.back() == '\r' || path.back() == ' '))
        path.pop_back();
    if (path.empty()) return;

    m_mesh = Mesh3D{};

    std::string ext;
    size_t dot = path.rfind('.');
    if (dot != std::string::npos) {
        ext = path.substr(dot+1);
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    }

    bool ok = false;
    if      (ext == "obj") ok = parseOBJ(path);
    else if (ext == "stl") ok = parseSTL(path);
    else { m_mesh.error = "Unsupported format — use .obj or .stl"; return; }

    if (ok) {
        m_mesh.loaded = true;
        size_t sep    = path.rfind('/');
        m_mesh.filename = (sep != std::string::npos) ? path.substr(sep+1) : path;
        m_zoom      = 1.0f;
        m_azimuth   = 30.0f;
        m_elevation = 20.0f;
    }
}

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
struct SharedAssetBBox {
    float cx = 0, cy = 0, cz = 0;
    float halfExt = 1.f;
};
static void accumulateAssetBBox(const scinodes::DeviceAsset& asset,
                                const std::string& partFilter,
                                float lo[3], float hi[3]);

static void flattenAssetForVulkan(const scinodes::DeviceAsset& asset,
                                  float                shaftAngle,
                                  std::vector<float>&    outPositions,
                                  std::vector<float>&    outNormals,
                                  std::vector<uint32_t>& outIndices,
                                  const std::string&     partFilter = "",
                                  bool                   rotateAll  = false,
                                  bool                   appendMode = false,
                                  const SharedAssetBBox* sharedBBox = nullptr,
                                  const std::array<float,3>& xyzRotation = {0.f,0.f,0.f});

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
static void accumulateAssetBBox(const scinodes::DeviceAsset& asset,
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

static void flattenAssetForVulkan(const scinodes::DeviceAsset& asset,
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
