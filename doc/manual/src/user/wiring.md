# Cablear y validar

Las aristas se crean arrastrando del puerto de salida de un
nodo al puerto de entrada de otro.  La gramática evalúa cada
arista al soltarla y rechaza las inválidas con un mensaje
que nombra la regla que se viola.

## Reglas

| Regla | Lo que valida                                  |
|-------|------------------------------------------------|
| R1    | Tipos de los puertos compatibles               |
| R2    | Un puerto de entrada acepta ≤ 1 arista         |
| R3    | Grafo dirigido (sin self-loop salvo *pure state*) |
| R4    | Sin ciclos algebraicos (todo ciclo pasa por *pure state*) |
| R5    | Los sumideros tienen al menos una arista entrante |

## Lazos cerrados

R4 acepta ciclos que pasan por al menos un nodo de
**estado puro**: `Integrator`, `LowPassFilter`,
`DCMotorModel`, `TransferFunction`, `TransferFunction2`.
Esos nodos rompen el ciclo en tiempo de codegen porque su
salida es estado integrado, no una expresión instantánea de
su entrada.

## Ejemplo: lazo PID + motor DC

```
StepSignal(50)  ──┐
                  │
                  ▼
                 Sum(+,-) ──► PIDController ──► DCMotorModel ──► Oscilloscope
                  ▲                                 │
                  │                                 │
                  └─────────────────────────────────┘
                       (realimentación)
```

El lazo se cierra porque la velocidad del `DCMotor` (segunda
salida) vuelve al sumador.  El motor es de estado puro, así
que R4 acepta el ciclo.
