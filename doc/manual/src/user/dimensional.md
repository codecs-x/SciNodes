# Análisis dimensional: unidades + R7

A partir de v0.0.9 cada puerto puede declarar una **unidad** en
el registro de tipos (volts, kg·m², rad/s, …) y cada cable que
tendés pasa por la **regla R7**: las unidades de los dos
extremos tienen que ser dimensionalmente compatibles, si no el
editor rechaza la conexión.

## La regla, exacta

R7 se chequea en `NodeGraph::tryAddEdge` (`src/core/NodeGraph.cpp:513-548`)
con una estrategia pre/post: corre el analizador dimensional
**antes** de añadir el cable y **después** de añadirlo
tentativamente. Si la cantidad de conflictos sube, el cable es
responsable de al menos uno → se rechaza con código `R7`.

La comparación es **`Unit::sameDimension`** (`src/core/Unit.hpp:65`):
compara únicamente el vector de 8 enteros con los exponentes
(7 SI + 1 ángulo "fantasma"). La **magnitud no se compara**: por
eso `100 cm` y `1 m` son compatibles; `60 deg` y `1.047 rad`
también (ambos `exp = (0,0,0,0,0,0,0,1)`).

## El analizador en cuatro fases

`analyzeUnits` (`src/core/DimensionalAnalyzer.cpp:69-259`) corre
hasta punto fijo (cap 32 iteraciones, los grafos típicos
convergen en 2-3):

1. **Seed**. Toma las unidades declaradas en el registry
   (`NodeDef::inputPortUnits`, `NodeDef::outputPortUnits`) y los
   overrides per-instancia (`NodeInstance::portUnitOverrides`).
   Los overrides solo se aplican a puertos *no* declarados en
   el registry — un `DCMotorModel.in [V]` es inmune a override.

2. **Propagación por edge**. Para cada cable:
   - si ambos extremos conocen su unit → chequea
     `sameDimension`; conflicto si no.
   - si solo el origen conoce → asigna esa unit al destino.
   - si solo el destino conoce → **asigna esa unit al origen**
     ("propagación backward"). Este es el punto clave: una
     señal sin unit declarada hereda la del consumer.

3. **Unit-transformers** (`Integrator`, `Differentiator`). La
   relación dimensional se calcula desde
   `NodeGraph::domainUnit()` (default `s`, time-domain):
   - `Integrator` (`MultiplyDomain`): `out = in × s`. Si
     `in = rad/s`, entonces `out = rad`.
   - `Differentiator` (`DivideDomain`): `out = in / s`. Si
     `in = rad`, entonces `out = rad/s`.

4. **Nodos polimórficos**. Un nodo es *fully polymorphic* si
   (`DimensionalAnalyzer.cpp:22-33`):
   - el registry no declara unit en ningún puerto suyo,
   - no hay overrides per-instancia,
   - no es unit-transformer (Integrator/Differentiator),
   - no es alias-like.
   En ese caso, **todos sus puertos comparten unit**. Si alguno
   tiene unit conocida (por seed o propagación), los demás la
   heredan en el siguiente paso del fixed-point.

5. **Alias** (`NodeType::Alias`). Su único output toma la unit
   del puerto del nodo target referenciado por `target_node_id`
   / `target_port`.

## Nodos polimórficos en la práctica

`StepSignal`, `SineSignal`, `RampSignal`, `VoltageSource`,
`CurrentSource`, `Gain`, `Summation` y muchos otros nodos
matemáticos están registrados **sin** declarar
`outputPortUnits` ni `inputPortUnits`. Caen en la regla del
paso 4 (nodos *fully polymorphic*): sus puertos heredan la
unidad del primer nodo dimensionado con el que el grafo los
conecte.

Ejemplos del catálogo actual:

| Cableado | R7 |
|----------|----|
| `StepSignal.out → DCMotorModel.in [V]` | **acepta**: StepSignal adopta `V` |
| `StepSignal.out → GearTransmission.in [rad/s]` | **acepta**: StepSignal adopta `rad/s` |
| `DegToRad.out [rad] → GearTransmission.in [rad/s]` | **rechaza**: `rad` y `rad/s` difieren en exp[2] y exp[7] |
| `DCMotorModel.out [rad/s] → DCMotorModel.in [V]` | **rechaza**: `rad/s` ≠ `V` |

Para forzar que un puerto polimórfico lleve una unidad
específica (en vez de heredarla del consumer), usá el
**override per-instancia** descrito abajo.

## Cómo se ve el rechazo

Cuando `tryAddEdge` devuelve `GrammarError{"R7", …}`, la UI:
- no dibuja el cable,
- pone un badge rojo sobre el nodo destino,
- escribe en la status bar el mensaje del primer conflicto
  nuevo (formato:
  `Edge dimensional mismatch: <unit-origen> → <unit-destino>`).

## Conversores explícitos

Los nodos `DegToRad` y `RadToDeg` (registro
`src/core/NodeType.cpp:795-817`) declaran `in` y `out`
explícitamente:

| Nodo       | input | output |
|------------|-------|--------|
| `DegToRad` | deg   | rad    |
| `RadToDeg` | rad   | deg    |

Ambas unidades tienen `exp = (0,0,0,0,0,0,0,1)` (ángulo
fantasma), pero `deg` lleva `magnitude = π/180` y `rad`
`magnitude = 1.0`. Como `sameDimension` ignora la magnitud, R7
los considera compatibles para cualquier cable entrante o
saliente — el conversor compone el factor numérico en el
codegen Scilab, no en la chequeo de R7.

## Overrides per-instancia

Si un puerto **no** declara unit en el registry y querés
forzarle una desde el grafo, abrí el panel del nodo y editá el
override de unidad. Se guarda en `NodeInstance::portUnitOverrides`
y se persiste en el `.scn`. Útil para tipear las salidas de los
Sources polimórficos: forzando `StepSignal.out = V` se convierte
en *no-polimórfico* para esa instancia, y a partir de ahí R7 sí
chequea contra el consumer.

Limitación: el override no funciona sobre puertos ya declarados
en el registry — ahí el registry manda.

## displayUnits (presentación, no cómputo)

`NodeGraph::displayUnits()` es un mapa
`{DimensionKey → Unit}` a nivel proyecto. Lo usa la UI para
mostrar valores en la unidad preferida del usuario (ver
oscilloscope en `°` aunque el cable lleve `rad`) sin tocar el
cómputo. No participa en R7.

## Errores frecuentes

| Síntoma | Causa probable | Cómo arreglar |
|---------|----------------|---------------|
| Cable rojo + status bar `R7: Edge dimensional mismatch` | exp[] de origen y destino difieren | Insertá un conversor explícito (`DegToRad`, etc.) o ajustá la unit declarada del field. |
| Botón Run deshabilitado | hay aristas con conflicto R7 sin resolver | Pasá el ratón sobre los badges rojos para ver el diagnóstico. |
| El plot muestra `× 57` lo esperado | escribiste un número en `deg` pero el campo era `rad`, o el conversor de ángulos no está en el cable | Doble-click al field y escribí el sufijo (`60 deg`), o insertá un `DegToRad` en el cable. |
| El Oscilloscope rotula `?` | la unit inferida es una composición no canónica que no tiene nombre en el catálogo | Es estético; el cómputo es correcto. |
