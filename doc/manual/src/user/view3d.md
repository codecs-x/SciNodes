# Visor 3-D

El panel **3D View** muestra una malla 3-D que se anima con
la salida del solver en tiempo real.

## Modos

1. **Modelo procedural del motor** — la malla por defecto:
   estator hexagonal, rotor cilíndrico, eje, bobinas.
2. **Asset cargado desde disco** — `.obj` o `.stl` que el
   usuario abre con `Browse`.

## Cámara orbital

| Gesto                            | Acción                          |
|----------------------------------|---------------------------------|
| Arrastrar con botón izquierdo    | Orbitar (azimuth + elevación)   |
| Arrastrar con botón medio        | *Pan* del pivote                |
| Rueda del ratón                  | Zoom (acerca / aleja)           |
| Doble-click sobre vacío          | Reset de cámara                 |

## Animar con `View3DSink`

El nodo `View3DSink` es un sumidero con una entrada escalar.
Cuando recibe muestras del solver las publica al
`View3DPanel`, que las usa como ángulo de rotación del eje
del motor procedural.

Ejemplo: el lazo PID + motor DC del manual de cableado.
Conectá un `View3DSink` a la velocidad del motor; el eje del
modelo gira a esa velocidad mientras la simulación corre.

## Render *offscreen* con Vulkan

El panel dibuja con un segundo pipeline de Vulkan que
renderiza *offscreen* a una textura y la presenta a ImGui
con `ImGui::Image`.  El rasterizado no pasa por
`ImDrawList` en CPU, así que mallas con miles de triángulos
se mueven sin pérdida de fluidez.
