# Plots y sumideros

Los sumideros son los nodos que observan la simulación. Para que
uno entregue muestras tiene que estar **conectado** a la salida
de una fuente o un transformador (el cable lo valida la gramática
con la regla R0). Mientras la simulación esté corriendo, el
`ScilabBridge` empuja cada paso a un *ring buffer* interno de
512 muestras por sumidero, y el panel de plots los renderiza.

## `Oscilloscope`

Plot de la señal contra el tiempo. Útil para ver la respuesta del
sistema a una entrada conocida o el seguimiento de un *setpoint*
en lazo cerrado. Tiene un único parámetro, `Time Window` (por
defecto 5 s), que controla cuánto pasado se muestra en pantalla;
las muestras más viejas que la ventana se descartan visualmente
aunque sigan ocupando lugar en el buffer mientras quepan.

## `FFT Analyzer`

Sumidero clasificado como analizador de espectro. En esta versión
comparte el *renderer* del osciloscopio: muestra la señal cruda en
el dominio del tiempo. El parámetro `Bin Count` (por defecto 256)
está expuesto en el catálogo pensando en la transformada real, que
se implementa en versiones posteriores. Por ahora es un sumidero
de aviso —dice "aquí va a vivir el espectro"— sin computación
adicional.

## `Phase Portrait`

Plot 2-D que dibuja la trayectoria del sistema en el espacio de
estados. A diferencia del resto de sumideros, **pide dos
entradas**: `in 1` se interpreta como `x(t)`, `in 2` como
`dx/dt(t)`. Si conectas solamente una, R5 dejará la otra libre y
el plot quedará vacío hasta que cables la segunda.

## `Data Logger`

Acumula muestras en memoria. El cuerpo del nodo en el canvas
muestra el número de puntos registrados y el parámetro `Sample
Rate` (por defecto 1000 Hz). En esta versión la exportación a
archivo CSV aún no está conectada; el `Data Logger` queda como
buffer interno listo para que las versiones siguientes le agreguen
el botón de exportar.

## `Terminal Display`

El más simple de los sumideros: no usa el panel de plots. Imprime
el último valor recibido directamente dentro de su propio cuerpo en
el canvas, como un cuadrado de texto. Útil para verificar a ojo el
valor instantáneo de una señal mientras ajustas otro parámetro,
sin tener que abrir un osciloscopio entero. No tiene parámetros.

## Buffers y ritmo

Cada paso del solver llena los buffers de los sumideros con una
muestra. El editor lee del buffer al redibujar el panel de plots
en cada *frame*; si la UI corre más rápido que el solver no hay
problema (se reusa la última muestra), y si el solver corre más
rápido que la UI las muestras se acumulan en el buffer y el
osciloscopio dibuja la ventana completa. El ritmo nominal del
solver es 10 ms por *frame* de UI, con la UI nominalmente a 60 Hz.
