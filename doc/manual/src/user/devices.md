# Dispositivos: contratos + glTF

En v0.0.7 los dispositivos físicos —motores, transmisiones,
brazos, sensores— dejan de ser nodos *hard-coded* en el
catálogo y pasan a ser **datos**: un contrato JSON
declarativo más un asset glTF cargado en *runtime*.

## La cuarta categoría: `NodeCategory::Device`

La gramática gana una categoría adicional, `Device`, que en las
reglas de conexión (R0–R7) se comporta como `Transformer` (puede
ser fuente o destino de cables, dentro de las restricciones de
categoría), pero además puede llevar un **contrato de geometría** y
un modelo glTF asociado.

El primer y único *device* del catálogo a partir de v0.0.7
es `DCMotorModel`, migrado desde Transformer.

## Contratos de geometría: `contracts/<device>.json`

Un contrato **no** redefine los puertos ni los parámetros del nodo
—esos los declara el catálogo built-in—: describe, de forma
declarativa, la **geometría** que un modelo 3D debe cumplir para
representar al dispositivo. Va *keyado* por `device_type`, que debe
coincidir con un `NodeType` de categoría `Device`:

```json
{
  "device_type": "DCMotorModel",
  "version":     "0.1",
  "description": "Motor DC con escobillas, alimentación bipolar.",
  "parts": [
    { "name": "shaft",   "kind": "mesh", "required": true, "doc": "Pieza que rota." },
    { "name": "housing", "kind": "mesh", "required": true, "doc": "Carcasa estática." }
  ],
  "joints": [
    { "name": "shaft_bearing", "type": "revolute", "parent": "housing",
      "child": "shaft", "driven_by": "omega", "required": true }
  ],
  "anchors": [
    { "name": "terminal_plus",  "kind": "electrical",   "required": true },
    { "name": "winding",        "kind": "thermal_zone", "required": false,
      "doc": "Si está presente, recibe T(t) del ThermalSink." }
  ]
}
```

- **`parts`** — las mallas que el modelo debe tener (`shaft`, `housing`).
- **`joints`** — articulaciones: una `revolute` con `parent`/`child` y
  `driven_by` apuntando a un puerto del nodo (`omega` mueve el `shaft`).
- **`anchors`** — puntos de anclaje lógicos: bornes `electrical`, zonas
  `thermal_zone`, etc.

El `ContractRegistry` parsea los archivos de `contracts/` al arranque
(*keyados* por `device_type`). Si `device_type` no coincide con un
`NodeType` de categoría `Device`, el editor lo reporta y no carga el
contrato.

## Cargar geometría: `DeviceAssetLoader`

El glTF **no** vive en el contrato: cada instancia `Device` referencia
su propio archivo (`asset_path` en el `.scn`). El `DeviceAssetLoader`
lo parsea con tinygltf (acepta `.gltf` y `.glb`) produciendo un
`DeviceAsset` que **valida** que las `parts` y `joints` marcados
`required` por el contrato aparezcan en el modelo, y lo expone al
`View3DPanel`. El visor lo renderiza con un segundo
pipeline Vulkan (`asset.vert`/`asset.frag`) con shading Lambert,
*depth buffer* real y tres modos visuales: `Wire`, `Solid`, `Both`.
Un *auto-fit* inicial encuadra el modelo y una guarda evita tamaños
degenerados (cuando el glTF llega sin `extent` válido).

## Binding in-app: `AssetMappingPanel`

Las partes del glTF (`stator`, `housing`, `shaft`, ...)
suelen estar nombradas de manera distinta a los puertos del
contrato. Un **sidecar JSON** asocia partes a puertos sin
tocar el `.gltf`. El `AssetMappingPanel` deja autorearlo:
seleccionas un puerto, eliges la parte que lo representa, y
la asociación queda guardada en `examples/dc_motor/
mapping.json`. El visor refleja el cambio inmediatamente.

## El catálogo de devices: `OutlinerPanel`

El `OutlinerPanel` es la vista jerárquica del catálogo de
devices cargados en el proyecto:

```
DC Motor (Faulhaber 2342)
├── stator       (asset:housing)
├── shaft        (asset:shaft)          ← jointed Y+
└── output_pulley(asset:pulley)
```

Hacer doble click sobre un device lo agrega al canvas como
nodo. Hacer click sobre una parte abre el panel de mapping
con esa parte preseleccionada.

## Fixtures: `examples/`

`examples/dc_motor/` contiene un glTF de referencia y un
mapping sidecar. `examples/graphs/` reúne las caminatas E1–E9
que la suite de integración usa como fixtures —
`walkthrough_E1.scn`, `walkthrough_E1_dc.scn`,
`walkthrough_E1_dc_3d.scn`, `walkthrough_E2.scn` … hasta
`walkthrough_E9.scn`, incluyendo variantes como `E3b`. El
`ExamplesBrowser` las expone in-app desde **Ayuda → Ejemplos**.
