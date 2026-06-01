# SciNodes — Documentación técnica

Este sitio es la documentación del editor de nodos en tiempo
real para Scilab.

## Audiencias

| Capa | Quién la lee | Qué encuentra |
|------|--------------|---------------|
| Usar SciNodes | Usuario que abre el programa para armar y correr un grafo | El **cómo se usa**: gestos del canvas, catálogo de nodos, parámetros, plots, export. No requiere C++. |
| Arquitectura interna | Desarrollador que va a modificar el editor o registrar tipos nuevos | El **cómo está hecho**: estructura de NodeGraph, codegen Scilab, backends, formato `.scn`, registro de nodos custom. Asume C++20 + ImGui. |

## Cómo navegar este sitio

El sumario de la izquierda separa el material en las dos capas
de arriba. Si recién llegás al programa, empezá por
**Usar SciNodes → Introducción**. Si vas a tocar el código,
saltá directo a **Arquitectura interna → Estructura del repo**.

## Cómo está hecho este sitio

Las páginas son Markdown editables en `doc/manual/src/`. mdBook
las compila a HTML estático y GitHub Pages las publica. Para
editar una página, hacé click en el icono ✏️ que aparece arriba
a la derecha — te lleva directo al archivo correspondiente en
GitHub.

## Versión

Esta documentación evoluciona junto con el código: cada *tag*
del repo acompaña su correspondiente snapshot del manual.
Para ver qué cambió en cada versión —y cuál es la versión
actual de SciNodes— mirá el [CHANGELOG] del repo o la barra
de título del editor (`SciNodes vX.Y.Z`).

[CHANGELOG]: https://github.com/codecs-x/SciNodes/blob/main/CHANGELOG.md
