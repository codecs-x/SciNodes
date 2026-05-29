# Examples Browser

*File → Browse Examples* abre una biblioteca de grafos
listos para cargar.  Cada *example* trae el `.scn` + una
descripción y *tags*.

## Cargar un example

Click sobre el example → **Load**.  Si tu grafo actual
tiene cambios sin guardar, el editor pregunta antes de
descartarlos.

## Import como template

**Import** crea un nodo *grouped* a partir del example y lo
inserta en tu grafo actual.  Útil para reusar un sub-grafo
(p. ej. el controlador PID anti-windup) en distintos
diseños.

## Save as Example

*File → Save as Example* guarda tu grafo actual en la
biblioteca local.  Aparece en el browser al lado de los
ejemplos *built-in*.

## "Sobre este grafo"

El panel `Sobre este grafo` (`Edit → About this Graph`)
expone los metadatos del documento: título, autor,
descripción larga, *tags*.  Los metadatos se serializan en
el `.scn`.
