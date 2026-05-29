# Plots: multi-canal + FFT + plano de fase

Los sumideros se dibujan abajo a la derecha en el panel
**Plots**.  Cada sumidero conectado obtiene su propio sub-
plot; el alto se reparte automáticamente.

## `Oscilloscope` y `DataLogger`

Aceptan una o más entradas y superponen las trayectorias.
La leyenda dice qué color corresponde a qué canal.

## `FFTAnalyzer`

Una entrada escalar.  El parámetro **Bin Count** controla el
tamaño de la ventana FFT (se redondea hacia abajo a la
potencia de 2 más cercana).  Se calcula con un algoritmo
*radix-2* iterativo de Cooley–Tukey en C++ puro y se grafica
la magnitud del espectro.  El *bin* DC se omite del display
para que el rango Y sea útil.

## `PhasePortrait`

Dos entradas: la primera define el eje X, la segunda el eje
Y.  Se traza la trayectoria paramétrica
\\((x(t), y(t))\\) como una polilínea con un *dot* sobre el
sample reciente.  Útil para inspeccionar la dinámica de un
sistema de segundo orden por la geometría de su trayectoria.

## Panel flotante de parámetros

**Doble-click sobre cualquier nodo** abre un panel flotante
con todos sus parámetros visibles en `DragFloat`.  Útil para
sintonizar sin tener que zoom-in al nodo.

## NaN en la salida

Si el solver emite NaN, el nodo culpable se ilumina en rojo
en el canvas.  Normalmente indica una ganancia desmedida o
un denominador que cayó a cero.
