#pragma once
#include "ContractRegistry.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// -----------------------------------------------------------------------------
// DeviceAsset — un modelo 3D cargado desde un .gltf, validado contra el
// contrato del tipo de dispositivo (DCMotor, PMSM, LinearActuator, ...).
//
// Convención sobre el glTF: cada nodo del scene graph que represente una
// part/joint/anchor lleva en `extras.scinodes` los campos:
//
//   extras.scinodes = {
//     "role":  "part" | "joint" | "anchor",
//     "name":  "<nombre que coincide con una entrada del contrato>",
//     "axis":  [x, y, z]   // sólo para role=joint, en coords locales del nodo
//   }
//
// Posiciones/orientaciones de joints y anchors se leen de la translation
// del nodo glTF.  Las meshes de las parts se leen del nodo.mesh referenciado.
//
// Spec completa: doc/geometry-contracts-design.md  (sección 4).
// -----------------------------------------------------------------------------
namespace scinodes {

// Geometría cruda de una part.  Se llena con los atributos POSITION (y
// opcionalmente NORMAL) de las primitivas del mesh, intercalados como
// floats planos.  Indices están separados.  No se hace deduplicación ni
// nada elegante: el consumidor (View3DPanel) puede subirlo a la GPU
// directamente o calcular AABBs sobre él.
struct AssetMesh {
    std::vector<float>    positions;   // 3 floats por vértice
    std::vector<float>    normals;     // 3 floats por vértice (puede ser vacío)
    std::vector<uint32_t> indices;     // triángulos (puede ser vacío si non-indexed)
    bool empty() const { return positions.empty(); }
};

// Frame cinemático de un joint: dónde está y cómo orienta.
// `origin` viene del campo node.translation del glTF.
// `axis`   viene de extras.scinodes.axis (default Z+).
// `parent`/`child`/`type`/`driven_by` se copian del contrato — el
// glTF NO los redeclara (vienen ya del contrato por nombre).
struct AssetJointFrame {
    std::array<float, 3> origin    = {{ 0.f, 0.f, 0.f }};
    std::array<float, 3> axis      = {{ 0.f, 0.f, 1.f }};
    std::string          parent;
    std::string          child;
    std::string          type;        // "revolute", "prismatic", ...
    std::string          driven_by;   // p.ej. "omega"
};

// Punto publicado (terminal eléctrico, hotspot térmico, etc.).
struct AssetAnchor {
    std::array<float, 3> position = {{ 0.f, 0.f, 0.f }};
    std::string          kind;        // "electrical" | "thermal_zone" | ...
};

struct DeviceAsset {
    std::string path;          // ruta del .gltf en disco (informativo)
    std::string deviceType;    // copiado del contrato (p.ej. "DCMotor")

    std::unordered_map<std::string, AssetMesh>       parts;
    std::unordered_map<std::string, AssetJointFrame> joints;
    std::unordered_map<std::string, AssetAnchor>     anchors;

    // Entradas required del contrato que NO aparecieron en el glTF.
    // Si está vacío, el asset cumple su contrato.
    std::vector<std::string> missing;

    // Entradas optional que faltaron, o nombres en extras.scinodes que
    // no corresponden a ninguna entrada del contrato (typos del autor).
    std::vector<std::string> warnings;

    bool valid() const { return missing.empty(); }
};

class DeviceAssetLoader {
public:
    // Parsea el .gltf en `path` y valida contra `contract`.
    //
    // - Acepta .gltf (JSON) y .glb (binario), detectado por extensión.
    // - Si tinygltf falla al parsear, *err lleva el mensaje y el
    //   DeviceAsset retornado tiene todos los required del contrato en
    //   `missing` (lo que permite mostrar un error útil sin checks
    //   adicionales).
    // - Nombres duplicados en extras.scinodes (dos nodos con el mismo
    //   "name" para la misma "role") quedan registrados como warning;
    //   prevalece el primero encontrado.
    static DeviceAsset load(const std::string&    path,
                            const DeviceContract& contract,
                            std::string*          err = nullptr);

    // Variante CONTRACT-LESS para entradas del catálogo de objetos 3D
    // (ver `doc/3d_scene_graph_design.md` §8: geometría se desacopla del
    // contrato).  Lee TODOS los nodos del glTF que tengan mesh y los
    // expone como `parts` indexadas por el `name` del nodo glTF (o
    // "part_N" si el nodo es anónimo).  No valida nada; no rellena
    // joints ni anchors; `missing` siempre queda vacío.
    //
    // Pensado para Menú Archivo → Importar modelo 3D, donde el usuario
    // sólo quiere ver el mesh, no validar dinámicas físicas.
    //
    // `deviceType` del asset resultante es "Catalog" — sentinel para que
    // el resto del código pueda detectar que esta entrada no salió de
    // un contrato.
    static DeviceAsset loadCatalog(const std::string& path,
                                   std::string*       err = nullptr);
};

}  // namespace scinodes
