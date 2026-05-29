# Correr una simulación

Pulsa **`Run`** en la barra de estado.  El editor:

1. Genera el script Scilab a partir del grafo.
2. Lanza `scilab-cli` como subproceso (`ScilabBridge`).
3. Inicia el hilo dedicado del solver con `dt = 1/60 s`.
4. Lee las muestras de los sumideros y las dibuja en `Plots`
   en tiempo real.

## Pause / Resume / Stop / Reset

- **`Pause`** — el solver se detiene; el grafo y los plots
  permanecen visibles.
- **`Resume`** — continúa desde donde quedó.
- **`Stop`** — termina el subproceso de Scilab; los plots
  conservan los datos.
- **`Reset`** — para y limpia.

## Live tuning

Mientras la simulación corre, puedes arrastrar el
`DragFloat` de cualquier parámetro y el cambio se aplica en
el siguiente *tick* del solver, sin reiniciar la corrida
desde \\(t=0\\).
