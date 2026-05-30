// =============================================================================
// gen_e7_scn — construye walkthrough_E7.scn.
//
// E7 = cinemática inversa 2R planar acoplando los dos ejes de E6.
//
//   Step(x) ─┐
//             ├── InverseKinematics(L1, L2) ── θ1_ref ─→ [Eje 1 (SubGraph)] ─→ θ1 ─┐
//   Step(y) ─┘                                └── θ2_ref ─→ [Eje 2 (SubGraph)] ─→ θ2 ─┤
//                                                                                     ├── Oscilloscope
//                                                                                     │
//                                                          (los dos ejes son los      │
//                                                           mismos SubGraphs de E6)   │
//   TerminalDisplay ── θ1_ref ──────────────────────────────────────────────────────┘
//                  ── θ2_ref
// =============================================================================

#include "../src/core/NodeGraph.hpp"
#include "../src/core/NodeType.hpp"
#include "../src/core/ScnSerializer.hpp"

#include <cstdio>
#include <fstream>

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

    auto setPos = [&](int id, float x, float y) {
        for (auto& n : const_cast<std::vector<NodeInstance>&>(c.nodes()))
            if (n.id == id) { n.position = { x, y }; break; }
    };
    setPos(in,     50.0f, 100.0f); setPos(sum,   200.0f, 100.0f);
    setPos(pid,   350.0f, 100.0f); setPos(motor, 500.0f, 100.0f);
    setPos(gear,  650.0f, 100.0f); setPos(integ, 800.0f, 100.0f);
    setPos(out,   950.0f, 100.0f); setPos(gain,  500.0f, 280.0f);
    return c;
}

int main() {
    NodeGraph g;

    // Sources: (x, y) target del end-effector.
    int sx = g.addNode(NodeType::StepSignal);
    int sy = g.addNode(NodeType::StepSignal);
    g.setParam(sx, "Amplitude", 0.5);   // x_target en metros
    g.setParam(sy, "Amplitude", 0.3);   // y_target

    // Inverse kinematics (Jazar Ex.184): L1 = L2 = 0.4 m, elbow-up.
    int ik = g.addNode(NodeType::InverseKinematics);
    g.setParam(ik, "Link 1 L", 0.4);
    g.setParam(ik, "Link 2 L", 0.4);

    // Dos SubGraphs idénticos (mismos parámetros que E6).
    int sg1 = g.addSubGraphNode();
    int sg2 = g.addSubGraphNode();
    g.installSubGraph(sg1, buildAxisChild());
    g.installSubGraph(sg2, buildAxisChild());
    g.recomputeSubGraphPorts(sg1);
    g.recomputeSubGraphPorts(sg2);
    g.setStringParam(sg1, "Name", "Eje 1 (θ1)");
    g.setStringParam(sg2, "Name", "Eje 2 (θ2)");

    // Sinks: Oscilloscope multi-canal + TerminalDisplay con los ángulos
    // calculados por la IK (lectura instantánea).
    int scope = g.addNode(NodeType::Oscilloscope);
    int term  = g.addNode(NodeType::TerminalDisplay);
    g.setParam(scope, "Time Window", 5.0);
    g.setStringParam(scope, "portLabel0", "θ1_load");
    g.setStringParam(scope, "portLabel1", "θ2_load");
    g.setStringParam(scope, "portUnit0",  "rad");
    g.setStringParam(scope, "portUnit1",  "rad");

    auto N = [&](int id) { return g.findNode(id); };

    // Cableo:
    //   (x, y) → IK
    //   IK:out0 (θ1) → sg1:in0, IK:out1 (θ2) → sg2:in0
    //   sg1:out0 → Scope:in0, sg2:out0 → Scope:in1
    //   IK:out0 → Term:in0, IK:out1 → Term:in1
    g.tryAddEdge(N(sx)->outputAttrId(),  N(ik)->inputAttrId(0));
    g.tryAddEdge(N(sy)->outputAttrId(),  N(ik)->inputAttrId(1));
    g.tryAddEdge(N(ik)->outputAttrId(0), sg1 * 10000 + 0);
    g.tryAddEdge(N(ik)->outputAttrId(1), sg2 * 10000 + 0);
    g.tryAddEdge(sg1 * 10000 + 9000,     N(scope)->inputAttrId(0));
    g.tryAddEdge(sg2 * 10000 + 9000,     N(scope)->inputAttrId(1));
    g.tryAddEdge(N(ik)->outputAttrId(0), N(term)->inputAttrId(0));
    g.tryAddEdge(N(ik)->outputAttrId(1), N(term)->inputAttrId(1));

    auto setPos = [&](int id, float x, float y) {
        for (auto& n : const_cast<std::vector<NodeInstance>&>(g.nodes()))
            if (n.id == id) { n.position = { x, y }; break; }
    };
    setPos(sx,     50.0f, 100.0f);
    setPos(sy,     50.0f, 220.0f);
    setPos(ik,    260.0f, 160.0f);
    setPos(sg1,   470.0f, 100.0f);
    setPos(sg2,   470.0f, 280.0f);
    setPos(scope, 720.0f, 180.0f);
    setPos(term,  720.0f, 420.0f);

    ScnPositions empty;
    std::string json = ScnSerializer::serialize(g, empty);
    std::ofstream out("examples/graphs/walkthrough_E7.scn");
    if (!out) { std::fprintf(stderr, "cannot open output\n"); return 1; }
    out << json;
    std::printf("Wrote walkthrough_E7.scn (%d nodes, %d edges)\n",
                g.nodeCount(), g.edgeCount());
    return 0;
}
