#pragma once
#include "DeviceAsset.hpp"
#include "ISimSession.hpp"
#include "NodeGraph.hpp"

#include <array>
#include <string>
#include <vector>

namespace scinodes {

// ---------------------------------------------------------------------------
// ISceneAssetResolver — capa anti-corrupción del sub-grafo de escena.
//
// El walker de la escena (collectScene) habla únicamente este idioma:
// "dame el DeviceAsset cuyo nombre del catálogo coincide con `name`".
// No sabe — ni le importa — cómo se carga el .gltf, dónde vive el cache,
// ni qué loader concreto lo produjo.  Eso vive del otro lado del seam.
//
// Análogo al IComputeBackend que separa la UI del solver Scilab
// concreto.  En producción, AssetService implementa esta interfaz para
// resolver entradas del catálogo `NodeGraph::importedObjects()`.  En
// tests, una stub in-memory cumple el contrato sin tocar el disco.
//
// Ver `doc/designs/3d_scene_graph_design.md` §7.
// ---------------------------------------------------------------------------
class ISceneAssetResolver {
public:
    virtual ~ISceneAssetResolver() = default;

    // Devuelve el DeviceAsset asociado al nombre del catálogo, o nullptr
    // si no se ha cargado (catálogo vacío, .gltf inválido, etc.).  El
    // puntero debe permanecer válido hasta el siguiente reload de la
    // entrada — el walker no lo retiene más allá del scope de
    // `collectScene()`.
    virtual const DeviceAsset* resolveByName(const std::string& objectName) const = 0;
};

// ---------------------------------------------------------------------------
// SceneRenderable — un Object3D ubicado en la escena con su transform
// acumulado desde el SceneOutput hacia atrás.  El renderer consume una
// lista plana de éstos por frame.
// ---------------------------------------------------------------------------
struct SceneRenderable {
    const DeviceAsset*    asset = nullptr;   // resolved via ISceneAssetResolver
    std::string           partName;          // "" = asset completo; si no, una part del .gltf
    std::array<float, 3>  rotation    = {{ 0.f, 0.f, 0.f }};  // rad, XYZ orden
    std::array<float, 3>  translation = {{ 0.f, 0.f, 0.f }};  // m
    std::array<float, 3>  scale       = {{ 1.f, 1.f, 1.f }};
    // Centro de rotación (m).  La rotación se aplica alrededor de este
    // punto; (0,0,0) = alrededor del origen (comportamiento histórico).
    std::array<float, 3>  pivot       = {{ 0.f, 0.f, 0.f }};

    // ID del nodo Object3D del que sale este renderable — útil para
    // selección desde el viewport (hover/click resuelve al nodo en el
    // grafo) y para tooltips de debugging.  0 si no se conoce.
    int                   sourceObject3DId = 0;

    // ID del SceneOutput que lo agrupa.  Permite a un renderer con
    // múltiples viewports separar por sink.  0 si es desconocido.
    int                   sinkId = 0;

    // Nombre del catálogo (objectRef) — útil para mensajes de error y
    // tooltips ("placeholder: 'Motor DC' no está en el catálogo").
    // Vacío si el Object3D no tiene `objectRef` configurado.
    std::string           objectRef;
};

// ---------------------------------------------------------------------------
// collectScene — recorre el grafo y produce la lista plana de renderables
// que el panel View3D debe pintar.
//
// `bridge` opcional (puede ser null en tests headless o cuando no hay
// sesión viva): si no es null, las transforms de los nodos
// TransformObject se leen del bridge en lugar de quedar en identidad.
// Convención de mapeo escalar→vec3 (paso 5c, simplificada):
//   • port 1 "rotation"    → rot[2]     (rotación alrededor del eje Z)
//   • port 2 "translation" → trans[2]   (desplazamiento en Z)
//   • port 3 "scale"       → uniforme aplicado a los tres ejes
// El walker busca un Sink corriente abajo del MISMO source del puerto
// signal — es ahí donde el bridge guarda los valores instantáneos (los
// Transformers no tienen buffer propio).  Si no encuentra Sink que
// "tape" la señal, el componente queda en su default (identidad).
//
// Algoritmo:
//   1. Identifica cada SceneOutput del grafo.
//   2. Para cada arista que entra a un SceneOutput, traza el camino
//      hacia atrás:
//        - si el origen es Object3D, emite un SceneRenderable con la
//          transform acumulada hasta ahí (identidad si no hubo
//          TransformObject en el camino).
//        - si el origen es TransformObject, acumula su transform
//          (rotación / translation / scale leídas de los puertos
//          Signal, por ahora con valores default — el wire al bridge
//          es trabajo de paso 5b) y baja por su input Geometry
//          (port 0).
//   3. Cualquier otro nodo en el camino se ignora (R6 garantiza que
//      sólo TransformObject puede aparecer en cadena Geometry).
//
// Pure function — no muta el grafo.  No carga assets; sólo consulta el
// resolver para cada Object3D encontrado.
//
// Ciclos: el walker es resistente a ciclos vía un set de nodos visitados
// por cadena.  En grafos válidos (R3 prohíbe self-loops) no debería
// ocurrir, pero el guard previene loops infinitos si la validación se
// salta.
// ---------------------------------------------------------------------------
std::vector<SceneRenderable>
collectScene(const NodeGraph& graph,
             const ISceneAssetResolver& resolver,
             const ISimSession* bridge = nullptr);

}  // namespace scinodes
