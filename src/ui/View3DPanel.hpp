#pragma once
#include "../app/FileDialog.hpp"
#include <array>
#include <imgui.h>
#include <string>
#include <vector>

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
// View3DPanel — interactive wireframe viewer for Stage 1.
//
// Supported formats : .obj (v/f), .stl (ASCII and binary)
// File picker       : native OS dialog (zenity / kdialog) in a thread
// Controls          : LMB-drag to orbit, scroll to zoom
// -----------------------------------------------------------------------
class View3DPanel {
public:
    void draw();

private:
    // ---- file handling ----
    void tryLoad();
    bool parseOBJ(const std::string& path);
    bool parseSTL(const std::string& path);
    void buildEdges(const std::vector<int>& tris);
    void normalizeMesh();

    // ---- rendering ----
    void renderViewport(ImDrawList* dl, ImVec2 pos, ImVec2 size);
    void renderPlaceholder(ImDrawList* dl, ImVec2 pos, ImVec2 size);

    // ---- state ----
    Mesh3D m_mesh;
    char   m_pathBuf[1024] = {};

    float  m_azimuth   =  30.0f;
    float  m_elevation =  20.0f;
    float  m_zoom      =   1.0f;
    bool   m_orbiting  = false;

    FileDialog m_fileDialog;
};
