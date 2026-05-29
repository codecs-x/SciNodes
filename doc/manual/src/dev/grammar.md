# Gramática R1–R5

`GrammarParser` (`src/core/GrammarParser.{cpp,hpp}`) evalúa
una arista candidata y devuelve un código de error con la
regla violada, si alguna.

| Regla | Predicado                                              |
|-------|--------------------------------------------------------|
| R1    | Los tipos de los puertos son compatibles               |
| R2    | El puerto de entrada destino tiene ≤ 1 arista existente |
| R3    | El grafo es dirigido — no se permiten self-loops salvo en *pure state* |
| R4    | Todo ciclo pasa por al menos un nodo de *estado puro*  |
| R5    | Todo sumidero conectado tiene al menos una entrada     |

## *Pure state*

Un nodo es *pure state* si su salida es una variable de
estado del solver: `Integrator`, `LowPassFilter`,
`DCMotorModel`, `TransferFunction`, `TransferFunction2`.
Su salida no depende de su entrada instantáneamente; eso
permite que un ciclo en el grafo se interprete como un
sistema dinámico bien planteado.
