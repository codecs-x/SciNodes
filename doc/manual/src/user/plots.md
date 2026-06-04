# Plots y sumideros

Los sumideros son los nodos que observan la simulación. Para que
uno entregue muestras tiene que estar **conectado** a la salida
de una fuente o un transformador (el cable lo valida la gramática
con la regla R0). Mientras la simulación esté corriendo, el
`ScilabBridge` empuja cada paso a un *ring buffer* interno de
512 muestras **por canal** del sumidero, y el panel de plots los
renderiza.

## `Oscilloscope`

Plot de la señal contra el tiempo. Útil para ver la respuesta del
sistema a una entrada conocida o el seguimiento de un *setpoint*
en lazo cerrado. Tiene un único parámetro, `Time Window` (por
defecto 5 s), que controla cuánto pasado se muestra en pantalla.

## `FFT Analyzer`

Dibuja el espectro de magnitud de la señal de entrada. El cálculo
lo hace una FFT *radix-2 Cooley-Tukey* propia (`Fft.cpp`,
`magnitudeSpectrum`) sobre la ventana más reciente del *ring
buffer*, ajustando el tamaño de ventana al mayor potencia de dos
que no exceda el parámetro `Bin Count` (por defecto 256). El
panel renderiza la salida con `renderSpectrum`. No hay calibración
de frecuencia explícita en esta versión —los *bins* se muestran
contra su índice, no contra su frecuencia en Hz—; eso entra en
versiones posteriores.

## `Phase Portrait`

Plot 2-D que dibuja la trayectoria del sistema en el espacio de
estados. A diferencia del resto de sumideros, **pide dos
entradas**: `in 1` se interpreta como `x(t)`, `in 2` como
`dx/dt(t)`. El bridge mantiene un buffer por canal y el panel
los combina al renderizar la trayectoria.

## `Data Logger`

Acumula muestras en memoria. El cuerpo del nodo en el canvas
muestra el número de puntos registrados y el parámetro `Sample
Rate` (por defecto 1000 Hz). Su panel en el visor de plots gana
en esta versión un botón **Export CSV** que abre el selector de
archivos nativo y escribe el *ring buffer* completo del logger
a un CSV en orden cronológico (más detalles en
[Exportar datos](exporting-data.md)).

## `Terminal Display`

El más simple de los sumideros: no usa el panel de plots.
Imprime el último valor recibido directamente dentro de su propio
cuerpo en el canvas, como un cuadrado de texto. Útil para
verificar a ojo el valor instantáneo de una señal mientras
ajustas otro parámetro, sin tener que abrir un osciloscopio
entero. No tiene parámetros.

## `3D View Sink`

Sumidero que tampoco usa el panel de plots: su muestra alimenta
el ángulo del eje del motor procedural en el [visor
3-D](view3d.md). Como cualquier otro sumidero, el bridge le
reserva un *ring buffer*; lo que cambia es quién lo lee
—el `View3DPanel` en lugar del `PlotPanel`—.

## `Distribution Sink`

A partir de v0.0.6 el panel de plots renderiza también un
histograma vivo. El `Distribution Sink` recibe un canal y lo
agrupa en `Bin Count` cubetas (20 por defecto). El panel
dibuja las barras junto a media, desviación, mínimo y máximo
observados, recalculados sobre todo el *ring buffer*. La
combinación natural es con `Tolerance Perturbator` o cualquier
otra fuente estocástica: cada paso del solver es un trial, y
el histograma converge a la distribución del observable según
avanza la simulación. Los detalles del flujo están en
[Estructural y NVH](structural.md).

## Buffers, canales y ritmo

`ScilabBridge` mantiene *ring buffers* de 512 muestras por
canal por sumidero. El número de canales (`channelCount(nodeId)`)
depende del tipo: `1` para los sumideros de una sola señal, `2`
para `PhasePortrait`. Cada paso del solver llena los buffers con
una muestra por canal. El editor lee del buffer al redibujar el
panel de plots en cada *frame*; si la UI corre más rápido que el
solver no hay problema, y si el solver corre más rápido las
muestras se acumulan en el buffer. El ritmo nominal del solver
es un paso cada `1/60 s` (≈ 16,7 ms), con la UI también a 60 Hz.
