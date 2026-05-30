# Exportar datos

Mirar la trayectoria en un osciloscopio es útil para diseñar; en
algún momento querrás llevar las muestras a un análisis externo
—un *script* propio en Python, un reporte en Octave, una hoja
de cálculo—. SciNodes da dos caminos a partir de esta versión:
**CSV por sumidero** y **`.sod` global**.

## CSV por sumidero (DataLogger)

Cada `DataLogger` del grafo gana un botón **Export CSV** en el
panel de plots. Al pulsarlo se abre el selector de archivos
nativo, escribes un nombre, y SciNodes escribe el *ring buffer*
del *logger* a un CSV en orden cronológico.

El formato es minimalista:

```
# Sink: DataLogger #3
time,value
0.000,0.000
0.010,0.000999983
0.020,0.001999866
...
```

- El comentario `# Sink: …` lleva la etiqueta del nodo si tiene
  un nombre asignado.
- La primera fila de datos es la cabecera `time,value`.
- Las muestras se escriben **oldest first**: la fila inicial es
  la más antigua del *ring buffer*, la final es la más reciente.
- `time` es el tiempo simulado (segundos), no el tiempo de
  pared.
- `value` es la muestra del único canal del `DataLogger`.

Pensado para abrir directo en cualquier herramienta. `pandas`:

```python
import pandas as pd
df = pd.read_csv("logger3.csv", comment="#")
df.plot(x="time", y="value")
```

## `.sod` global (toda la simulación)

`File → Export Simulation Data (SOD)…` escribe **toda la
corrida** en formato `.sod`, el formato HDF5 nativo de Scilab.
Incluye el vector de tiempo y todos los buffers de todos los
sumideros del grafo, no sólo un `DataLogger`.

Esto sirve cuando quieres analizar la corrida desde Scilab
mismo:

```scilab
sod = loadmat("corrida.sod");
plot(sod.t, sod.osciloscopio_1, "b", ...
     sod.t, sod.phaseportrait_2_ch0, "r");
```

Las claves del archivo se nombran a partir del tipo de nodo y
su id (más el índice de canal cuando el sumidero es
multi-canal, como `PhasePortrait` con `_ch0` y `_ch1`).

Algunos puntos importantes:

- **`.sod` sólo se puede exportar cuando hay datos.** Si no has
  pulsado Run aún, el archivo sale vacío. Lo razonable es correr
  unos segundos, pausar, y exportar.
- La exportación, en el modo con hilo dedicado del solver, se
  encola para evitar carreras: el hilo del solver termina su
  paso actual y entrega el archivo antes de seguir.
- En modo síncrono (sin hilo del solver) la exportación es
  inmediata.

## Cuándo usar cada uno

- **CSV** si quieres una señal específica de un `DataLogger`
  para procesarla en otra herramienta —es el formato más
  universal—.
- **`.sod`** si vas a analizar la corrida completa desde Scilab
  o si necesitas todos los sumideros en un solo archivo
  binario.
