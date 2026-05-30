// =============================================================================
// gen_e6_scn — herramienta one-shot que construye walkthrough_E6.scn a partir
// de la API headless del NodeGraph + ScnSerializer.  E6 = dos ejes del brazo
// 2R en paralelo, cada uno con el lazo completo de E5 encapsulado en un
// SubGraph.
// =============================================================================

#include "../src/core/NodeGraph.hpp"
#include "../src/core/NodeType.hpp"
#include "../src/core/ScnSerializer.hpp"

#include <cstdio>
#include <fstream>

// Construye el grafo hijo de un "Eje" — lazo completo de control de
// posición articular con frontera explícita (SubGraphInput / Output).
// Topología:
//
//   SubGraphInput(port=0) → Sum(+,−) → PID → DCMotor → Gear → Integrator → SubGraphOutput(port=0)
//                            ↑                                   │
//                            └─────── Gain(K=1) ────────────────┘
static NodeGraph buildAxisChild() {
    NodeGraph c;
    int in    = c.addNode(NodeType::SubGraphInput);
    int out   = c.addNode(NodeType::SubGraphOutput);
    int sum   = c.addNode(NodeType::Summation);
    int pid   = c.addNode(NodeType::PIDController);
    int motor = c.addNode(NodeType::DCMotorModel);
    int gear  = c.addNode(NodeType::GearTransmission);
    int integ = c.addNode(NodeType::Integrator);
    int gain  = c.addNode(NodeType::Gain);

    c.setParam(in,    "Port",       0.0);
    c.setParam(out,   "Port",       0.0);
    c.setParam(sum,   "Sign2",     -1.0);
    c.setParam(pid,   "Kp",        10.0);
    c.setParam(pid,   "Ki",         1.0);
    c.setParam(pid,   "Kd",         5.0);
    c.setParam(pid,   "N (filter)", 100.0);
    c.setParam(gear,  "Ratio",      50.0);
    c.setParam(gear,  "Efficiency", 0.95);

    auto N = [&](int id) { return c.findNode(id); };
    c.tryAddEdge(N(in)->outputAttrId(),     N(sum)->inputAttrId(0));
    c.tryAddEdge(N(sum)->outputAttrId(),    N(pid)->inputAttrId(0));
    c.tryAddEdge(N(pid)->outputAttrId(),    N(motor)->inputAttrId(0));
    c.tryAddEdge(N(motor)->outputAttrId(),  N(gear)->inputAttrId(0));
    c.tryAddEdge(N(gear)->outputAttrId(),   N(integ)->inputAttrId(0));
    c.tryAddEdge(N(integ)->outputAttrId(),  N(out)->inputAttrId(0));
    c.tryAddEdge(N(integ)->outputAttrId(),  N(gain)->inputAttrId(0));
    c.tryAddEdge(N(gain)->outputAttrId(),   N(sum)->inputAttrId(1));

    // Posiciones internas para que el child sea legible al entrar.
    auto setPos = [&](int id, float x, float y) {
        for (auto& n : const_cast<std::vector<NodeInstance>&>(c.nodes()))
            if (n.id == id) { n.position = { x, y }; break; }
    };
    setPos(in,     50.0f, 100.0f);
    setPos(sum,   200.0f, 100.0f);
    setPos(pid,   350.0f, 100.0f);
    setPos(motor, 500.0f, 100.0f);
    setPos(gear,  650.0f, 100.0f);
    setPos(integ, 800.0f, 100.0f);
    setPos(out,   950.0f, 100.0f);
    setPos(gain,  500.0f, 280.0f);
    return c;
}

int main() {
    NodeGraph g;

    // Top-level: 2 StepSignals + 2 SubGraphs + Oscilloscope.
    int step1 = g.addNode(NodeType::StepSignal);
    int step2 = g.addNode(NodeType::StepSignal);
    int scope = g.addNode(NodeType::Oscilloscope);
    g.setParam(step1, "Amplitude",  3.14159265 / 2.0);
    g.setParam(step2, "Amplitude",  3.14159265 / 3.0);
    g.setStringParam(scope, "portLabel0", "Eje 1 — θ_load");
    g.setStringParam(scope, "portLabel1", "Eje 2 — θ_load");
    g.setStringParam(scope, "portUnit0",  "rad");
    g.setStringParam(scope, "portUnit1",  "rad");
    g.setParam(scope, "Time Window", 5.0);

    int sg1 = g.addSubGraphNode();
    int sg2 = g.addSubGraphNode();
    g.installSubGraph(sg1, buildAxisChild());
    g.installSubGraph(sg2, buildAxisChild());
    g.recomputeSubGraphPorts(sg1);
    g.recomputeSubGraphPorts(sg2);
    g.setStringParam(sg1, "Name", "Eje 1");
    g.setStringParam(sg2, "Name", "Eje 2");

    auto Ng = [&](int id) { return g.findNode(id); };
    auto e1 = g.tryAddEdge(Ng(step1)->outputAttrId(), sg1 * 10000 + 0);
    auto e2 = g.tryAddEdge(Ng(step2)->outputAttrId(), sg2 * 10000 + 0);
    auto e3 = g.tryAddEdge(sg1 * 10000 + 9000, Ng(scope)->inputAttrId(0));
    auto e4 = g.tryAddEdge(sg2 * 10000 + 9000, Ng(scope)->inputAttrId(1));
    if (e1 || e2 || e3 || e4) {
        std::fprintf(stderr, "edge failures: %s %s %s %s\n",
            e1 ? e1->message.c_str() : "ok",
            e2 ? e2->message.c_str() : "ok",
            e3 ? e3->message.c_str() : "ok",
            e4 ? e4->message.c_str() : "ok");
        return 1;
    }

    auto setPos = [&](int id, float x, float y) {
        for (auto& n : const_cast<std::vector<NodeInstance>&>(g.nodes()))
            if (n.id == id) { n.position = { x, y }; break; }
    };
    setPos(step1,  50.0f, 100.0f);
    setPos(sg1,   300.0f, 100.0f);
    setPos(step2,  50.0f, 300.0f);
    setPos(sg2,   300.0f, 300.0f);
    setPos(scope, 700.0f, 200.0f);

    ScnPositions emptyPositions;
    std::string json = ScnSerializer::serialize(g, emptyPositions);
    std::ofstream out("examples/graphs/walkthrough_E6.scn");
    if (!out) { std::fprintf(stderr, "cannot open output\n"); return 1; }
    out << json;
    std::printf("Wrote examples/graphs/walkthrough_E6.scn (%d nodes top-level, %d edges)\n",
                g.nodeCount(), g.edgeCount());
    return 0;
}
