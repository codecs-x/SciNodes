# Formato `.scn`

`.scn` es el formato de persistencia del grafo. JSON simple,
diseñado para ser:

- **Legible.** Útil para *diff* en git, revisión a ojo.
- **Robusto.** Cargar un archivo con tipos desconocidos no falla;
  reporta el problema y abre el grafo en modo de sólo lectura.
- **Versionado.** El campo `scnodes_version` permite migraciones
  futuras sin romper archivos viejos.

## Versión actual: `0.5`

El loader acepta archivos en `"0.3"`, `"0.4"` y `"0.5"` y los
re-emite en `"0.5"` al guardar.

| versión | introduce |
|---|---|
| `0.3` | esquema base (nodes + edges + params) |
| `0.4` | `subgraph` recursivo en nodos `SubGraph` container |
| `0.5` | `objects` top-level (catálogo de assets 3D), `comment`, `string_params`, `port_units`, `port_type`, `asset_path`, `domain_unit`, `display_units`, `id`, `title`, `description`, `tags` |

## Esquema mínimo

```json
{
  "scnodes_version": "0.5",
  "next_node_id": 7,
  "nodes": [
    { "id": 1, "type": "VoltageSource",
      "position": [120.0, 80.0],
      "params": { "Voltage": 12.0, "Int. Resistance": 0.1 } },
    { "id": 2, "type": "DCMotorModel",
      "position": [320.0, 80.0],
      "params": { "Ra": 1.0, "La": 0.01, "Ke": 0.1,
                  "Kt": 0.1, "J": 0.01, "B": 0.001 } }
  ],
  "edges": [
    { "id": 1, "from_node": 1, "to_node": 2,
      "from_port": 0, "to_port": 0 }
  ]
}
```

## Campos top-level

- **`scnodes_version`** — *string* con la versión del formato (no
  del editor). Al cargar, si no coincide con
  `ScnSerializer::FILE_VERSION` ni con una versión legacy
  soportada, `LoadReport` reporta *version mismatch*.
- **`next_node_id`** — entero `max(node.id) + 1`. Persistirlo
  evita colisiones al cargar un grafo y luego agregar nodos.
- **`nodes`** — array de objetos nodo.
- **`edges`** — array de objetos arista.

### Metadata opcional del grafo (v0.0.9+)

Editable desde el panel **Help → Sobre este grafo**:

- **`id`** — UUID estable del grafo.
- **`title`** — título legible.
- **`description`** — descripción larga.
- **`tags`** — `["control", "pmsm", ...]` para búsqueda /
  clasificación.

### Catálogo de objetos 3D (v0.5+)

- **`objects`** — array de objetos importados desde
  **Archivo → Importar modelo 3D**. Solo se emite si está
  poblado para no ensuciar los `.scn` legacy:
  ```json
  "objects": [
    { "name": "DC Motor", "path": "examples/dc_motor/dc_motor.gltf",
      "parts": ["stator", "shaft", "housing"] }
  ]
  ```

### Análisis dimensional (v0.0.9+)

- **`domain_unit`** — string canónico (e.g. `"s"`, `"Hz"`). Define
  la unidad del dominio (variable independiente) para
  `Integrator`/`Differentiator` domain-aware. Default `"s"` no se
  serializa.
- **`display_units`** — array de strings de unidad. Indica las
  unidades **preferidas** del usuario por dimensión SI; el
  Oscilloscope las usa por defecto al renderizar. Solo se emite
  si hay overrides.

## Campos por nodo

| Campo | Tipo | Significado |
|---|---|---|
| `id` | int | Único en el grafo |
| `type` | string | Nombre canónico del tipo (`typeName(NodeType)`) |
| `position` | `[x, y]` | Coordenadas en model units del canvas |
| `params` | `{name: number}` | Parámetros numéricos |
| `asset_path`* | string | Ruta al asset glTF (nodos `Object3D` / `Device`) |
| `comment`* | string | Comentario libre del usuario sobre el nodo |
| `string_params`* | `{name: string}` | Parámetros string (e.g. `Contract::typeId`, `Alias::target`) |
| `port_units`* | `{slot: string}` | Overrides per-instance de unidades (`in0`, `in1`, `out0`, ...) |
| `port_type`* | string | Override de `TypeExpr` (solo stubs `SubGraphInput`/`SubGraphOutput` no escalares) |
| `subgraph`* | object | Cuerpo del grafo hijo si el nodo es un `SubGraph` container |

\* Los campos opcionales solo se emiten si están poblados.

### Detalles de algunos campos

- **`port_units`** persiste el texto verbatim que el usuario
  escribió en el override; no se canonicaliza con
  `Unit::toCanonicalString` porque para unidades adimensionales
  (rad, dB) la canonicalización pierde el nombre. El round-trip
  es lossless.
- **`port_type`** solo aparece en `SubGraphInput`/`SubGraphOutput`
  cuando el tipo del stub no es escalar. El contenedor
  `SubGraph` **no** emite tipos; `recomputeSubGraphPorts` los
  reconstruye desde los stubs en load.
- **`subgraph`** es recursivo: lleva la misma estructura que el
  grafo top-level (sin `scnodes_version`, sin `objects`).
- **`asset_path`** es relativo al `.scn` o absoluto. El
  `AssetService` lo resuelve al cargar.

## Campos por arista

| Campo | Tipo | Significado |
|---|---|---|
| `id` | int | Único en el grafo |
| `from_node` | int | id del nodo origen |
| `to_node` | int | id del nodo destino |
| `from_port` | int | Índice del puerto de salida origen (0-based) |
| `to_port` | int | Índice del puerto de entrada destino. Para per-param pins, apunta al pin del parámetro correspondiente |

## `LoadReport`

`ScnSerializer::deserialize` no lanza excepciones. Devuelve un
`LoadReport` con:

- **`graph`** — el `NodeGraph` reconstruido (nodos y aristas que
  sí se pudieron interpretar).
- **`version`** — el campo `scnodes_version` del archivo.
- **`unknownTypes`** — lista de problemas: tipos desconocidos,
  campos requeridos faltantes, mismatch de versión.
- **`rejectedEdges`** — aristas que la gramática rechazó al
  reinsertarlas (`R0–R7`); cada una con su código de regla,
  mensaje y nodos involucrados.
- **`readOnly`** — `true` si hubo errores. El canvas abre el grafo
  bloqueado y muestra los mensajes en la barra de estado.

Esto permite abrir un `.scn` generado por una versión más nueva
sin perder lo que sí se pudo interpretar.

## Auditoría

`tools/scn_format_coverage.py` extrae los campos JSON usados por
`src/core/ScnSerializer.cpp` (regex sobre `j["..."]`,
`j.contains("...")`, `j.value("...", ...)`) y verifica que todos
estén listados en `doc/db/file_format.json` y mencionados en
esta página. Cualquier campo nuevo agregado al serializador sin
actualizar la doc se detecta en el próximo `tag`.
