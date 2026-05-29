# Formato `.scn`

`ScnSerializer.{cpp,hpp}` serializa el `NodeGraph` a JSON
con `nlohmann/json`, en orden creciente de id para que el
archivo sea *diff-able*.

## Estructura mínima

```json
{
  "version": "0.1",
  "nodes": [
    { "id": 1, "type": "StepSignal", "x": 100, "y": 200,
      "params": { "Step Time": 0.0, "Amplitude": 50.0 } }
  ],
  "edges": [
    { "from": [1, 9000], "to": [2, 0] }
  ]
}
```

`(nodeId, attrId)` codifica el puerto exacto: los inputs son
`0..99`, los outputs son `100..199`, ambos sumados al `id`
del nodo × 10000.

## Carga con reporte

`loadFromFile(path)` devuelve un `LoadReport` que distingue
entre fallo fatal (archivo inválido, versión no soportada) y
fallos no fatales (`unknownTypes`, `rejectedEdges`).
Permite cargar `.scn` antiguos con tipos eliminados, con un
*diff* visible al usuario.
