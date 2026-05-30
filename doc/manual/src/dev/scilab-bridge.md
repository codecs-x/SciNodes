# Puente con Scilab

`ScilabBridge` (en `src/core/`) abstrae el subproceso `scilab-cli`.
La UI nunca habla directo con Scilab — siempre vía el puente.

## Ciclo de vida

| Método                    | Qué hace                                                       |
|---------------------------|-----------------------------------------------------------------|
| `reset(graph)`            | Mata el hijo anterior (si existe), genera el *driver* Scilab a partir del grafo, lanza `scilab-cli` y deja al hijo listo en `Ready`. |
| `step(dt)`                | Avanza la simulación `dt` segundos: pide un paso, lee el vector de estado por *stdout*, actualiza los buffers. Sincrónico, llamable desde la UI cuando no hay hilo del solver. |
| `startSolverThread(dt)`   | Lanza un hilo dedicado que entra en un bucle de `step(dt)` con cadencia gobernada por `std::chrono::steady_clock`. Mientras el hilo corre, la UI **no** debe llamar a `step()`. |
| `stopSolverThread()`      | Pone el flag atómico a *off*, espera al `join`, y deja al `bridge` en `Ready`. |
| `sendParameter(...)`      | *Live tuning*: reasigna el valor de un parámetro en caliente. *Fire-and-forget* — no espera respuesta. |
| `stop()`                  | Envía `quit`, espera, hace `wait` del hijo.                    |
| `status()`                | Devuelve `NotStarted` / `Ready` / `Running` / `Stopped` / `Error`. |
| `channelCount(nodeId)`    | Número de canales que el sumidero `nodeId` emite (`1` por defecto, `2` para `PhasePortrait`). |
| `buffer(nodeId, channel)` | Snapshot del *ring buffer* (`BUFFER_SIZE = 512`) del sumidero, por canal. |
| `exportSod(path)`         | Escribe la corrida completa (vector de tiempo + cada canal de cada sumidero) a archivo `.sod` HDF5 nativo de Scilab. Encolado cuando el hilo del solver está activo; inmediato en modo síncrono. |

## El subproceso

`reset()` ejecuta `scilab-cli` con las banderas
`-nb -nwni -noatomsautoload`:

- `-nb` — sin banner.
- `-nwni` — modo terminal sin gráficos.
- `-noatomsautoload` — sin carga automática de *toolbox*, para
  arrancar rápido.

La búsqueda del binario prueba varias rutas comunes antes de caer
al `PATH`:

```
/opt/scilab-2026.0.1/bin/scilab-cli
/opt/scilab/bin/scilab-cli
/usr/bin/scilab-cli
/usr/local/bin/scilab-cli
scilab-cli           (delegado al PATH)
```

Si ninguna ruta funciona, `reset()` devuelve `false` y el bridge
entra en `Status::Error` con `lastError()` poblado. El editor
puede seguir editando el grafo; la UI muestra un *banner* con el
mensaje.

## Modelo de comunicación

**Línea-a-línea.** El *stdout* de `scilab-cli` queda
*line-buffered* cuando se *pipea* (verificado en Scilab 2026.0.1).
Cada paso del solver:

1. Escribe una línea al *stdin* del hijo pidiendo el siguiente paso.
2. Lee una línea del *stdout* del hijo (bloquea hasta que llegue).
3. Decodifica el vector de estado.
4. Reparte las muestras a los *ring buffers* de los sumideros, por
   canal (longitud `BUFFER_SIZE = 512`).

## Hilo dedicado del solver

El editor usa `startSolverThread(dt)` para entrar al modo normal de
ejecución. El hilo corre en un *loop*:

1. Pide un paso a Scilab y lo procesa (pasos 1–4 de arriba).
2. Calcula cuánto le queda al *tick* objetivo con
   `std::chrono::steady_clock` y duerme la diferencia.

Esto deja al solver corriendo a su cadencia independientemente de
la UI: el *frame loop* del editor puede usar el tiempo libre para
redibujar los plots, atender al usuario y responder a eventos sin
bloquear la simulación. La comunicación entre el hilo del solver
(productor de muestras) y el *frame loop* (consumidor que
renderiza los plots) usa buffers SPSC por canal: un escritor
único (el hilo) y un lector único (la UI), sin necesidad de
*mutex* en el camino caliente.

`sendParameter` mantiene su semántica *fire-and-forget*: se
escribe a *stdin* en cualquier momento desde la UI, y el hilo del
solver recoge el cambio en el siguiente paso.

## *Live tuning*

`sendParameter(nodeId, paramIdx, value)` escribe al *stdin* del
hijo una asignación Scilab que reemplaza el valor del parámetro
para los siguientes pasos. Es *fire-and-forget*: no espera respuesta
porque el siguiente `step(dt)` recogerá el cambio. El usuario ve el
efecto en <1/60 s.

## Por qué subproceso y no embebido

Tres razones prácticas:

1. **Aislamiento.** Un *crash* de Scilab no tumba al editor.
2. **Licencia.** `scilab-cli` es CeCILL — el editor no se enlaza
   contra Scilab.
3. **Portabilidad.** El usuario instala Scilab del paquete del
   sistema; el editor no embebe nada.

Una alternativa con `call_scilab` (Scilab in-process) se exploraría
en versiones posteriores como segundo *backend* opcional.
