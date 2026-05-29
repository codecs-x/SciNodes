# Visor 3-D

El visor 3-D es un panel auxiliar dedicado a inspeccionar
modelos geométricos. En esta versión es completamente
independiente del solucionador: el modelo que cargues queda en
pantalla, pero su pose no responde a la simulación. Sirve para
mirar la geometría con la que vas a trabajar antes de cablear el
grafo, o para tener una referencia visual mientras experimentas.

## Cargar un modelo

Dentro del panel, al lado de la cámara, hay un botón
**Browse**. Al pulsarlo se abre el selector de archivos del
sistema y puedes elegir un `.obj` o un `.stl`. El editor parsea el
archivo y centra el modelo automáticamente en la cámara
orbital.

Mientras no haya un modelo cargado, el panel muestra el mensaje
*"Click Browse to load a .obj or .stl model"*. No hay arrastrar y
soltar ni una entrada en el menú File; el botón es el único punto
de entrada del visor.

## Mover la cámara

La cámara es orbital alrededor del origen del modelo. Las
interacciones del ratón son las usuales en este tipo de visor:

- **Click izquierdo + arrastre** orbita la cámara.
- **Rueda** acerca o aleja el modelo.
- **Shift + arrastre** desplaza el punto de mira.

## Qué verás (y qué no)

El render es de líneas (modo *wireframe*). No hay relleno sólido,
no hay materiales, no hay luces. Esa decisión es deliberada para
esta versión: el visor sirve para verificar la geometría sin
arrastrar la complejidad de un pipeline de iluminación completo.

Tampoco hay vínculo con la simulación. Si quieres que un modelo
3-D rote con la salida de un nodo del grafo, esa capacidad llega en
versiones posteriores cuando el visor se acopla al solucionador.
