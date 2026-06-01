# Dispositivos: contratos + glTF

En v0.0.7 los dispositivos físicos —motores, transmisiones,
brazos, sensores— dejan de ser nodos *hard-coded* en el
catálogo y pasan a ser **datos**: un contrato JSON
declarativo más un asset glTF cargado en *runtime*.

## La cuarta categoría: `NodeCategory::Device`

La gramática gana una categoría adicional, `Device`, que se
comporta como `Transformer` en las reglas R1–R5 (puede ser
fuente o destino de cables, dentro de las restricciones de
categoría) pero está respaldada por un contrato externo en
lugar de una implementación nativa.

El primer y único *device* del catálogo a partir de v0.0.7
es `DCMotorModel`, migrado desde Transformer.

## Contratos JSON: `contracts/<device>.json`

Cada contrato describe el dispositivo en forma declarativa:

```json
{
  "device_type": "DCMotorModel",
  "label": "DC Motor (Faulhaber 2342)",
  "ports": {
    "inputs":  [ {"name": "v", "unit": "V"} ],
    "outputs": [ {"name": "omega", "unit": "rad/s"} ]
  },
  "params": [
    {"name": "R", "default": 14.0, "unit": "Ohm"},
    {"name": "L", "default": 0.001, "unit": "H"},
    {"name": "Ke", "default": 0.05, "unit": "V*s/rad"},
    {"name": "J", "default": 1e-5, "unit": "kg*m^2"}
  ],
  "asset": {
    "gltf": "examples/dc_motor/dc_motor.gltf",
    "joints": [ {"name": "shaft", "axis": [0, 1, 0]} ]
  }
}
```

El `ContractRegistry` parsea los archivos al arranque y
registra cada uno como un nodo del catálogo. Si el campo
`device_type` no coincide con el `typeName` del enum nativo
correspondiente, el editor lo reporta en la barra de estado y
no carga el contrato.

## Cargar geometría: `DeviceAssetLoader`

El campo `asset.gltf` apunta a un archivo glTF + .bin. El
`DeviceAssetLoader` lo parsea con tinygltf, extrae la
jerarquía de partes (mallas + transformaciones locales) y la
expone al `View3DPanel`. El visor lo renderiza con un segundo
pipeline Vulkan (`asset.vert`/`asset.frag`) con shading
Lambert, depth buffer real y tres modos visuales:
`Wire`, `Solid`, `Both`. Un *auto-fit* inicial encuadra el
modelo y una guarda evita tamaños degenerados (cuando el
glTF llega sin `extent` válido).

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
