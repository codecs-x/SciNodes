# Formato `.scn`

`.scn` es el formato de persistencia del grafo. JSON simple,
diseñado para ser:

- **Legible.** Útil para *diff* en git, revisión a ojo.
- **Robusto.** Cargar un archivo con tipos desconocidos no falla;
  reporta el problema y abre el grafo en modo de sólo lectura.
- **Versionado.** El campo `scnodes_version` permite migraciones
  futuras sin romper archivos viejos.

## Esquema (`scnodes_version = "0.3"`)

```json
{
  "scnodes_version": "0.3",
  "next_node_id": 7,
  "nodes": [
    { "id": 1, "type": "VoltageSource",
      "position": [120.0, 80.0],
      "params": { "Voltage": 12.0, "Int. Resistance": 0.1 } },
    { "id": 2, "type": "DCMotorModel",
      "position": [320.0, 80.0],
      "params": { "R": 1.0, "L": 0.5, "J": 0.01, "b": 0.1, "K": 0.05 } }
  ],
  "edges": [
    { "id": 1, "from_node": 1, "to_node": 2, "to_port": 0 }
  ]
}
```

### Campos

- **`scnodes_version`** — *string* con la versión del formato (no
  del editor). Al cargar, si no coincide con la constante
  `ScnSerializer::FILE_VERSION`, se reporta como
  `unknownTypes` con el mensaje *version mismatch*.
- **`next_node_id`** — el id máximo en uso + 1. Persistirlo evita
  colisiones al cargar un grafo y luego agregar nodos.
- **`nodes`** — lista de nodos.
  - **`id`** — entero único.
  - **`type`** — nombre canónico del tipo (`typeName(NodeType)`).
  - **`position`** — `[x, y]` en coordenadas del canvas. *Las
    posiciones las gestiona `NodeCanvas` vía
    `ScnPositions = unordered_map<int, ScnVec2>` separado del
    `NodeInstance` (el modelo puro no tiene coordenadas).*
  - **`params`** — objeto JSON `{ "nombre": valor }`. El orden de
    serialización lo da `nlohmann/json`.
- **`edges`** — lista de aristas.
  - **`id`** — entero único.
  - **`from_node`** / **`to_node`** — ids de los nodos extremos.
  - **`to_port`** — índice del puerto de entrada destino
    (`0`, `1`, …). El puerto de salida no necesita índice porque
    cada nodo tiene una sola salida.

## `LoadReport`

`ScnSerializer::deserialize` no lanza excepciones. Devuelve un
`LoadReport` con:

- **`graph`** — el `NodeGraph` reconstruido (nodos y aristas que
  sí se pudieron interpretar).
- **`version`** — el campo `scnodes_version` del archivo.
- **`unknownTypes`** — lista de problemas: tipos desconocidos,
  campos requeridos faltantes, mismatch de versión.
- **`rejectedEdges`** — aristas que la gramática rechazó al
  reinsertarlas (`R0–R5`); cada una con su código de regla,
  mensaje y nodos involucrados.
- **`readOnly`** — `true` si hubo errores. El canvas abre el grafo
  bloqueado y muestra los mensajes en la barra de estado.

Esto permite abrir un `.scn` generado por una versión más nueva
sin perder lo que sí se pudo interpretar.
