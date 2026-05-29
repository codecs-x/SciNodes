# Exportar datos

Dos rutas para sacar trayectorias del editor.

## CSV por sumidero

Cada sumidero del `PlotPanel` tiene un botón **Export CSV**.
Al pulsarlo se abre el *file picker* y el editor escribe
una columna `t` + una columna por canal del sumidero.

```
t,y
0.000000,0.000000
0.020000,0.125333
0.040000,0.248690
...
```

Compatible con LibreOffice Calc, Excel, `pandas.read_csv`.

## `.sod` (HDF5 nativo de Scilab)

Para llevar todos los sumideros al mismo tiempo a Scilab,
usá *File → Export simulation data → .sod*.  El archivo es
HDF5; cada sumidero es una variable nombrada por la
sanitización de su título visible
(`Oscilloscope #3 → Oscilloscope_3`).

```scilab
load("mi_simulacion.sod");
plot(Oscilloscope_3(:,1), Oscilloscope_3(:,2));
```

## Cuándo usar cuál

- **CSV** — mirar un solo sumidero en Calc, Excel,
  `pandas`.
- **`.sod`** — análisis multi-sumidero en una sesión de
  Scilab.
