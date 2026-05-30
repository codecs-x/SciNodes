#pragma once
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// IComputeBackend — contrato mínimo para un motor numérico que SciNodes pueda
// usar como solucionador de ODEs detrás del bridge.
//
// El propósito de esta abstracción es permitir que la implementación concreta
// (subproceso scilab-cli, embebido vía call_scilab, o incluso un solver
// propio) sea intercambiable sin tocar el resto del código.
//
// Notas de diseño:
//   - El contrato NO depende de NodeGraph. El bridge (ScilabBridge u otro) es
//     quien traduce un grafo a un BackendPrepareSpec usando ScilabCodeGen.
//     Esto separa "autoría" (NodeGraph) de "ejecución" (BackendSpec).
//   - El backend NO mantiene ring buffers ni hilos: solo expone step() y los
//     valores instantáneos de los sumideros. La política de almacenamiento
//     y la concurrencia viven una capa más arriba.
//   - El backend reporta su propio Status y un mensaje de error textual.
//     Eso permite que la UI muestre un banner sin importar qué backend está
//     activo.
// -----------------------------------------------------------------------------
namespace scinodes {

// Especificación con la que un grafo, una vez compilado, queda listo para
// que un backend numérico lo ejecute.
struct BackendPrepareSpec {
    // Definición de la función Scilab `dynamics(t, x)` que el backend integra.
    // Cuerpo completo, incluyendo `function ... endfunction`. Si el grafo no
    // tiene estado integrado, puede estar vacía: en ese caso step() solo
    // evalúa las expresiones de los sumideros.
    std::string dynamicsFunction;

    // Dimensión del vector de estado x. Igual al número total de slots
    // asignados a bloques con estado.
    int stateSize = 0;

    // Vector inicial de estado. Si está vacío, se asume cero.
    std::vector<double> initialState;

    // Snippet de Scilab que se ejecuta DESPUÉS de ode() en cada step() para
    // recomputar todas las variables intermedias (`v<id>_<port>` para cada
    // nodo del grafo).  Permite que las expresiones de los sumideros sean
    // tan simples como leer una variable ya calculada.
    //
    // Si el grafo no tiene estado o no se requiere recálculo, puede quedar
    // vacío: en ese caso las expresiones de los sumideros se evalúan
    // directamente.
    std::string outputEvalScript;

    // Función almacenada `scn_step(t_new, t_prev, x_in)` que envuelve el
    // ciclo completo de un tick: ode + outputEvalScript + empaquetado del
    // vector y de sumideros.  Cuerpo completo incluyendo `function ...
    // endfunction`.  Backends in-process (ScilabCallApiBackend) la definen
    // UNA vez en prepare() y la invocan en cada step(), evitando que Scilab
    // re-parsee outputEvalScript por tick — clave para escalar a grafos
    // con muchos nodos.  Si el grafo está vacío o el backend no la usa,
    // este campo puede quedar vacío.
    std::string stepFunction;

    // Canal de un sumidero: una expresión Scilab que, evaluada después de
    // outputEvalScript, produce el valor a publicar al consumidor.
    // Típicamente es "v<id>_<channel>" si el codegen llenó las variables;
    // en specs hechas a mano puede ser cualquier expresión válida.
    struct SinkChannel {
        int         nodeId;     // id del nodo sumidero
        int         channel;    // índice del canal (0 para single-channel)
        std::string expression; // p.ej. "v17_0" o "x(3)"
    };
    std::vector<SinkChannel> sinkChannels;

    // Slot de parámetro vivo. El backend debe poder reasignar el valor en
    // cualquier momento (durante o entre pasos) y reflejarlo en el siguiente
    // step().
    struct ParamSlot {
        int         nodeId;       // id del nodo dueño del parámetro
        int         paramIdx;     // índice del parámetro dentro del nodo
        std::string scilabName;   // nombre de la variable Scilab que lo aloja
        double      initialValue;
    };
    std::vector<ParamSlot> params;
};

// Muestra publicada por un step(): valor instantáneo de un canal de sumidero.
struct SinkSample {
    int    nodeId;
    int    channel;
    double value;
};

class IComputeBackend {
public:
    enum class Status {
        NotStarted,
        Ready,
        Running,
        Stopped,
        Error
    };

    virtual ~IComputeBackend() = default;

    // Inicializa el backend con la especificación dada. Si retorna false,
    // status() == Error y lastError() explica por qué.
    //
    // Idempotente: una segunda llamada limpia el estado previo y carga la
    // nueva spec.
    virtual bool prepare(const BackendPrepareSpec& spec) = 0;

    // Avanza la simulación dt segundos. Llena `outSamples` con un valor por
    // cada SinkChannel declarado en la spec, en el mismo orden.
    //
    // Si algún canal produce NaN/Inf, *outOffendingNodeId queda con el id
    // del nodo culpable (el primero en orden topológico) y la función puede
    // retornar true igualmente — el llamador decide qué hacer.
    virtual bool step(double dt,
                      std::vector<SinkSample>& outSamples,
                      int* outOffendingNodeId) = 0;

    // Actualiza un parámetro vivo. El backend lo aplica en (o antes de) el
    // próximo step(). Devuelve false si el slot no existe.
    virtual bool setParameter(int nodeId, int paramIdx, double value) = 0;

    // Exporta el historial acumulado a un archivo. El formato lo decide la
    // implementación (subprocess emite .sod de Scilab; call_scilab podría
    // emitir CSV en su lugar). El resultado textual va a *result.
    //
    // Implementaciones que no soportan export devuelven false con un mensaje
    // explicativo en *result.
    virtual bool exportHistory(const std::string& path,
                               std::string*       result) = 0;

    // Termina el backend de forma limpia. Idempotente.
    virtual void shutdown() = 0;

    virtual Status      status()    const = 0;
    virtual std::string lastError() const = 0;
};

}  // namespace scinodes
