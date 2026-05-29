# Exportar e importar

SciNodes soporta tres formatos:

| Formato      | Contiene                                                    | Cuándo usarlo                                           |
|--------------|-------------------------------------------------------------|---------------------------------------------------------|
| `.scn` JSON  | Grafo completo (nodos, cables, parámetros, unidades, sub-grafos). | Guardar/compartir un modelo.                            |
| `.csv`       | Series temporales muestreadas de los sinks.                 | Análisis post-hoc en otro programa.                     |
| `.png` / `.svg` | Plot exportado de un nodo de visualización.              | Figuras para tesis/papers.                              |
| `.gltf`      | Asset 3D para el catálogo del proyecto.                     | Importar geometría desde Blender/CAD.                   |

## Guardar el grafo

`Archivo → Guardar` (o <kbd>Ctrl</kbd>+<kbd>S</kbd>).

El `.scn` es JSON legible: podés diff-earlo, editarlo a mano si
hace falta, y meterlo en git.

### Qué se guarda

- Estructura del grafo: nodos + sus tipos + sus IDs.
- Cables: pares (from-attr, to-attr).
- Parámetros: nombre + valor + unidad declarada.
- Posiciones del top-level (las de dentro de sub-grafos quedan
  sólo en memoria — limitación conocida).
- Setup de simulación: tiempo final, sample step, solver.
- Referencias a custom nodes (no su definición — esa vive en
  el JSON externo).
- Referencias a assets glTF del catálogo.

### Qué no se guarda

- Buffers de plot (se generan al correr).
- Estado del solver (cada Run arranca de t=0 según las IC).
- Posiciones dentro de sub-grafos anidados (se recalculan al
  entrar).
- Selección de UI, zoom y pan del canvas.

## Cargar un grafo

`Archivo → Abrir` (o <kbd>Ctrl</kbd>+<kbd>O</kbd>).

Al cargar, SciNodes corre la **gramática completa** sobre el
grafo:

- R1: nodos válidos (que existan en el registry).
- R2: aristas válidas (tipos compatibles).
- R3: sub-grafos cerrados.
- R7: unidades coherentes.

Si algo no valida, el grafo se carga **igual** pero las
aristas problemáticas quedan rojas y el botón Run deshabilitado
hasta que corrijas.

## Exportar CSV

`Archivo → Exportar → CSV todos los sinks`.

Produce un único archivo con columnas:

```
t, <sink_1>, <sink_2>, ..., <sink_N>
```

donde `<sink_i>` es el nombre del nodo (o su id si no tiene
nombre).  Las unidades aparecen en la línea de comentario
inicial:

```
# t [s], oscilloscope_3 [rad], oscilloscope_7 [rad/s]
0.000, 0.000, 0.000
0.001, 0.001, 0.998
...
```

El muestreo coincide con el `Sample step` configurado en el
setup de simulación.

## Importar asset glTF

`Archivo → Importar modelo 3D` → seleccionar `.gltf` o `.glb`.

El asset:

- Se copia al catálogo del proyecto (`<grafo>.assets/`).
- Queda disponible para todos los nodos `Object3D`.
- Se referencia por nombre, no por ruta — si movés el `.scn`,
  el asset se mueve junto si están en la misma carpeta.

## Versión del archivo

El `.scn` tiene un campo `version` en el JSON.  SciNodes
sostiene migración hacia adelante: cuando subimos versión
mayor del schema, abrir un `.scn` viejo lo migra
silenciosamente al schema nuevo (y la próxima vez que guardes,
queda en la nueva versión).
