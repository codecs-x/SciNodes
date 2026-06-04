# Renderer nativo del canvas

En v0.0.8 el editor deja de depender de la librería externa
`imnodes` y dibuja su propio canvas. Ya no hay un nodo
"externo" que SciNodes adapta; los nodos, los pines, los
cables, las sombras, las cajas de selección — todo se dibuja
con `ImDrawList` desde código propio.

## La abstracción: `INodeRenderer`

`src/ui/canvas/Canvas.hpp` define la interfaz Strategy que el
`NodeCanvas` consume (forward-declarada en `NodeCanvas.hpp`, donde
se guarda como `INodeRenderer* m_renderer`). Es una API de **modo
inmediato** —el mismo idioma que hablaba la librería externa que
reemplaza—: el `NodeCanvas` recorre el modelo y va emitiendo
llamadas `beginNode`/`endNode`, `beginInputAttribute`,
`beginParamAttribute`, `drawLink`, etc., y el renderer dibuja sobre
la marcha. No es una API retenida que reciba el nodo entero.

La interfaz está partida en tres roles que se agregan por herencia
virtual:

```cpp
// ciclo de vida + dibujo + transformación de coordenadas
class INodeRendererCore {
    virtual void beginCanvas(const std::string& contextKey) = 0;
    virtual void beginNode(/* ... */)                       = 0;
    virtual void endNode()                                  = 0;
    virtual void beginInputAttribute(int attrId, PortShape shape) = 0;
    virtual void beginParamAttribute(int attrId, PortShape shape) = 0;
    virtual void drawLink(int linkId, int fromAttrId, int toAttrId) = 0;
    virtual void centerOn(CanvasPos modelPoint) = 0;
    // ...
};
class INodeRendererSelection { /* selectNode, getSelectedNodes, … */ };
class INodeRendererQuery     { /* hover / hit-test / link-created event */ };

class INodeRenderer : public virtual INodeRendererCore,
                      public virtual INodeRendererSelection,
                      public virtual INodeRendererQuery {};
```

Los *attribute IDs* (`attrId`) son el mismo esquema entero del
modelo (ver [NodeGraph](nodegraph.md)): nodo + puerto/param en un
solo `int`. Hubo una `ImnodesAdapter` durante la transición; a
partir de v0.0.8 sólo queda la implementación nativa y la librería
externa de nodos sale del proyecto.

## La implementación: `NativeNodeRenderer`

`NativeNodeRenderer : public INodeRenderer` (en
`src/ui/canvas/NativeNodeRenderer.hpp`). Por tamaño, su
implementación está repartida en varios *translation units* que
comparten `NativeNodeRendererInternal.hpp`:
`NativeNodeRenderer.cpp` (ciclo de vida + canvas), `…Node.cpp`
(dibujo del cuerpo del nodo), `…Link.cpp` (cables) y
`…Interaction.cpp` (pan/zoom, selección, hit-test).

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
`computeNodeDimensions(const NodeInstance&, const NodeGraph*)`
(en `Canvas.hpp`) recorre los puertos y los widgets y devuelve un
`CanvasDims` (ancho × alto) en model units; el renderer lo dibuja a
la escala del zoom actual sin pixelado intermedio. Las constantes
de layout viven junto a esa función en `Canvas.hpp` como **única
fuente de verdad** del tamaño de los nodos, de modo que la medida
que calcula el modelo y el espacio que reserva el renderer no se
desincronizan. El parámetro `NodeGraph*` es opcional: sólo se usa
para resolver el ancho del texto de un nodo `Alias` (`"→ <target>"`).

## Compatibilidad con el resto del editor

`NodeCanvas` sigue siendo el dueño del modelo del grafo y de
las shortcuts; el renderer sólo recibe consultas y emite
primitivas. Esto deja la puerta abierta a un renderer
alternativo (p. ej. uno que dibuje al SVG para exportar el
canvas) sin tocar la lógica del editor.
