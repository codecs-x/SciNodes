# NativeNodeRenderer (canvas WYSIWYG)

El canvas de nodos se renderiza con un `INodeRenderer`
implementado por SciNodes — sin dependencia de `imnodes` ni
otras librerías de canvas.

## Por qué propio

`imnodes` (la librería original) tenía dos limitaciones que
rompieron la dirección del proyecto:

- Zoom no WYSIWYG: los puertos no escalaban con el cuerpo del
  nodo, quedaban del mismo tamaño en cualquier zoom.
- No exponía las posiciones reales al modelo — el grafo sólo
  sabía lo que `imnodes` le decía, y mover un nodo en código no
  podía leerse desde el render.

`NativeNodeRenderer` se construyó sobre `ImDrawList` directamente
para resolver ambos.

## Interfaz

```cpp
class INodeRenderer {
public:
    virtual void init() = 0;
    virtual void shutdown() = 0;

    virtual void beginCanvas(const std::string& contextKey) = 0;
    virtual void endCanvas() = 0;
    virtual void frameToBox(CanvasPos minM, CanvasPos maxM,
                            float vW, float vH, float maxZoom) = 0;
    virtual void centerOn(CanvasPos modelPoint) = 0;

    virtual void beginNode(int nodeId, CanvasDims dims, bool hasComment) = 0;
    virtual void endNode() = 0;
    virtual void beginNodeTitleBar() = 0;
    virtual void endNodeTitleBar() = 0;
    virtual void beginInputAttribute(int attrId, PortShape shape) = 0;
    virtual void endInputAttribute() = 0;
    virtual void beginOutputAttribute(int attrId, PortShape shape, int labels) = 0;
    virtual void endOutputAttribute() = 0;

    virtual void drawLink(int linkId, int fromAttrId, int toAttrId) = 0;

    // poll APIs para detectar interacción
    virtual bool pollLinkCreated(LinkCreatedEvent& out) = 0;
    virtual bool pollLinkDetached(int& outLinkId) = 0;
    virtual bool isNodeHovered(int& outNodeId) = 0;
    // ...
};
```

Reglas duras del contrato:

1. Ningún tipo ImGui cruza la frontera (`CanvasPos`,
   `CanvasDims` son del proyecto, no `ImVec2`).
2. Una `CanvasState` por path-key (`/`, `/5/`, …) preserva
   zoom/pan/selección al navegar SubGraphs.
3. `setNodePosition` con `CoordSpace::Screen` aplica la
   inversa del pan+zoom para guardar en editor space.  La
   fuente de verdad siempre es editor space.

## Transformación model ↔ screen

```
screen = canvasOrigin + zoom · model + pan
```

Y la inversa:

```
model = (screen − canvasOrigin − pan) / zoom
```

Esto se computa en `modelToScreen` y `screenToModel`.  Toda
medida que el código pase al renderer es model space; el
renderer se ocupa del WYSIWYG.

## Estado por canvas

Cada path del grafo (top-level, `/5/`, `/5/2/`, …) mantiene su
propio:

- `zoom` (rango 0.1× a 4.0×).
- `pan` (en píxeles, screen space).
- `positions` (mapa nodeId → posición editor space).
- `selectedNodes` / `selectedLinks` (sets).

Al entrar a un SubGraph, `pushCanvas(key)` guarda el estado
actual y carga el del path nuevo.  Al salir, `popCanvas()`
restaura.  Resultado: el usuario vuelve a su zoom + pan
exactos al navegar entre niveles.

## Cómo se dibuja un nodo

```
beginNode(id, dims)
  beginNodeTitleBar()
    [ImGui::Text(...)]
  endNodeTitleBar()

  beginInputAttribute(in1, CircleFilled)
    [ImGui::Text("u1")]
  endInputAttribute()

  beginOutputAttribute(out1, CircleFilled)
    [ImGui::Text("y")]
  endOutputAttribute()
endNode()
```

`beginNode` posiciona el cursor en model→screen y reserva
espacio.  Cada `beginXxxAttribute` registra dónde queda el pin
(círculo) para el siguiente frame.

Los pines se dibujan **después** del borde del nodo (en
`endNode`) para que el outline no corte los círculos —
convención de Blender.

## Drag, pan, selección

`handleInteraction()` corre en cada frame y gestiona:

- `Drag::Node`: arrastre de selección de nodos.
- `Drag::Pin`: arrastre desde un pin para crear cable.
- `Drag::Pan`: middle-click drag para mover canvas.
- `Drag::RectSelect`: click vacío + drag para box-select.

El detach (arrancar drag desde un input ya conectado) reusa el
mismo flujo `Drag::Pin` con el cable original como "candidato a
borrar" si el drop no aterriza en otro puerto.

## Por qué INodeRenderer es interfaz

Permite tests headless: `FakeNodeRenderer` no dibuja pero
registra todo lo que se le pasó.  La UI completa de
NodeCanvas puede testearse sin Vulkan ni ventana.

## Split del archivo (etapa 6K.F)

`NativeNodeRenderer.cpp` original tenía 1053 líneas.  Etapa
6K.F lo dividió en:

- `NativeNodeRenderer.cpp` (302) — métodos públicos del
  contrato + init/shutdown.
- `NativeNodeRendererNode.cpp` — beginNode/endNode + título +
  atributos.
- `NativeNodeRendererLink.cpp` — drawLink + Bézier.
- `NativeNodeRendererInteraction.cpp` — handleInteraction +
  polls.
- `NativeNodeRendererInternal.hpp` — constantes + drawPinShape
  inline.
