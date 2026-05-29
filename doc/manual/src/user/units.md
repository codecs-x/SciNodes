# Análisis dimensional + unidades

El editor entiende unidades físicas como parte del cableado.

## Cómo aparece

- Cada *port* de cada nodo lleva su unidad esperada (ej.
  V para `VoltageSource`, rad/s para la velocidad del
  motor).  Aparece a la derecha del *port label* entre
  corchetes: `Voltage [V]`.
- Cada *param* lleva su unidad por defecto.  El widget
  `QuantityField` acepta valor + unidad en el mismo input:
  `12 V`, `100 mΩ`, `2 kΩ`, `60 Hz`.
- **R7** (regla nueva, *enforcement* HARD): una arista entre
  puertos con unidades dimensionalmente incompatibles se
  rechaza al cablear.  Mensaje: "expected `V`, got `A`".

## Cambiar la unidad de display

El panel de parámetros del nodo (doble-click) permite
*override* per-instance: si el sumidero está dibujando en
rad/s y querés grados/s, lo cambiás ahí y la conversión se
aplica automáticamente.

## Conversores explícitos

Para puentes entre unidades que no son escalado lineal
trivial (típicamente ángulos), el catálogo trae:
- `DegToRad`
- `RadToDeg`

El codegen los implementa como `pi/180` y `180/pi`
respectivamente.

## Cuándo el sistema NO inferirá

- **Fields ideales** (parámetros como exponentes,
  coeficientes adimensionales) aceptan solo escalares.
- **Phantom angle exponent** — los radianes / grados se
  tratan como una "8.ª dimensión" cosmética para que el
  análisis no confunda `velocidad angular [rad/s]` con
  `frecuencia [Hz]`.
