# Unidades y análisis dimensional

SciNodes valida unidades como parte de la gramática del grafo
(regla R7).  Esta página explica qué significa eso para el
usuario.

## Qué resuelve

En un editor de bloques tradicional, escribís `120` y queda como
**120**, sin contexto.  Si después ese valor entra a un
integrador que espera radianes pero vos pensaste grados, el
plot sale 57 veces más grande y descubrís el error horas
después.

SciNodes hace que el campo entienda `120 deg` o `2 rad` o
`0.5 turn`.  Si después un cable conecta `rad` con un nodo que
espera `m`, el cable queda rojo y la simulación no arranca —
R7 enforcement.

## Dos tipos de field

SciNodes distingue **fields físicos** (unidad declarada en el
registry) de **fields ideales** (coeficientes adimensionales).
Las reglas para escribir en cada uno son distintas:

| Tipo                  | Ejemplos                                                    | Acepta sufijo (`60 deg`) | Acepta prefijo SI (`2k`) |
|-----------------------|-------------------------------------------------------------|--------------------------|--------------------------|
| **Físico**             | `DCMotorModel.Ra [Ohm]`, `DCMotorModel.La [H]`, `SineSignal.Phase [rad]`, `Vec3Constant.x [m]` | sí, si la dimensión coincide | sí |
| **Ideal / matemático** | `StepSignal.Amplitude`, `Gain.Gain`, `Summation.Sign1`, exponentes de `TransferFunction`, `PIDController.N (filter)` | no — el field es un coeficiente, sólo número plano | no |

## Qué se puede escribir en un campo físico

| Entrada            | Interpretación                                |
|--------------------|-----------------------------------------------|
| `120`              | Número en la unidad declarada del field.      |
| `120 deg`          | 120 grados (si el field acepta unidad angular).  Se guarda como Quantity{120, deg}. |
| `2.094 rad`        | 2.094 radianes.                               |
| `100 cm`           | 100 centímetros (si el field acepta longitud). Magnitud parseada del prefijo. |
| `2k`               | 2000 en la unidad del field (`2 kΩ` si es `Ra`, etc.). |
| `1 km/h`           | Composición.  Se simplifica a m/s al propagar.|
| `9.81 m/s^2`       | Aceleración.                                  |
| `60 Hz`            | Frecuencia.                                   |
| `120 V` en un field con `Ohm` | **rechazado** — cable rojo + diagnóstico R7. |

## Qué se puede escribir en un campo ideal

Sólo número plano:

| Entrada            | Interpretación                                |
|--------------------|-----------------------------------------------|
| `1`                | Coeficiente 1.0.  La unidad efectiva la pone el cable downstream vía propagación R7. |
| `2.5`              | Idem.                                          |
| `60 deg`           | **No funciona** — el field no tiene unidad declarada; sólo lee el número y descarta el sufijo (o muestra error).  Para semantizar como grados, insertá un nodo `DegToRad` entre el Source ideal y el consumer. |

> La asimetría es a propósito: separar "coeficiente
> matemático" de "cantidad física" hace que los nodos
> matemáticos (`Gain`, `Summation`, `TransferFunction`) sean
> reusables en cualquier dominio sin acarrear una unidad que
> después haya que reconciliar.

## Notación

- **Prefijos SI**: `n p μ m k M G T` antes de la unidad
  (`5 mA`, `1.2 GHz`).
- **Potencias**: `^` o sufijo (`m^2`, `m2`).
- **Composición**: `·` o ` ` (`N m`, `N·m`).
- **División**: `/` (`m/s`, `kg/(m s^2)`).

## Catálogo canónico

| Magnitud físical    | Unidad SI canónica  | Aliases aceptados             |
|---------------------|----------------------|-------------------------------|
| Tiempo              | s                    | sec, second                   |
| Longitud            | m                    | meter, metre                  |
| Masa                | kg                   |                                |
| Corriente           | A                    | ampere, amp                   |
| Temperatura         | K                    | kelvin                        |
| Cantidad de sustancia | mol                |                                |
| Intensidad luminosa | cd                   | candela                       |
| Ángulo (fantasma)   | rad                  | radian (1 turn = 2π rad)      |
| Fuerza              | N                    | newton                        |
| Energía             | J                    | joule                         |
| Potencia            | W                    | watt                          |
| Voltaje             | V                    | volt                          |
| Frecuencia          | Hz                   | hertz                         |
| Velocidad angular   | rad/s                | rpm (con conversión)          |

> El ángulo es la "octava dimensión fantasma": no entra en las
> 7 fundamentales del SI, pero SciNodes lo trata como dimensión
> propia para detectar errores comunes (multiplicar ángulos por
> ángulos sin pasar por trigonometría).

## Propagación por el grafo

Cada nodo declara cómo transforma la unidad de su salida:

| Nodo            | Regla de propagación                        |
|-----------------|---------------------------------------------|
| `Gain`           | unidad pasa igual                           |
| `Summation`      | unidad de todas las entradas debe coincidir |
| `Saturation`     | unidad pasa igual (no escala)              |
| `Integrator`     | unidad · dominio (típicamente `× s`; `rad/s → rad`) |
| `Differentiator` | unidad / dominio (típicamente `/ s`; `rad → rad/s`) |
| `DegToRad`       | `deg → rad`                                 |
| `Vec3*`          | propaga por componente                      |

## Errores comunes y cómo lee SciNodes

| Síntoma                                  | Causa probable                                   | Cómo arreglar                                      |
|------------------------------------------|--------------------------------------------------|-----------------------------------------------------|
| Cable rojo entre dos nodos.              | Las unidades no son compatibles (R7).            | Insertá un convertidor (`DegToRad`, etc.) o revisá el field. |
| Botón Run deshabilitado.                 | Hay aristas R7-incompatibles.                    | Buscá los cables rojos.                            |
| El plot muestra 57× lo esperado.         | Olvidaste la unidad en un campo y SciNodes lo interpretó como SI. | Doble-click al field, escribí la unidad real.     |
| El Oscilloscope muestra eje Y vacío.     | La unidad inferida es desconocida (composición no canónica). | Es estético; el cómputo está bien.                |

## Overrides — dos cosas distintas

Hay dos mecanismos de override fácilmente confundibles:

- **`portUnitOverrides`** (afecta cómputo + R7).  Si un puerto
  *no* declara unit en el registry — típico de Sources
  polimórficos como `StepSignal.out` o de matemáticos como
  `Gain.out`, `Summation.out` —, podés forzarle una unit desde
  el panel del nodo.  Esa instancia deja de ser polimórfica
  para ese puerto y R7 chequea contra el consumer.  Se persiste
  por nodo en el `.scn`.  Limitación: **no anula** puertos ya
  declarados en el registry — `DCMotorModel.in [V]` es inmune.
- **`displayUnits`** (sólo presentación).  Mapa a nivel
  proyecto `{Dimensión → Unit}`.  Si el cable lleva `rad` pero
  querés que el `Oscilloscope` rotule en `°`, configurá el
  display.  El cómputo no cambia y R7 sigue mirando los
  exponentes, no la magnitud.

Ver [Análisis dimensional: unidades + R7](dimensional.md) para
el detalle del analizador y la propagación.
