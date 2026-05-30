# Nombres y comentarios

Cada nodo del grafo tiene metadata semántica editable: un
**nombre custom** y un **comentario libre**.  Ambos viven en
el modelo (persisten al `.scn`).

## Nombre custom

Por defecto un nodo se llama con su label traducido (ej.
"Ganancia", "Integrador").  Si tenés varios del mismo tipo, las
etiquetas se vuelven ambiguas — "Ganancia #3" no es muy
informativo.

Solución: presioná <kbd>F2</kbd> con el nodo seleccionado y
escribí un nombre custom (`Realimentación_velocidad`,
`Compensador_jerk`, etc.).

El nombre custom:

- Persiste en `NodeInstance.stringParams["Name"]` → `.scn`.
- Reemplaza el label traducido al renderear el nodo.
- Es lo que muestra el find popup (<kbd>Shift</kbd>+<kbd>B</kbd>).
- Es lo que aparece cuando otro nodo lo elige como target de
  Alias (drag desde input → popup → buscar nodo).
- Sobrevive al copy-paste, encapsulación, undo/redo.

## Comentario

El comentario es texto libre opcional, también editable con
<kbd>F2</kbd> (en el mismo diálogo que el nombre).  Se renderea
como un punto pequeño en la esquina del nodo; el texto aparece
en un tooltip cuando hacés hover sobre el nodo.

Usalo para anotaciones que no caben en el nombre:

- "Kp limitado a 5 — saturación del PWM real".
- "Convertir a deg después de cambiar el setpoint a grados".
- "TODO: validar con datos del setup del lab del lunes".

## Por qué importan

Los grafos crecen.  Un sistema E1-DC tiene 25 nodos; un sistema
multifísico con térmica + magnético + control llega a 80.  Sin
nombres semánticos:

- Cada vez que volvés al grafo después de un tiempo, hay que
  releer las aristas para entender qué hace cada bloque.
- El find popup queda inservible (buscás "kp" pero hay siete
  ganancias llamadas "Ganancia").
- Los Aliases creados desde inputs apuntan a "Gain #17" en vez
  de a "Compensador_proporcional" — el nombre del Alias es
  igualmente opaco.

Los nombres custom son el equivalente del comment block que
los programadores usan en código — documentan la **intención**,
no el tipo.

## Diferencia vs Xcos

En Xcos los bloques no soportan rename.  Para "etiquetar"
hay que insertar un bloque `TEXT_f` (anotación) al lado, que
es un elemento visual flotante sin vínculo lógico con el
bloque que pretende etiquetar.  Ver
[`doc/test_manual/xcos_comparison/observations/08_labels_metadata/`](https://github.com/nelson1421/SciNodes/tree/main/doc/test_manual/xcos_comparison/observations/08_labels_metadata)
para el contraste documentado.
