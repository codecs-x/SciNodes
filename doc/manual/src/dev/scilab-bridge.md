# Puente con Scilab

`ScilabBridge` (en `src/core/`) abstrae el subproceso `scilab-cli`.
La UI nunca habla directo con Scilab — siempre vía el puente.

## Ciclo de vida

| Método              | Qué hace                                                       |
|---------------------|-----------------------------------------------------------------|
| `reset(graph)`      | Mata el hijo anterior (si existe), genera el *driver* Scilab a partir del grafo, lanza `scilab-cli` y deja al hijo listo en `Ready`. |
| `step(dt)`          | Avanza la simulación `dt` segundos: pide un paso, lee el vector de estado por *stdout*, actualiza los buffers de cada sumidero. |
| `sendParameter(...)`| *Live tuning*: reasigna el valor de un parámetro en caliente. *Fire-and-forget* — no espera respuesta. |
| `stop()`            | Envía `quit`, espera, hace `wait` del hijo.                    |
| `status()`          | Devuelve `NotStarted` / `Ready` / `Running` / `Stopped` / `Error`. |

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

**Línea-a-línea, bloqueante.** El *stdout* de `scilab-cli` queda
*line-buffered* cuando se *pipea* (verificado en Scilab 2026.0.1).
Cada `step(dt)`:

1. Escribe una línea al *stdin* del hijo pidiendo el siguiente paso.
2. Lee una línea del *stdout* del hijo (bloquea hasta que llegue).
3. Decodifica el vector de estado.
4. Reparte las muestras a los *ring buffers* de cada sumidero
   (longitud `BUFFER_SIZE = 512`).

No hay hilo dedicado del solver: la UI llama a `step(dt)` cada
*frame* desde el *frame loop* del `AppWindow`, y el ritmo de la
simulación queda atado al ritmo de la UI. Para grafos pequeños el
costo de un paso es del orden de los milisegundos --- suficiente
para mantener una tasa nominal de 60 Hz mientras Scilab avanza
~10 ms de tiempo simulado por *frame*.

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
