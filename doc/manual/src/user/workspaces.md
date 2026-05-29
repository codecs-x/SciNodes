# Workspaces tipo Blender

Una barra de tabs sobre el viewport conmuta entre tres
*workspaces* pre-configurados.  Cada uno arregla los paneles
para una etapa de trabajo distinta.

| Workspace        | Layout                                                  |
|------------------|---------------------------------------------------------|
| **Design**       | `Node Editor` maximizado; 3D/Plots como pestañas        |
| **Simulation 2D**| `Node Editor` arriba; Plots y 3D apilados abajo          |
| **Simulation 3D**| Node Editor centro; 3D arriba der; Plots abajo der       |

Default al arranque: **Simulation 3D**.

## Conmutar

- **Click en el tab** del workspace deseado.  El layout se
  reconstruye en el siguiente *frame*.
- **View → Reset Layout** restaura el preset del workspace
  activo.

Cada workspace recuerda su propia disposición; si moviste
paneles dentro de *Design*, los encontrás así cuando volvés
al tab.
