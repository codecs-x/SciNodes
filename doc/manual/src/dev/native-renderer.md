# Native renderer

`src/ui/canvas/` aloja un renderer del canvas escrito desde
cero, alternativo a `imnodes`.

## Capas

| Archivo                         | Rol                                  |
|---------------------------------|--------------------------------------|
| `Canvas.{cpp,hpp}`              | Abstracción del canvas + zoom + pan. |
| `NativeNodeRenderer.{cpp,hpp}`  | Renderer concreto que usa la abstracción. |
| `NativeNodeRendererNode.cpp`    | Dibuja un nodo individual.           |
| `NativeNodeRendererLink.cpp`    | Dibuja un cable (Bézier con cap).    |
| `NativeNodeRendererInteraction.cpp` | Selección rectangular, drag, etc. |

## Por qué un renderer propio

`imnodes` tiene asumido un mapeo px/canvas 1:1.  Con zoom
no se ve bien.  El renderer propio toma todas las medidas
en *model units* y deja a la capa de presentación
multiplicar por el zoom actual.  Resultado: zoom suave +
sombras consistentes + selección rectangular limpia.

## Cómo se activa

A esta versión el toggle es por variable de entorno
(`SCINODES_NATIVE_CANVAS=1`).  El renderer `imnodes`
permanece como fallback para casos en los que el propio no
soporte todavía algún feature.
