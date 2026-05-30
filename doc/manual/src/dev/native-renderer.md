# Renderer nativo del canvas

En v0.0.8 el editor deja de depender de la librería externa
`imnodes` y dibuja su propio canvas. Ya no hay un nodo
"externo" que SciNodes adapta; los nodos, los pines, los
cables, las sombras, las cajas de selección — todo se dibuja
con `ImDrawList` desde código propio.

## La abstracción: `INodeRenderer`

`src/ui/INodeRenderer.hpp` define la interfaz Strategy que el
`NodeCanvas` consume:

```cpp
class INodeRenderer {
public:
    virtual void begin(...) = 0;
    virtual void drawNode(const NodeInstance& n, const NodeBox& box, ...) = 0;
    virtual void drawLink(const Edge& e, const LinkStyle& style) = 0;
    virtual bool isHovered(int nodeId, int pinIdx) const = 0;
    virtual void end() = 0;
    // ...
};
```

`NodeCanvas` itera el modelo, traduce a estas llamadas y deja
que el renderer decida la presentación. Hubo una
`ImnodesAdapter` durante la transición; a partir de v0.0.8
sólo queda la implementación nativa y `imnodes_lib` sale del
proyecto.

## La implementación: `NativeNodeRenderer`

Construida en cuatro fases incrementales:

- **Fase 1 — skeleton + zoom/pan.** Las coordenadas del canvas
  son "model units", no píxeles. Una transformación
  `worldToScreen` aplica el factor de zoom y la traslación del
  pan; todos los cálculos de hit-test viven en model units.
- **Fase 2 — interactividad estilo Blender.** Click+arrastre
  para mover; `Ctrl+wheel` zoom centrado en el cursor; rubber
  band con click izquierdo en vacío; `F` encuadra la
  selección; `Ctrl+L` recentra el grafo.
- **Fase 3 — sombras + selección.** Sombras suaves debajo del
  nodo, color del cable por la categoría de la fuente, halo de
  iluminación en las conexiones del nodo seleccionado.
- **Fase 4 — default.** El renderer nativo pasa a ser el
  único; `imnodes_lib` se retira de `CMakeLists.txt`.

## Gramática de layout responsive

El layout del cuerpo del nodo (filas de pines + widgets +
sufijos de unidad) se expresa en model units, no en píxeles.
`computeNodeDimensions(NodeInstance, NodeDef)` recorre los
puertos y los widgets y devuelve el bounding box en model
units; el renderer lo dibuja a la escala del zoom actual sin
pixelado intermedio.

## Compatibilidad con el resto del editor

`NodeCanvas` sigue siendo el dueño del modelo del grafo y de
las shortcuts; el renderer sólo recibe consultas y emite
primitivas. Esto deja la puerta abierta a un renderer
alternativo (p. ej. uno que dibuje al SVG para exportar el
canvas) sin tocar la lógica del editor.
