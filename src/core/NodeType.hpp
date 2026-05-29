#pragma once
#include "Unit.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <unordered_map>

// -----------------------------------------------------------------------
// NodeType — one enum value per node type in the grammar
// -----------------------------------------------------------------------
enum class NodeType {
    // Sources (grammar terminal-left)
    VoltageSource,
    CurrentSource,
    StepSignal,
    SineSignal,
    RampSignal,
    DesignTemplate,        // Phase 2 — design requirements bundle (v0.8)

    // Transformers (grammar non-terminal)
    Gain,
    Summation,
    Integrator,
    Differentiator,
    LowPassFilter,
    PIDController,
    TransferFunction,
    TransferFunction2,
    Saturation,
    DCMotorModel,
    GearTransmission,
    InverseKinematics,
    PMSMSizing,            // Phase 2 — classical sizing equation (v0.8)
    IPMSizing,             // Phase 2 — IPM with reluctance-torque boost (v0.8)
    BLDCSizing,            // Phase 2 — BLDC with trapezoidal factor (v0.8)
    PMSMElectromagnetic,   // Phase 2 — Ke, Ld=Lq, V_rms, T_cog (v0.8)
    AirgapFluxDensity,     // Phase 2 — B_g(t) waveform at stator point (v0.8)
    PMSMEfficiency,        // Phase 2 — η from (T, ω, Ke) + loss params (v0.8)

    // Stage v0.9 — Thermal Network: losses + lumped RC nodes.
    JouleLoss,             // Phase 1 — copper loss from (T, Ke)         (v0.9)
    CoreLoss,              // Phase 1 — iron loss from (ω, B_g)          (v0.9)
    MechanicalLoss,        // Phase 1 — friction + windage from ω        (v0.9)
    ThermalMass,           // Phase 1 — single-node RC thermal           (v0.9)
    ThermalNode,           // Phase 3 — pure capacitance, 4 heat inputs  (v0.9)
    ThermalResistance,     // Phase 3 — dual-output q_HtoC / q_CtoH      (v0.9)
    CoolingSystem,         // Phase 4 — fan / water / ambient knobs      (v0.9)
    ConvectiveCooling,     // Phase 4 — h(flow)·ΔT cooling block         (v0.9)

    // Stage v1.0 — Structural & NVH.
    MaxwellForce,          // Phase 1 — radial Maxwell pressure σ=B²/2μ₀ (v1.0)
    ModalFrequency,        // Phase 1 — thin-ring mode-m natural freq    (v1.0)
    TolerancePerturbator,  // Phase 3 — uniform-random noise within ±h   (v1.0)

    // Sub-lenguaje Geometry — los tres nodos que materializan el grafo
    // de escena 3D (ver `doc/3d_scene_graph_design.md`).  Object3D es un
    // Source en Geometry, TransformObject es el bridge bilingüe
    // Signal↔Geometry, SceneOutput es el Sink que el render colecciona.
    Object3D,
    TransformObject,
    SceneOutput,

    // Sub-lenguaje Vec3 (etapas 4–5 del upgrade gramatical, ver
    // `doc/grammar_typesystem_upgrade.md`).  vec(3) como TypeExpr —
    // estos nodos materializan operaciones del álgebra lineal entre
    // escalares y vectores.  Renderizan: Vec3Constant define un vec(3)
    // editable; CombineXYZ empaqueta 3 escalares en 1 vec(3);
    // SeparateXYZ los desempaqueta; VectorAdd/Sub/Scale/Dot/Cross/
    // Length/Normalize componen operaciones del álgebra lineal.  Costo
    // cero para el codegen Scilab: emiten "0.0" como placeholder (el
    // sub-lenguaje vec3 vive render-side, no en el solver).
    Vec3Constant,
    CombineXYZ,
    SeparateXYZ,
    VectorAdd,
    VectorSub,
    VectorScale,
    VectorDot,
    VectorCross,
    VectorLength,
    VectorNormalize,

    // Conversores explícitos de unidades (etapa 6H del análisis
    // dimensional).  Declaran input/output con dimensiones DISTINTAS
    // — el nodo es la herramienta para cruzar entre convenciones que
    // SciNodes trata como distintas (rad vs deg, futuros V→mV, etc.).
    // Emiten Scilab real (multiplicación por factor constante), a
    // diferencia de Vec3*/Math*Vec* que viven render-side.
    DegToRad,
    RadToDeg,

    // Sinks (grammar terminal-right)
    Oscilloscope,
    FFTAnalyzer,
    PhasePortrait,
    DataLogger,
    TerminalDisplay,
    View3DSink,
    View3DThermalSink,     // Stage v0.9 — tints the procedural mesh
    View3DDeformationSink, // Stage v1.0 — animated mode-shape overlay
    HeatmapSink,           // Phase 2 — 2-D heatmap (x, y, value) (v0.8)
    DistributionSink,      // Stage v1.0 — histogram of accumulated samples

    // SuperBlock — recursive grouping ("paréntesis" en la gramática).
    // Un `SubGraph` agrupa otros nodos en un grafo hijo; los puertos
    // visibles desde fuera se materializan en su interior con los
    // stubs `SubGraphInput` (signal entering) y `SubGraphOutput`
    // (signal leaving).  El codegen aplana cada SubGraph antes de
    // emitir Scilab, así la simulación es indistinguible de la
    // versión sin agrupación.
    SubGraph,
    SubGraphInput,    // dentro del SubGraph: 0 inputs, 1 output (= la señal del puerto i del padre)
    SubGraphOutput,   // dentro del SubGraph: 1 input,  0 outputs (= la señal hacia el puerto j del padre)

    // Sentinel for JSON-loaded node types — see CustomNodeRegistry.
    // The actual descriptor lives in NodeInstance::customType.
    Custom,
};

// Device se comporta como Transformer en las reglas gramaticales (puede
// recibir y emitir señales en el medio de una cadena), pero está
// diferenciado para que el resto del sistema (UI, asset binding,
// outliner) pueda tratarlo como "dispositivo físico con geometría
// asignable".  Ver doc/geometry-contracts-design.md.
enum class NodeCategory { Source, Transformer, Device, Sink };

// -----------------------------------------------------------------------
// TypeExpr — gramática unificada del tipo de cada puerto.  Define la
// familia de tipos sobre la que R6 (validateEdge) decide compatibilidad.
// Ver `doc/grammar_typesystem_upgrade.md`.
//
// Constructores:
//   TensorType(dims=())     escalar
//   TensorType(dims=(3,))   vec3
//   TensorType(dims=(4,4))  mat4
//   GeometryType{}          mesh + parts (estructura compuesta)
//
// Crecer el dominio (matrices, quaternions, color, ...) NO toca R6 ni
// el switch que enumera casos — sólo agrega alternatives a TypeExpr.
// -----------------------------------------------------------------------
struct TensorType {
    enum class Element { F64 };

    std::vector<int> dims;                   // dims.empty() = escalar
    Element          element = Element::F64;

    bool isScalar() const { return dims.empty(); }
    bool operator==(const TensorType& o) const {
        return element == o.element && dims == o.dims;
    }
    bool operator!=(const TensorType& o) const { return !(*this == o); }
};

struct GeometryType {
    // Singleton por ahora — todos los GeometryType son estructuralmente
    // compatibles entre sí.  Si en el futuro hace falta distinguir
    // (p. ej. PointCloud vs Mesh vs Curve), agregar campos discriminantes
    // y refinar operator==.
    bool operator==(const GeometryType&) const { return true; }
    bool operator!=(const GeometryType&) const { return false; }
};

using TypeExpr = std::variant<TensorType, GeometryType>;

// Constructores convenientes — el código del registry y los tests los
// usa en vez de instanciar TensorType{...} directamente.
inline TypeExpr exprScalar() {
    return TensorType{};                     // dims vacío, element F64
}
inline TypeExpr exprVec(int n) {
    TensorType t; t.dims = { n }; return t;
}
inline TypeExpr exprMat(int rows, int cols) {
    TensorType t; t.dims = { rows, cols }; return t;
}
inline TypeExpr exprGeometry() {
    return GeometryType{};
}

// Predicados de discriminación — para call-sites que pre-existían y
// preguntaban "este puerto es Geometry o Signal".
inline bool isGeometryType(const TypeExpr& t) {
    return std::holds_alternative<GeometryType>(t);
}
inline bool isScalarType(const TypeExpr& t) {
    if (auto* tt = std::get_if<TensorType>(&t)) return tt->isScalar();
    return false;
}

// R6 unificada — esta es la regla, no un switch.  Dos TypeExpr son
// compatibles sii son del mismo constructor (Tensor↔Tensor o
// Geometry↔Geometry) y sus parámetros internos coinciden (forma del
// tensor, o ambos GeometryType).  Crecer el dominio no toca esta
// función.
bool typeMatches(const TypeExpr& a, const TypeExpr& b);

// Descripción legible — usada por los mensajes de error de R6
// ("expected scalar but got vec(3)") y los tooltips de la UI.
//
// Convenciones de display:
//   TensorType()         → "scalar"
//   TensorType(3,)       → "vec(3)"
//   TensorType(4,4)      → "mat(4,4)"
//   TensorType(2,3,4)    → "tensor(2,3,4)"
//   GeometryType         → "geometry"
std::string describeType(const TypeExpr& t);

// Color del pin en el canvas — DERIVADO de la forma, no hardcoded por
// case.  Devuelve un IM_COL32 (RGBA empaquetado en uint32) que la UI
// consume sin saber del enum subyacente.
//
//   scalar     → azul   (120,200,250)
//   vec(N)     → violeta (180,130,220)
//   mat(R,C)   → naranja (220,160, 60)
//   geometry   → cyan   ( 80,200,200)
//   tensor>2D  → magenta (220,120,200)  (fallback — no se usa todavía)
unsigned int pinColorFromType(const TypeExpr& t);

struct ParamDef {
    std::string name;
    double      defaultValue;
    std::string unit;       // display string (no enforcement, sólo UI).
                            // R7 chequea unidades a nivel PUERTO via
                            // NodeDef::inputPortUnits/outputPortUnits,
                            // no a nivel param.
};

struct NodeDef {
    NodeType     type;
    NodeCategory category;
    std::string  label;          // display name
    std::string  description;    // tooltip text
    int          inputPorts;     // 0 for Sources
    int          outputPorts;    // 0 for Sinks
    std::vector<ParamDef> params;

    // Semántica de estado para el solver — centraliza lo que antes vivía
    // como switches sobre NodeType en ScilabCodeGen.cpp:
    //
    //   stateWidth  : número de slots reservados en el vector `x` de la
    //                 dinámica.  0 significa "stateless" (algebraic).
    //   isPureState : la salida del nodo ES x(slot), sin feedthrough
    //                 algebraico desde la entrada del mismo paso — eso
    //                 le permite romper ciclos algebraicos en feedback
    //                 loops.  Implica stateWidth > 0.  PIDController y
    //                 Differentiator tienen estado PERO feedthrough
    //                 directo, así que no son pure-state.
    //   stateOnlyPorts : puertos cuya señal afecta SOLO la derivada del
    //                 estado, no la salida instantánea (p. ej. anti-windup
    //                 back-calculation del PID en port 1).  Permiten
    //                 cerrar ciclos PID → Saturation → PID:port1 sin
    //                 generar bucle algebraico.
    //
    // CustomNodeRegistry rellena estos campos en su descriptor sintetizado
    // — por defecto custom nodes son stateless (stateWidth = 0).
    int                  stateWidth     = 0;
    bool                 isPureState    = false;
    std::vector<int>     stateOnlyPorts;

    // Tipos de cada puerto como expresiones de tipo (TypeExpr).  Si
    // los vectores están vacíos (default), todos los puertos del nodo
    // son `exprScalar()`.  Los nodos del sub-lenguaje Geometry los
    // rellenan explícitamente con `exprGeometry()`.  Tamaños esperados
    // cuando se rellenan: inputPortTypes.size() == inputPorts y
    // outputPortTypes.size() == outputPorts.  Helpers
    // `inputPortTypeOf` / `outputPortTypeOf` ocultan el default y
    // devuelven `exprScalar()` cuando el vector está vacío o el índice
    // queda fuera de rango.
    std::vector<TypeExpr> inputPortTypes;
    std::vector<TypeExpr> outputPortTypes;

    // Etiquetas declarativas por puerto.  Si el vector trae N entradas
    // y la i-ésima no es vacía, el renderer la muestra en lugar de
    // "in <N+1>".  Per-instance, `stringParams["portLabel<i>"]` pisa
    // este default (la UI del Oscilloscope edita esa clave por
    // canal).  Vacío = fallback "in <N>".
    std::vector<std::string> inputPortLabels;

    // Idem para los outputs.  SeparateXYZ los usa para etiquetar
    // "x"/"y"/"z" en sus tres outputs.  Vacío = fallback "out <N>".
    std::vector<std::string> outputPortLabels;

    // Unidades físicas declaradas por puerto (etapa 6D del análisis
    // dimensional).  Si los vectores están vacíos (default), el nodo
    // es POLIMÓRFICO — su unidad se infiere por propagación desde el
    // contexto (forward/backward).  Cuando se rellenan, R7 los usa
    // como gold-standard contra el que la propagación se confronta
    // y los edges se aceptan/rechazan.
    //
    // Tamaños esperados cuando se rellenan: inputPortUnits.size() ==
    // inputPorts y outputPortUnits.size() == outputPorts.
    //
    // Categorías:
    //   - Dimensionado (VoltageSource, DCMotor): los rellena con
    //     `scinodes::units::k*`.
    //   - Polimórfico (Gain, Sum, Step, Sine, Integrator): vacíos.
    //   - Unit-transformer (DegToRad, RadToDeg futuros): los rellena
    //     con dimensiones distintas en input y output.
    std::vector<scinodes::Unit> inputPortUnits;
    std::vector<scinodes::Unit> outputPortUnits;

    // Unit-transformer kind (etapa 6I.O).  Declara la relación
    // dimensional entre input y output del nodo en términos del
    // DOMINIO del grafo (NodeGraph::domainUnit) — no de una unidad
    // hardcoded.  Esto refleja la observación del usuario: integrar
    // multiplica por la unidad del dominio (s en time-domain, Hz en
    // frequency, m en espacio), no siempre por "tiempo".
    //
    //   None            → out unit = in unit (identidad polimórfica)
    //   MultiplyDomain  → out = in × domainUnit      (Integrator)
    //   DivideDomain    → out = in / domainUnit      (Differentiator)
    //
    // Asume nodos con 1 input y 1 output.  El analyzer combina este
    // kind con graph.domainUnit() para obtener el factor real al
    // tiempo de análisis.  Cuando el kind es distinto de None, el
    // nodo NO se considera polimórfico — sus puertos NO unifican,
    // obedecen la transformación.
    enum class UnitTransformKind {
        None             = 0,
        MultiplyDomain   = 1,
        DivideDomain     = 2,
    };
    UnitTransformKind unitTransformKind = UnitTransformKind::None;
};

// Returns the full registry of all node definitions (indexed by NodeType).
const std::unordered_map<NodeType, NodeDef>& nodeRegistry();

// Convenience helpers
NodeCategory categoryOf(NodeType t);
const char*  labelOf(NodeType t);

// "Pure-state" — la salida del nodo es la variable de estado integrada,
// SIN feedthrough algebraico desde la entrada del mismo paso.  Estos
// nodos rompen lazos algebraicos: el ciclo cierra a través de la
// integración, no instantáneamente.  Lo usa ScilabCodeGen::topoSort
// para permitir feedback loops y Canvas::autoLayout para asignar
// niveles de profundidad a grafos con realimentación.
//
// Wrapper sobre NodeDef::isPureState — lookup en el registry.  Se
// mantiene la forma libre por compatibilidad con call sites que sólo
// tienen el NodeType (no la NodeInstance completa).
bool isPureStateNode(NodeType t);

// Predicados de SubGraph — reemplazan las cadenas
// `t == NodeType::SubGraphInput || t == NodeType::SubGraphOutput` y
// `t == NodeType::SubGraph` repetidas en 32 sitios del código.  El
// nombre semántico es más legible y centraliza la definición.
constexpr bool isSubGraphStub(NodeType t) {
    return t == NodeType::SubGraphInput || t == NodeType::SubGraphOutput;
}
constexpr bool isSubGraphContainer(NodeType t) {
    return t == NodeType::SubGraph;
}

// Stable enum-name conversion for serialization (e.g. "VoltageSource").
// Distinct from labelOf() which returns the human display label ("Voltage Source").
const char*               typeName(NodeType t);
std::optional<NodeType>   typeFromName(const std::string& name);

// Resuelve el tipo de un puerto del NodeDef.  Si `inputPortTypes` /
// `outputPortTypes` está vacío (default) o el índice queda fuera de
// rango, devuelve `exprScalar()`.  Centraliza el default — todo el
// código que pregunta "¿qué tipo tiene este puerto?" pasa por aquí.
TypeExpr inputPortTypeOf(const NodeDef& def, int portIdx);
TypeExpr outputPortTypeOf(const NodeDef& def, int portIdx);

// Lookup por NodeType — combina nodeRegistry().at(t) con los helpers
// de arriba.  Devuelven `exprScalar()` silenciosamente si el tipo no
// está registrado (p. ej. Custom sin descriptor).
TypeExpr inputPortTypeOf(NodeType t, int portIdx);
TypeExpr outputPortTypeOf(NodeType t, int portIdx);

// ¿Tiene este puerto una unidad DECLARADA?  False sii el vector está
// vacío o el índice queda fuera de rango — el puerto se considera
// polimórfico (la propagación lo resolverá desde el contexto).
bool hasDeclaredInputUnit(const NodeDef& def, int portIdx);
bool hasDeclaredOutputUnit(const NodeDef& def, int portIdx);

// Devuelve la unidad declarada del puerto.  Si no hay declaración,
// devuelve `Unit{}` (adimensional) — el caller debe usar
// `hasDeclaredXxxUnit` primero para distinguir "polimórfico" de
// "declarado adimensional".
scinodes::Unit inputPortUnitOf(const NodeDef& def, int portIdx);
scinodes::Unit outputPortUnitOf(const NodeDef& def, int portIdx);

// ¿El nodo pertenece al sub-lenguaje Geometry?  True sii al menos un
// puerto (input u output) tiene `GeometryType` declarado.  Object3D /
// TransformObject / SceneOutput dan true; el resto, false.  El codegen
// Scilab lo usa para SALTAR estos nodos en silencio en vez de reportar
// "not yet supported" — son visuales, no parte del solver.
bool isSceneGraphNode(NodeType t);
