# Codegen multifísico

Los bloques PMSM / térmicos / structural se integran al mismo
`ScilabCodeGen` que los nodos clásicos.  Cada uno aporta
una expresión Scilab (o un cuerpo de `dynamics(t, x)` si es
*stateful*).

## PMSM

`PMSMSizing` emite ecuaciones cerradas:
\\(T = K_t I, V = K_e \omega + L \frac{dI}{dt} + R I, \dots\\)
con factores empíricos del archivo de calibración si
existe.

## Red térmica

`ThermalMass` es estado puro: una capacitancia con `dT/dt =
(P_in - q_out) / (m c_p)`.  `ThermalResistance` es
*stateless*: `q = (T_h - T_c) / R`.  Conectar nodos via
`ThermalResistance` arma una red RC arbitrariamente
compleja; el solver la integra con `ode()` igual que
cualquier sistema lineal.

## Heatmap y Distribution sinks

`HeatmapRenderer` y `HistogramRenderer` viven en
`src/ui/plots/`.  El primero acepta una tupla `(x, y, v)`
por *tick*; el segundo acumula en *bins* y dibuja la
distribución cuando la corrida termina.

## Per-node Import / Export CSV

`PlotPanel` y el panel de parámetros ofrecen *Import / Export
CSV* por nodo, no solo por sumidero.  Útil para guardar la
configuración paramétrica de un `PMSMSizing` y reusarla en
otro `.scn`.
