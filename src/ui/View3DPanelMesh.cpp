#include "View3DPanel.hpp"
#include "View3DPanelInternal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// View3DPanelMesh — split de View3DPanel.cpp (etapa 6K.E).  Carga de mallas
// triángulo simples (OBJ / STL ASCII / STL binario), normalización a unit
// bounding box, construcción de aristas y orquestación de tryLoad.  Pura
// utility: no toca rendering, no toca el grafo, no toca el bridge.
// ---------------------------------------------------------------------------

using namespace scinodes::ui::view3d_detail;

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

