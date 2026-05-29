#include "View3DPanel.hpp"

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
    const float focal = 3.0f;
    float dz = v.z + 2.5f;
    float d  = (dz > 0.01f) ? focal / dz : 0.001f;
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
    if (fsize >= 84) {
        fb.seekg(80);
        fb.read(reinterpret_cast<char*>(&binCount), 4);
        if (fsize == 84 + (std::streamsize)binCount * 50)
            isBinary = true;
    }

    m_mesh.verts.clear();
    std::vector<int> tris;

    if (isBinary) {
        if (binCount > 10'000'000) { m_mesh.error = "STL too large."; return false; }
        fb.seekg(84);
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
            m_zoom = std::clamp(m_zoom * (1.0f + w * 0.12f), 0.05f, 20.0f);
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

    float sc    = std::min(size.x, size.y) / 28.f;
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
    appendRingCylinder(m_motor, 0.f, 0.f, 0.65f,  0.18f, 0.65f, 12);

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
            if (e.toNodeId == sizing->id && (e.toAttrId % 10000) == port) {
                int srcPort = (e.fromAttrId % 10000) - 9000;
                return { e.fromNodeId, srcPort };
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
    double omega = readUpstream(1, 150.0);
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
    const float airgap     = std::max(0.001f, g.boreD * 0.01f);
    const float rStatorIn  = rRotor + airgap;
    const float rStatorOut = rStatorIn + rRotor * 0.6f;     // back-iron ~30% of bore
    const float halfL      = g.stackL * 0.5f;
    const float rotorHalfL = halfL * 0.95f;
    const float shaftR     = std::max(0.005f, rRotor * 0.30f);

    int segStator = std::clamp(g.slotCount * 4, 48, 256);
    int segRotor  = std::clamp(g.poleCount * 8, 48, 256);

    appendRingCylinder(m_motor, 0, 0, 0, rStatorOut, halfL, segStator);
    appendRingCylinder(m_motor, 0, 0, 0, rStatorIn,  halfL, segStator);
    appendRingCylinder(m_motor, 0, 0, 0, rRotor,     rotorHalfL, segRotor);
    appendRingCylinder(m_motor, 0, 0, halfL * 0.7f,  shaftR, halfL * 0.6f, 12);

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
                                        const ScilabBridge& bridge,
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
    outT = buf[(wIdx - 1) % ScilabBridge::BUFFER_SIZE];

    auto p = [&](const char* key, double fb) {
        auto it = sink->params.find(key);
        return static_cast<float>(it != sink->params.end() ? it->second : fb);
    };
    outCold = p("Cold Temperature", 290.0);
    outHot  = p("Hot Temperature",  390.0);
    return true;
}

// ===========================================================================
// currentDeformation — first View3DDeformationSink in the graph + its
// three channel readings (frequency, mode order, amplitude).
// ===========================================================================
bool View3DPanel::currentDeformation(const NodeGraph& graph,
                                     const ScilabBridge& bridge,
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
        out = buf[(w - 1) % ScilabBridge::BUFFER_SIZE];
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
            m_zoom = std::clamp(m_zoom * (1.0f + w * 0.12f), 0.05f, 20.0f);
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
    float scale = std::min(size.x, size.y) * 0.30f * m_zoom;
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
    const float indR = 0.30f;
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
                                     const ScilabBridge& bridge) {
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
        omega = buf[(wi - 1) % ScilabBridge::BUFFER_SIZE];
        break;
    }

    // El bridge está activo solo en Ready o Running.  En cualquier otro
    // estado (Stopped, NotStarted, Error) los samples viejos siguen en
    // los buffers; ignorarlos y forzar ω = 0 evita que la malla mantenga
    // la velocidad de la corrida anterior.
    const auto status = bridge.status();
    const bool active = status == ScilabBridge::Status::Ready ||
                        status == ScilabBridge::Status::Running;
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

void View3DPanel::drawContent(const NodeGraph& graph,
                              const ScilabBridge& bridge,
                              const std::unordered_map<int, scinodes::DeviceAsset>& assets) {
    // Pull machine geometry from the graph if a PMSMSizing node exists.
    // When found, rebuild the procedural mesh only on actual change (the
    // user dragging a slot count or bore diameter); otherwise the mesh
    // stays cached frame-to-frame.
    MachineGeometry geom{};
    const bool procedural = computeGeometryFromGraph(graph, geom);

    // Thermal tint — driven by a View3DThermalSink in the graph (if any).
    // Falls back to the cool default colour when no thermal source is wired.
    float T = 0, Tcold = 290, Thot = 390;
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

    // ---- Prefer asset-bound rendering if any Device node has a valid
    // asset loaded.  Fallback al m_mesh / Vulkan procedural / CPU
    // procedural cuando no hay ninguno.  TODO multi-device: por ahora
    // solo el primero se renderiza; los demás Devices quedan
    // invisibles aunque tengan asset.
    const scinodes::DeviceAsset* primary = nullptr;
    for (const auto& n : graph.nodes()) {
        if (defOf(n).category != NodeCategory::Device) continue;
        auto it = assets.find(n.id);
        if (it == assets.end() || !it->second.valid()) continue;
        primary = &it->second;
        break;
    }

    if (primary) {
        renderAsset(dl, pos, wsize, *primary,
                    currentShaftAngle(graph, bridge));
    } else if (m_mesh.loaded) {
        renderViewport(dl, pos, wsize);
    } else if (m_useVulkan && wsize.x > 4 && wsize.y > 4) {
        // Hand off to the offscreen Vulkan renderer: resize, dispatch
        // commands, then display the resulting texture via ImGui::Image.
        m_vkRenderer.resize((uint32_t)wsize.x, (uint32_t)wsize.y);
        if (m_vkRenderer.ready()) {
            m_vkRenderer.render(currentShaftAngle(graph, bridge),
                                m_azimuth, m_elevation, m_zoom);
            ImGui::SetCursorScreenPos(pos);
            ImGui::Image(m_vkRenderer.imguiTextureId(), wsize);

            // Mouse interaction still happens on the ImGui-side widget.
            bool hov = ImGui::IsItemHovered();
            if (hov) {
                float w = ImGui::GetIO().MouseWheel;
                if (w != 0.f)
                    m_zoom = std::clamp(m_zoom * (1.0f + w * 0.12f), 0.05f, 20.0f);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
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
        } else {
            renderMotor(dl, pos, wsize, currentShaftAngle(graph, bridge));
        }
    } else {
        renderMotor(dl, pos, wsize, currentShaftAngle(graph, bridge));
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
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
                              float shaftAngle) {
    // Defensa contra tamaños degenerados (ImGui puede entregar 0×0 durante
    // transiciones de dock/maximize/detach).  Sin esto, scale=0 más
    // proyección 2D acaba en NaNs que crashean ImDrawList en algunos drivers.
    if (size.x < 4.f || size.y < 4.f) return;

    // Orbit + zoom — mismo patrón que renderMotor.
    bool hov = ImGui::IsWindowHovered();
    if (hov) {
        float w = ImGui::GetIO().MouseWheel;
        if (w != 0.f)
            m_zoom = std::clamp(m_zoom * (1.0f + w * 0.12f), 0.05f, 20.0f);
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

    const float scale = std::min(size.x, size.y) * 0.30f * m_zoom;
    const ImVec2 ctr = { pos.x + size.x*0.5f, pos.y + size.y*0.5f };

    // ---- por cada part: aplicar joint que la mueve (si la mueve) ----
    for (const auto& [partName, mesh] : asset.parts) {
        // Buscar un joint que tenga esta part como child.
        const scinodes::AssetJointFrame* drv = nullptr;
        for (const auto& [jname, jf] : asset.joints) {
            if (jf.child == partName) { drv = &jf; break; }
        }

        // Rodrigues: prepara axis normalizado + sen/cos del ángulo.
        V3 axis    = {0.f, 0.f, 1.f};
        V3 origin  = {0.f, 0.f, 0.f};
        float cT   = 1.0f, sT = 0.0f;
        if (drv && drv->type == "revolute") {
            axis   = { drv->axis[0], drv->axis[1], drv->axis[2] };
            float n = std::sqrt(axis.x*axis.x + axis.y*axis.y + axis.z*axis.z);
            if (n > 1e-9f) { axis.x/=n; axis.y/=n; axis.z/=n; }
            origin = { drv->origin[0], drv->origin[1], drv->origin[2] };
            cT = std::cos(shaftAngle);
            sT = std::sin(shaftAngle);
        }

        auto applyJoint = [&](V3 v) -> V3 {
            if (!drv) return v;
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

        // Proyectar todos los vértices.  Tras applyJoint() recentramos al
        // bbox y normalizamos (auto-fit), porque el asset viene en unidades
        // del mundo real (metros).
        const int nVerts = static_cast<int>(mesh.positions.size() / 3);
        if (nVerts == 0) continue;
        std::vector<ImVec2> proj(nVerts);
        for (int i = 0; i < nVerts; ++i) {
            V3 v = { mesh.positions[i*3],
                     mesh.positions[i*3+1],
                     mesh.positions[i*3+2] };
            v = applyJoint(v);
            v.x = (v.x - cxw) * normFactor;
            v.y = (v.y - cyw) * normFactor;
            v.z = (v.z - czw) * normFactor;
            v = rotX(rotY(v, azR), elR);
            proj[i] = project(v, ctr, scale);
        }

        // Color: ámbar para piezas que rotan, azul claro para estáticas.
        const ImU32 col = drv
            ? IM_COL32(255, 200,  90, 220)
            : IM_COL32(170, 195, 230, 220);

        // Wireframe a partir de triángulos.
        if (!mesh.indices.empty()) {
            for (size_t k = 0; k + 2 < mesh.indices.size(); k += 3) {
                uint32_t a = mesh.indices[k];
                uint32_t b = mesh.indices[k+1];
                uint32_t c = mesh.indices[k+2];
                if ((int)a >= nVerts || (int)b >= nVerts || (int)c >= nVerts) continue;
                dl->AddLine(proj[a], proj[b], col, 0.9f);
                dl->AddLine(proj[b], proj[c], col, 0.9f);
                dl->AddLine(proj[c], proj[a], col, 0.9f);
            }
        } else {
            // Sin índices: triángulo-soup secuencial.
            for (int i = 0; i + 2 < nVerts; i += 3) {
                dl->AddLine(proj[i],   proj[i+1], col, 0.9f);
                dl->AddLine(proj[i+1], proj[i+2], col, 0.9f);
                dl->AddLine(proj[i+2], proj[i],   col, 0.9f);
            }
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
    dl->AddText({pos.x + 10, pos.y + 8},
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
    const float  gscale  = 28.0f;
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
