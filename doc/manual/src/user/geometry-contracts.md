# Contratos de geometría + assets glTF

Cuando un nodo del grafo representa un dispositivo físico
real (motor DC, brazo, articulación), puede asociarse a un
*asset* glTF que el visor 3-D anima a partir de las salidas
del solver.

## Cómo se conecta

1. **Contrato JSON.**  En `contracts/dc_motor.json` se
   declara el `device_type`, sus *joints* (qué transforma
   qué) y el mapeo entre puertos del nodo y huesos del
   asset.
2. **Asset glTF.**  Un `.glb` o `.gltf` (con `tinygltf`) que
   contiene la malla + esqueleto.  Una plantilla Blender
   está en `tools/blender/setup_dc_motor.py` para producir
   el asset siguiendo la convención del contrato.
3. **Categoría `Device`.**  El nodo aparece en el popup
   bajo "Devices".  Al dropear, el editor busca un contrato
   cuyo `device_type` coincide con el `typeName` del nodo y
   carga el asset asociado.
4. **`OutlinerPanel`.**  Vista jerárquica de los
   dispositivos cargados y sus *parts*.

## Cómo se anima

Las salidas escalares del nodo (típicamente ángulos
articulares) se mapean a transformaciones de huesos del
asset según el contrato.  En el caso del motor DC, la
velocidad angular se integra a un ángulo y se aplica al
joint del eje.  El `View3DPanel` dibuja el resultado en
tiempo real.

## Limitaciones

- *Joints* sin metadata de eje toman `+Y` por defecto.
- La carga de un contrato es al iniciar; modificar el
  `.json` mientras el editor corre no recarga.
