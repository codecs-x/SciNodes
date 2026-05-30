#pragma once

// -----------------------------------------------------------------------------
// SimTypes — enums del dominio del controlador de simulación.
//
// Antes vivían dentro de `src/ui/StatusBar.hpp`, lo que forzaba a
// `SimController` (en `src/app/`) a incluir un header de UI sólo para
// referenciar dos enums.  Eso filtraba ImGui transitivamente al
// controlador y rompía la regla de capas (`app/` no debe depender de
// `ui/` para tipos del dominio).
//
// Aquí viven los enums puros, sin dependencias.  Tanto `SimController`
// como `StatusBar` los consumen de aquí; `StatusBar.hpp` re-incluye
// este header para que código UI legado (que esperaba `SimState` desde
// StatusBar) siga compilando sin cambios.
// -----------------------------------------------------------------------------

// Estado actual de la sesión de simulación.  La máquina vive en
// SimController; la UI lo refleja en el StatusBar.
enum class SimState {
    Idle,        // no corriendo (grammar puede ser válida o no)
    Simulating,  // bridge avanzando step por step
    Paused,      // bridge vivo, frame loop no llama step()
    Error        // bridge falló o generador rechazó el grafo
};

// Acción que pidió el usuario en este frame (lo devuelve el StatusBar
// al SimController vía dispatch).
enum class SimAction {
    None,
    Run,         // Idle  → Simulating  (regenera driver, arranca bridge)
    Pause,       // Simulating → Paused
    Resume,      // Paused → Simulating
    Stop,        // Simulating/Paused → Idle (mata bridge)
    Reset        // cualquiera → Idle (mata bridge, tiempo a 0)
};
