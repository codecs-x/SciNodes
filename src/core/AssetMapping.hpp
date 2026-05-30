#pragma once

#include <array>
#include <string>
#include <unordered_map>

// -----------------------------------------------------------------------------
// AssetMapping — sidecar JSON que vincula una geometría 3D externa (glTF/GLB)
// a las ranuras del contrato del dispositivo, SIN tocar el archivo de
// geometría.
//
// Motivación: la convención `extras.scinodes` del contrato requiere que el
// asset glTF haya sido autoreado con metadata (típicamente desde Blender o
// pygltflib).  Eso descarta archivos provenientes de SolidWorks, FreeCAD,
// Thingiverse, etc., aunque la geometría sea perfectamente válida.  El
// sidecar resuelve esa fricción: el usuario carga un glTF sin metadata,
// SciNodes muestra un panel de mapping que liste los nodos del archivo, y
// guarda esta estructura en `<asset>.mapping.json` al lado del original.
//
// Convención de precedencia (vive en DeviceAssetLoader::load):
//   1. Si existe sidecar para el path → se usa el sidecar y se ignora
//      `extras.scinodes` del glTF.
//   2. Si NO existe sidecar → camino legacy (lee extras del glTF).
//
// El sidecar es texto plano editable a mano; el power user puede saltarse
// el panel.
// -----------------------------------------------------------------------------
namespace scinodes {

struct AssetMapping {
    // Versión del esquema.  Se incrementa si en el futuro cambia la forma
    // del JSON; loadFromString rechaza versiones que no reconoce.
    std::string version = "0.1";

    // Ruta del asset a la que pertenece este mapping.  Informativo —
    // permite detectar inconsistencias si el usuario renombra el .gltf
    // sin renombrar el sidecar.  Relativo (basename) preferentemente.
    std::string asset_path;

    // Tipo de dispositivo cuyo contrato debería satisfacerse.  Redundante
    // con el campo del .scn pero hace al sidecar autocontenido.
    std::string device_type;

    // Una part vincula un slot del contrato a un nodo glTF que sirve
    // como contenedor de la malla (parts heredan node.mesh).
    struct PartSlot {
        // Nombre del nodo glTF.  Vacío = slot no asignado (no entra al
        // DeviceAsset al cargar; si el slot es required, se reporta en
        // missing).
        std::string node_name;
    };

    // Un joint vincula un slot del contrato a un nodo glTF cuyo
    // node.translation es el origen del eje.
    struct JointSlot {
        std::string          node_name;
        // Eje en coordenadas del frame post-export (glTF Y-up).  Solo se
        // usa cuando axis_explicit = true; cuando es false el loader lo
        // deriva de node.rotation (mismo fallback que para
        // assets con extras sin scinodes.axis explícito).
        std::array<float, 3> axis           = {{ 0.f, 1.f, 0.f }};
        bool                 axis_explicit  = false;
    };

    // Un anchor vincula un slot del contrato a un nodo glTF cuyo
    // node.translation es el punto del ancla.
    struct AnchorSlot {
        std::string node_name;
    };

    std::unordered_map<std::string, PartSlot>   parts;
    std::unordered_map<std::string, JointSlot>  joints;
    std::unordered_map<std::string, AnchorSlot> anchors;

    bool empty() const {
        return parts.empty() && joints.empty() && anchors.empty();
    }

    // ---- I/O ---------------------------------------------------------------
    static AssetMapping loadFromString(const std::string& jsonText,
                                       std::string*       err = nullptr);
    static AssetMapping loadFromFile  (const std::string& path,
                                       std::string*       err = nullptr);

    std::string toJsonString() const;
    bool        saveToFile(const std::string& path,
                           std::string*       err = nullptr) const;

    // Dado el path de un asset glTF/GLB, devuelve la ruta convencional de
    // su sidecar.  Ejemplos:
    //   "foo/dc_motor.gltf" → "foo/dc_motor.mapping.json"
    //   "foo/dc_motor.glb"  → "foo/dc_motor.mapping.json"
    //   "dc_motor"          → "dc_motor.mapping.json"
    static std::string sidecarPathFor(const std::string& assetPath);
};

}  // namespace scinodes
