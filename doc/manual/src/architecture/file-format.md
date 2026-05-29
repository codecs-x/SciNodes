# Formato de archivo `.scn`

El grafo se persiste como JSON UTF-8 con la extensión `.scn`.
Está diseñado para ser legible a ojo y diff-eable en git.

## Esquema (v2)

```json
{
  "schema_version": 2,

  "metadata": {
    "name":     "walkthrough_E1_dc",
    "created":  "2026-05-10T14:23:00Z",
    "modified": "2026-05-27T19:11:00Z",
    "scinodes_version": "0.2.0"
  },

  "simulation": {
    "t_final": 5.0,
    "dt":      0.001,
    "solver":  "rk",
    "abs_tol": 1e-6,
    "rel_tol": 1e-6
  },

  "nodes": [
    {
      "id":   1,
      "type": "StepSignal",
      "name": "Setpoint",
      "position": { "x": 100, "y": 200 },
      "fields": {
        "Final value": { "value": 1.0, "unit": "rad" }
      }
    },
    {
      "id":   2,
      "type": "SubGraph",
      "name": "MotorDC",
      "position": { "x": 400, "y": 200 },
      "subgraph": { /* recursive: misma estructura que el root */ }
    }
  ],

  "edges": [
    { "id": 100, "from": { "node": 1, "port": 0 },
                "to":   { "node": 2, "port": 0 } }
  ],

  "displayUnits": {
    "5": { "0": "deg" }
  },

  "customNodeRefs": [
    "doc/custom_nodes/saturation.json"
  ],

  "assetRefs": [
    "assets/motor_dc.gltf"
  ]
}
```

## Decisiones

### Schema versionado

`schema_version` es entero monotónico.  Cada cambio
incompatible incrementa.  Los loaders saben migrar de la
versión anterior a la actual; saltar 2 versiones requiere
abrir-y-guardar con una versión intermedia.

Migraciones implementadas:

- v1 → v2: cambio de `params: { "Kp": 2.0 }` (number) a
  `fields: { "Kp": { "value": 2.0, "unit": "" } }` (Quantity).
- v2 → v3: futuro, no implementado.

### IDs estables

Cada nodo tiene un `id` numérico estable que persiste a través
de save/load.  Las aristas referencian nodos por ID, no por
nombre.  Esto permite renombrar un nodo sin romper el grafo.

### Posiciones sólo del top-level

Limitación conocida: las posiciones de los nodos **dentro de
sub-grafos** se persisten sólo en el renderer (memoria), no
en el `.scn`.  Si abrís el grafo en otra instalación, los
nodos del sub-grafo aparecen en su layout default y hay que
re-ordenar.

Solución diseñada (no implementada todavía): incluir el campo
`position` también en los sub-nodos, recursivamente.

### Custom nodes por referencia

Los nodos custom (CustomKind) NO se empotran en el `.scn`.
El JSON guarda sólo el path al archivo definitorio.  Esto
porque el JSON definitorio **es código** (incluye Scilab
inline) y debe vivir en su propio archivo para diff y revisión.

### Assets por referencia

Los `.gltf` tampoco se empotran.  El `.scn` referencia un path
relativo y el catálogo del proyecto resuelve.

## Validación al cargar

`NodeGraph::loadFromFile` corre:

1. JSON schema validation (estructura básica).
2. `GrammarParser::validateFull` (R1–R7).
3. `DimensionalAnalyzer::propagate` para llenar unidades
   derivadas.

Si algo falla, el grafo se carga **igualmente** con los
problemas marcados (aristas rojas, diagnósticos en el panel
de problemas).  El usuario decide si arreglar o descartar.

## Edición manual

Es perfectamente válido editar un `.scn` a mano:

```bash
$EDITOR examples/graphs/walkthrough_E1_dc_3d.scn
```

Sólo recordar:

- Mantener `schema_version` correcto.
- Re-correr el validator al abrir (`Archivo → Validar`).
- Los IDs deben permanecer únicos.

## Diff en git

Como es JSON ordenado por keys, los diffs de git son legibles:

```diff
   {
     "id": 5,
     "type": "Gain",
     "fields": {
-      "Gain": { "value": 2.0, "unit": "" }
+      "Gain": { "value": 5.0, "unit": "" }
     }
   }
```
