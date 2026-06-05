// =============================================================================
// test_canvas.cpp — Tests headless de Canvas::autoLayout.
//
// Sin ImNodes, sin ventana, sin SDL: el Canvas opera sobre un NodeGraph en
// memoria y deja posiciones consultables.  Si esto pasa, la abstracción
// cumple su promesa de ser testeable sin la librería de UI.
// =============================================================================

#include "core/NodeGraph.hpp"
#include "core/NodeType.hpp"
#include "ui/canvas/Canvas.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int g_pass = 0, g_fail = 0;

void expect_true(bool cond, const char* msg) {
    if (cond) { ++g_pass; }
    else { ++g_fail; std::fprintf(stderr, "[FAIL] %s\n", msg); }
}

void expect_lt(float a, float b, const char* msg) {
    if (a < b) { ++g_pass; }
    else {
        ++g_fail;
        std::fprintf(stderr,
            "[FAIL] %s — got %.2f < %.2f\n", msg, a, b);
    }
}

// Devuelve true si las bboxes de a y b se intersectan en cualquier eje.
bool overlaps(scinodes::ui::CanvasPos pa, scinodes::ui::CanvasDims da,
              scinodes::ui::CanvasPos pb, scinodes::ui::CanvasDims db) {
    const bool xOverlap = (pa.x < pb.x + db.w) && (pb.x < pa.x + da.w);
    const bool yOverlap = (pa.y < pb.y + db.h) && (pb.y < pa.y + da.h);
    return xOverlap && yOverlap;
}

}  // namespace

// -----------------------------------------------------------------------------
// E1-like: lazo cerrado con realimentación
//   Step → Sum(+,−) → PID → Integrator → TF2 → Scope
//                                            │
//                                            └→ Gain → Sum  (feedback)
// El Integrator (pure-state) ROMPE el ciclo; sin esa regla, todos los
// nodos del ciclo caerían en el mismo nivel.  Con la regla, layout
// produce columnas crecientes Step < Sum < PID < Integrator < TF2 < Scope,
// y Gain queda en alguna columna intermedia.
// -----------------------------------------------------------------------------
static void test_e1_feedback_loop() {
    NodeGraph g;
    int step = g.addNode(NodeType::StepSignal);
    int sum  = g.addNode(NodeType::Summation);
    int pid  = g.addNode(NodeType::PIDController);
    int integ= g.addNode(NodeType::Integrator);
    int tf2  = g.addNode(NodeType::TransferFunction2);
    int gain = g.addNode(NodeType::Gain);
    int sco  = g.addNode(NodeType::Oscilloscope);

    auto wire = [&](int srcId, int dstId, int dstPort) {
        const NodeInstance* sn = g.findNode(srcId);
        const NodeInstance* dn = g.findNode(dstId);
        return g.tryAddEdge(sn->outputAttrId(),
                            dn->inputAttrId(dstPort));
    };
    wire(step, sum,  0);
    wire(sum,  pid,  0);
    wire(pid,  integ,0);
    wire(integ,tf2,  0);
    wire(tf2,  sco,  0);
    wire(tf2,  gain, 0);
    wire(gain, sum,  1);

    scinodes::ui::Canvas canvas(g);
    canvas.autoLayout();

    auto pStep  = canvas.positionOf(step);
    auto pSum   = canvas.positionOf(sum);
    auto pPid   = canvas.positionOf(pid);
    auto pInteg = canvas.positionOf(integ);
    auto pTf2   = canvas.positionOf(tf2);
    auto pSco   = canvas.positionOf(sco);

    expect_lt(pStep.x, pSum.x,  "Step < Sum en X");
    expect_lt(pSum.x,  pPid.x,  "Sum  < PID en X");
    expect_lt(pPid.x,  pInteg.x,"PID  < Integ en X (pure-state rompe el lazo)");
    expect_lt(pInteg.x,pTf2.x,  "Integ < TF2 en X");
    expect_lt(pTf2.x,  pSco.x,  "TF2  < Scope en X");

    // No overlap: todos los pares de bboxes son disjuntos.
    const int ids[] = { step, sum, pid, integ, tf2, gain, sco };
    bool anyOverlap = false;
    for (size_t i = 0; i < std::size(ids); ++i)
        for (size_t j = i + 1; j < std::size(ids); ++j) {
            if (overlaps(canvas.positionOf(ids[i]),
                         canvas.dimensionsOf(ids[i]),
                         canvas.positionOf(ids[j]),
                         canvas.dimensionsOf(ids[j]))) {
                anyOverlap = true;
                std::fprintf(stderr,
                    "[FAIL] overlap entre nodes %d y %d\n", ids[i], ids[j]);
            }
        }
    expect_true(!anyOverlap, "Ningún par de nodos se solapa tras autoLayout");
}

// -----------------------------------------------------------------------------
// SubGraph stubs: SubGraphInput en columna 0, SubGraphOutput en última.
// -----------------------------------------------------------------------------
static void test_subgraph_stub_columns() {
    NodeGraph g;
    int sgId = g.addSubGraphNode();
    NodeGraph* child = g.subGraphOf(sgId);
    expect_true(child != nullptr, "SubGraph child existe");
    if (!child) return;

    // El addSubGraphNode ya crea un SubGraphInput y un SubGraphOutput
    // por defecto.  Añadimos un Gain en medio.
    int gain = child->addNode(NodeType::Gain);

    // Encontrar los stubs y cablearlos al Gain.
    int inStub = 0, outStub = 0;
    for (const auto& n : child->nodes()) {
        if (n.type == NodeType::SubGraphInput)  inStub  = n.id;
        if (n.type == NodeType::SubGraphOutput) outStub = n.id;
    }
    const NodeInstance* nIn   = child->findNode(inStub);
    const NodeInstance* nGain = child->findNode(gain);
    const NodeInstance* nOut  = child->findNode(outStub);
    child->tryAddEdge(nIn->outputAttrId(),   nGain->inputAttrId(0));
    child->tryAddEdge(nGain->outputAttrId(), nOut->inputAttrId(0));

    scinodes::ui::Canvas canvas(*child);
    canvas.autoLayout();

    auto pIn   = canvas.positionOf(inStub);
    auto pGain = canvas.positionOf(gain);
    auto pOut  = canvas.positionOf(outStub);
    expect_lt(pIn.x,   pGain.x, "SubGraphInput a la izquierda del cuerpo");
    expect_lt(pGain.x, pOut.x,  "SubGraphOutput a la derecha del cuerpo");
}

// -----------------------------------------------------------------------------
// Sub-canvas recursivo: si el grafo padre tiene un SubGraph, Canvas expone
// un Canvas hijo con el grafo correcto.
// -----------------------------------------------------------------------------
static void test_subcanvas_recursive() {
    NodeGraph g;
    int sgId = g.addSubGraphNode();
    NodeGraph* childGraph = g.subGraphOf(sgId);
    expect_true(childGraph != nullptr, "SubGraph instalado en parent");

    scinodes::ui::Canvas parent(g);
    scinodes::ui::Canvas* sub = parent.subCanvasOf(sgId);
    expect_true(sub != nullptr, "subCanvasOf devuelve canvas hijo");
    expect_true(&sub->graph() == childGraph,
                "el canvas hijo apunta al mismo NodeGraph del modelo");

    // Lookup repetido devuelve el mismo puntero (cacheado).
    scinodes::ui::Canvas* sub2 = parent.subCanvasOf(sgId);
    expect_true(sub == sub2, "subCanvasOf cachea la instancia");
}

// -----------------------------------------------------------------------------
// Regresión: el pase de layout físico (resortes) NO debe divergir en nodos
// de alto grado.  Un hub (StepSignal → N·Gain) tiene grado N; con la fuerza
// SUMADA (no promediada) el coeficiente de auto-realimentación del hub es
// 1 − kDamping·grado·0.5, que supera 1 en módulo para grado ≳ 13 → el Euler
// explícito diverge geométricamente (posiciones a 1e14, vista al zoom mínimo,
// nodos fuera de pantalla).  Era el bug real al agrupar muchos cables en un
// SubGraph container.  Con la fuerza promediada por grado, el paso queda
// acotado y el layout converge a coordenadas finitas y razonables.
// -----------------------------------------------------------------------------
static void test_high_degree_hub_no_divergence() {
    NodeGraph g;
    int hub = g.addNode(NodeType::StepSignal);
    constexpr int kFanOut = 20;   // bien por encima del umbral de inestabilidad
    std::vector<int> gains;
    for (int i = 0; i < kFanOut; ++i) {
        int gn = g.addNode(NodeType::Gain);
        gains.push_back(gn);
        const NodeInstance* sn = g.findNode(hub);
        const NodeInstance* dn = g.findNode(gn);
        g.tryAddEdge(sn->outputAttrId(), dn->inputAttrId(0));
    }

    scinodes::ui::Canvas canvas(g);
    canvas.autoLayout();

    bool allFinite = true;
    float maxAbsY = 0.f;
    const int hubAndGains = static_cast<int>(gains.size()) + 1;
    int checked = 0;
    auto check = [&](int id) {
        auto p = canvas.positionOf(id);
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) allFinite = false;
        maxAbsY = std::max(maxAbsY, std::fabs(p.y));
        ++checked;
    };
    check(hub);
    for (int gn : gains) check(gn);

    expect_true(checked == hubAndGains, "se verificaron hub + todos los gains");
    expect_true(allFinite, "todas las posiciones son finitas (no NaN/inf)");
    // Cota generosa: un layout sano de 21 nodos cabe MUY por debajo de esto;
    // el bug producía ~1e14.
    expect_lt(maxAbsY, 1.0e6f, "ningún nodo diverge en Y (hub de alto grado)");
}

int main() {
    std::fprintf(stderr,
        "================================================================\n"
        " test_canvas — Canvas::autoLayout headless\n"
        "================================================================\n");

    test_e1_feedback_loop();
    test_subgraph_stub_columns();
    test_subcanvas_recursive();
    test_high_degree_hub_no_divergence();

    std::fprintf(stderr, "\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
