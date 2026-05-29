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

    // Project + draw the static body.
    int nVerts = (int)m_motor.verts.size() / 3;
    std::vector<ImVec2> proj(nVerts);
    for (int i = 0; i < nVerts; ++i) {
        V3 v = { m_motor.verts[i*3], m_motor.verts[i*3+1], m_motor.verts[i*3+2] };
        v = rotX(rotY(v, azR), elR);
        proj[i] = project(v, ctr, scale);
    }
    for (const auto& e : m_motor.edges) {
        if (e[0] < nVerts && e[1] < nVerts)
            dl->AddLine(proj[e[0]], proj[e[1]], IM_COL32(100, 165, 230, 200), 0.9f);
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
// Pull the most recent shaft angle from a View3DSink in the graph,
// or fall back to wall-clock time so the motor spins even before
// any node is wired.
// ===========================================================================
float View3DPanel::currentShaftAngle(const NodeGraph& graph,
                                     const ScilabBridge& bridge) const {
    for (const auto& n : graph.nodes()) {
        if (n.type != NodeType::View3DSink) continue;
        int wi = bridge.writeIndex(n.id);
        if (wi == 0) continue;
        const auto buf = bridge.buffer(n.id);
        if (buf.empty()) continue;
        return buf[(wi - 1) % ScilabBridge::BUFFER_SIZE];
    }
    // No View3DSink wired or no data yet — spin at 1 Hz.
    return 2.0f * 3.14159265f *
           static_cast<float>(ImGui::GetTime()) * 1.0f;
}

void View3DPanel::draw(const NodeGraph& graph, const ScilabBridge& bridge) {
    if (!m_motor.loaded) buildMotor();

    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(18, 18, 22, 255));
    ImGui::Begin("3D View");

    // ---- Poll for dialog result (non-blocking) -----------------------------
    if (std::string picked = m_fileDialog.take(); !picked.empty()) {
        std::strncpy(m_pathBuf, picked.c_str(), sizeof(m_pathBuf)-1);
        m_pathBuf[sizeof(m_pathBuf)-1] = '\0';
        tryLoad();
    }

    // ---- Header row -------------------------------------------------------
    //   [path input — Enter to load ] [ Load ] [ Browse... / Opening... ]
    bool busy = m_fileDialog.isOpen();

    float btnW    = 52.f;
    float browseW = busy ? 96.f : 80.f;
    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x - btnW - browseW - 10.f);

    bool enterPressed = ImGui::InputText("##mpath", m_pathBuf, sizeof(m_pathBuf),
                                         ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Load", {btnW, 0.f}) || enterPressed)
        tryLoad();

    ImGui::SameLine();
    if (busy) {
        ImGui::BeginDisabled();
        ImGui::Button("Opening...", {browseW, 0.f});
        ImGui::EndDisabled();
    } else {
        if (ImGui::Button("  Browse...  ", {browseW, 0.f}))
            m_fileDialog.open(FileDialog::Mode::Open,
                              "Load 3D Model",
                              {"3D Models (*.obj, *.stl)", "*.obj *.stl"});
    }

    // ---- Status / error row -----------------------------------------------
    if (m_mesh.loaded) {
        ImGui::TextDisabled("  %s   |   %d verts   %d faces   %d edges",
                            m_mesh.filename.c_str(),
                            (int)m_mesh.verts.size() / 3,
                            m_mesh.faceCount,
                            (int)m_mesh.edges.size());
    } else if (!m_mesh.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 80, 80, 255));
        ImGui::TextUnformatted(("  " + m_mesh.error).c_str());
        ImGui::PopStyleColor();
    }

    // ---- 3-D viewport -----------------------------------------------------
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(14, 15, 20, 255));
    ImGui::BeginChild("##vp3d", avail, false,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos     = ImGui::GetWindowPos();
    ImVec2 wsize   = ImGui::GetWindowSize();

    if (m_mesh.loaded) {
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

    ImGui::End();
    ImGui::PopStyleColor();
}
