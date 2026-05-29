# Pipeline 3D — SceneCollector y View3DPanel

El panel View3D renderiza una escena 3D a partir de un
sub-grafo del modelo principal.  El walker que extrae la
escena es `SceneCollector`.

## Sub-grafo de escena

Por convención, la escena vive como un sub-grafo del grafo
principal cuyos sinks son nodos `SceneOutput`.  El walker:

1. Encuentra todos los `SceneOutput`.
2. Para cada uno, hace DFS reverso hasta llegar a las hojas
   (`Object3D` típicamente).
3. Acumula transformaciones a medida que pasa por
   `TransformObject`.
4. Produce una lista de `SceneNode { meshRef, modelMatrix }`.

## SceneCollector

```cpp
class SceneCollector {
public:
    Scene collect(const NodeGraph& g, const IComputeBackend& b);
};
```

`collect` corre headless — no necesita Vulkan ni ventana.  Es
testeable.

Para cada `TransformObject` encontrado:

- Sus puertos signal (rotation, translation, scale) leen valor
  vía `readLiveSampleAt` del backend.  Esto es lo que hace
  que la escena se actualice en cada frame.
- La matriz se compone como `T · R · S`.

## Cómo leer un valor en vivo

`readLiveSampleAt(graph, attrId, t)` recorre el grafo desde
el puerto hacia atrás hasta encontrar:

- Una fuente con valor constante → ese valor.
- Un puerto **bufferizado** del bridge → el último STATE
  recibido del solver para ese canal.

La distinción se introdujo en etapa 6J.8: antes el walker
buscaba downstream un Sink y tomaba su última muestra; eso
fallaba cuando había un convertidor `RadToDeg` entre el
Integrator y el Sink (el walker llegaba al Sink pero el dato
estaba en grados, el TransformObject esperaba radianes).

Solución: cada escalar producido por el grafo se bufferea en
el bridge (`bufferedChannels` ⊇ `sinkChannels`).  El walker
lee directamente desde el punto del grafo más cercano al
tap.

## View3DPanel

`View3DPanel` toma una `Scene` y la renderiza vía Vulkan.
Componentes:

- `Renderer3D`: maneja device, swapchain, descriptor sets.
- `MeshCache`: carga `.gltf`/`.glb` una vez por sesión.
- `Camera`: pan/orbit/zoom con estado propio.

El panel pregunta al SceneCollector la escena cada frame.
Esto es 60 Hz; el SceneCollector es lo bastante barato para
no aparecer en profilers (típicamente <0.5 ms).

## Split del archivo (etapa 6K.E)

`View3DPanel.cpp` original tenía 920 líneas.  Etapa 6K.E lo
dividió en:

- `View3DPanel.cpp` — coordinación + IMGUI del panel.
- `View3DPanelCamera.cpp` — input + estado de cámara.
- `View3DPanelRender.cpp` — render passes Vulkan.
- `View3DPanelAssetMenu.cpp` — UI del catálogo de assets.

## ISceneAssetResolver

El View3DPanel no carga `.gltf` directamente — pregunta al
`IAssetService` por mesh con un asset ID.  Esto deja al
AssetService elegir la fuente:

- Disco: lee el `.gltf` del catálogo del proyecto.
- Memoria: si el asset fue importado vía drag-drop, vive en
  RAM hasta el primer save.
- Procedural: el caso del motor genérico (sin asset
  asignado) genera mesh procedural en memoria.

## Limitaciones actuales

Ver "Limitaciones" en [Visualización 3D](../user/3d.md) — son
las mismas, listadas desde la perspectiva del usuario.
