# Nodos personalizados

A partir de esta versión el catálogo deja de ser cerrado. Puedes
agregar transformadores nuevos sin recompilar el editor: escribís
un descriptor JSON y lo cargás desde la UI; SciNodes lo integra en
la paleta, lo valida con la gramática y lo emite en el código
Scilab como cualquier otro nodo. (Los ejemplos del repo viven en
`doc/custom_nodes/`.)

## El descriptor

Un descriptor es un único archivo `.json` que ocupa una entrada
en `doc/custom_nodes/`. El esquema es directo:

```json
{
  "type_id":      "Tripler",
  "label":        "Tripler",
  "description":  "Output = 3 * p_k * u1 — example custom transformer.",
  "category":     "transformer",
  "input_ports":  1,
  "output_ports": 1,
  "params": [
    { "name": "k", "default": 1.0, "units": "" }
  ],
  "expression":   "3 * p_k * u1"
}
```

Campos:

- **`type_id`** — clave única usada por el grafo y el codegen. No
  puede coincidir con un tipo del catálogo *built-in* ni con
  otro custom ya registrado.
- **`label`** — nombre humano que aparece en la paleta.
- **`description`** — explicación breve, se muestra como
  *tooltip*.
- **`category`** — `"source"`, `"transformer"`, o `"sink"`.
- **`input_ports`** / **`output_ports`** — número de puertos.
  La gramática los respeta exactamente igual que los nodos
  *built-in*.
- **`params`** — lista de parámetros con `name`, `default`,
  `units` (decorativo). Cada parámetro queda editable en el
  cuerpo del nodo y en el panel flotante.
- **`expression`** — la fórmula Scilab que el codegen sustituye
  cuando emite el script.

## La sintaxis de la expresión

Dentro de `expression` puedes usar dos clases de placeholders:

- **`u1`, `u2`, …** — la expresión Scilab que produce cada
  puerto de entrada, en orden de cable (1-based, alineado con
  el lenguaje natural de Scilab).
- **`p_<nombre>`** — el valor actual del parámetro de ese
  nombre. Por ejemplo, si declaras `"name": "k"`, dentro de la
  expresión usas `p_k`.

El codegen hace una sustitución de texto plano antes de pasar el
*script* a `scilab-cli`. Lo que escribes en `expression` puede
ser cualquier expresión Scilab válida —`abs(u1)`,
`sin(p_omega*t) + u1`, `if u1 > 0 then 1 else -1 end`—. El
editor no valida la sintaxis Scilab; si la expresión es
inválida, el solver fallará al arrancar y la barra de estado
mostrará el error.

## Los ejemplos incluidos

`doc/custom_nodes/` viene con dos descriptores de muestra:

- **`abs_value.json`** — `|u1|`. Un transformador sin
  parámetros que pasa la magnitud de su entrada.
- **`tripler.json`** — `3 · p_k · u1`. Un transformador con un
  parámetro `k`; con el valor por defecto produce `3 · u1`.

Sirven como plantilla para que escribas los tuyos.

## Cómo aparecen en el editor

Cargás un descriptor con la opción **Load custom from JSON…** del
popup de nodos, que abre un selector de archivos; el
`CustomNodeRegistry` lo registra al elegir el `.json`. A partir de
ahí el popup `Shift+A` lo muestra agrupado en una sección
**Custom**, debajo del catálogo *built-in*. Una vez insertado, un
nodo custom se
comporta exactamente como uno *built-in*: se cablea
respetando la gramática, sus parámetros se editan inline o en
el panel flotante, y su salida puede llegar a cualquier
sumidero.

## Limitaciones

- En esta versión sólo se pueden definir **transformadores con
  expresión algebraica**. Las fuentes y los sumideros custom
  exigen lógica que no cabe en una única expresión y se difieren
  a versiones futuras.
- La expresión no soporta nodos con estado propio (un custom no
  puede ser un integrador). Los nodos con estado están limitados
  al catálogo *built-in*.
- No hay *hot reload* de un descriptor ya cargado: como un
  `type_id` repetido se rechaza, para reflejar cambios en un
  `.json` ya registrado hay que reiniciar el editor.
