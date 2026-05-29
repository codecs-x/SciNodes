#pragma once
#include "NodeCanvas.hpp"

// -----------------------------------------------------------------------------
// OutlinerPanel — vista jerárquica de los nodos físicos (NodeCategory::Device)
// con sus assets 3D cargados.
//
// Inspirado en el Outliner de Blender / Tree View de FreeCAD.  Por cada
// nodo Device:
//
//   📦 DCMotor #4  motor.gltf   ✓ contrato OK
//      ├ parts:    shaft, housing
//      ├ joints:   shaft_bearing (revolute, drives omega)
//      └ anchors:  terminal_plus, terminal_minus  (winding optional ausente)
//
// Cada fila tiene botones para recargar el asset desde disco o quitarlo
// del nodo.
//
// No mantiene estado propio: lee de NodeCanvas (graph + caché de assets)
// y actúa sobre él mediante los métodos públicos detachAsset/reloadAsset.
// -----------------------------------------------------------------------------
class OutlinerPanel {
public:
    // Renderiza el contenido del Outliner SIN ImGui::Begin/End — el
    // host (Area) abre y cierra el window.  La lógica de
    // focus-follows-mouse vive aquí porque depende del window actual.
    void drawContent(NodeCanvas& canvas);
};
