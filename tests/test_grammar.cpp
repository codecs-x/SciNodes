// -----------------------------------------------------------------------
// Unit tests for GrammarParser, NodeGraph, and ScnSerializer.
//
// Build: cmake --build build --target test_grammar
// Run:   ./build/test_grammar
// -----------------------------------------------------------------------
#include "../src/core/CsvExport.hpp"
#include "../src/core/CsvParamIO.hpp"
#include "../src/core/CustomNodeRegistry.hpp"
#include "../src/core/Fft.hpp"
#include "../src/core/GrammarParser.hpp"
#include "../src/core/NodeGraph.hpp"
#include "../src/core/NodeInstance.hpp"
#include "../src/core/DimensionalAnalyzer.hpp"
#include "../src/core/SceneCollector.hpp"
#include "../src/core/ScilabCodeGen.hpp"
#include "../src/core/ScnSerializer.hpp"
#include "../src/core/Unit.hpp"
#include "../src/core/UnitCatalog.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

// ---- Minimal test framework --------------------------------------------
static int g_pass = 0, g_fail = 0;

#define EXPECT_TRUE(cond) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; \
           std::cerr << "  FAIL  " << #cond \
                     << "  [" << __FILE__ << ":" << __LINE__ << "]\n"; } \
} while(0)

#define EXPECT_FALSE(cond)   EXPECT_TRUE(!(cond))
#define EXPECT_VALID(edge)   EXPECT_FALSE((edge).has_value())
#define EXPECT_INVALID(edge) EXPECT_TRUE( (edge).has_value())
#define EXPECT_RULE(edge, r) EXPECT_TRUE( (edge).has_value() && (edge)->rule == (r))

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------
static NodeInstance src(int id, NodeType t = NodeType::SineSignal) {
    return makeNode(id, t);
}
static NodeInstance tx(int id, NodeType t = NodeType::Gain) {
    return makeNode(id, t);
}
static NodeInstance sk(int id, NodeType t = NodeType::Oscilloscope) {
    return makeNode(id, t);
}

// -----------------------------------------------------------------------
// Section 1 — Single-edge validation (GrammarParser::validateEdge)
// -----------------------------------------------------------------------
static void test_edge_source_to_transformer() {
    std::cout << "[1] Source → Transformer\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    EXPECT_VALID(p.validateEdge(src(1), tx(2), no_edges));
}
static void test_edge_source_to_sink() {
    std::cout << "[2] Source → Sink\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    EXPECT_VALID(p.validateEdge(src(1), sk(2), no_edges));
}
static void test_edge_transformer_to_transformer() {
    std::cout << "[3] Transformer → Transformer\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    EXPECT_VALID(p.validateEdge(tx(1), tx(2), no_edges));
}
static void test_edge_transformer_to_sink() {
    std::cout << "[4] Transformer → Sink\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    EXPECT_VALID(p.validateEdge(tx(1), sk(2), no_edges));
}
static void test_edge_sink_to_anything() {
    std::cout << "[5] Sink → Transformer  (R1 violation)\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto err = p.validateEdge(sk(1), tx(2), no_edges);
    EXPECT_RULE(err, "R1");
}
static void test_edge_anything_to_source() {
    std::cout << "[6] Transformer → Source  (R2 violation)\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto err = p.validateEdge(tx(1), src(2), no_edges);
    EXPECT_RULE(err, "R2");
}
static void test_edge_source_to_source() {
    std::cout << "[7] Source → Source  (R2 violation)\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto err = p.validateEdge(src(1), src(2), no_edges);
    EXPECT_RULE(err, "R2");
}
static void test_edge_self_loop() {
    std::cout << "[8] Self-loop  (R3)\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto err = p.validateEdge(tx(5), tx(5), no_edges);
    EXPECT_RULE(err, "R3");
}
static void test_edge_duplicate() {
    std::cout << "[9] Duplicate edge  (R4)\n";
    // R4 is now enforced at NodeGraph::tryAddEdge so it can compare full
    // (fromAttrId, toAttrId) attribute pairs — multi-output sources are
    // allowed to fan out to distinct ports of the same target.
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    auto* ns = g.findNode(s);
    auto* nt = g.findNode(t);
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(0), nt->inputAttrId(0)));
    // Same exact attribute pair again — must be rejected with R4.
    auto err = g.tryAddEdge(ns->outputAttrId(0), nt->inputAttrId(0));
    EXPECT_RULE(err, "R4");
}

// Multi-output source fans out to two distinct input ports of the same
// destination — used to be rejected as R4 in the node-pair-only check.
static void test_edge_multiport_same_pair_allowed() {
    std::cout << "[9b] Multi-output → multi-input on same pair (no R4)\n";
    NodeGraph g;
    int dt = g.addNode(NodeType::DesignTemplate);   // 4 outputs
    int sz = g.addNode(NodeType::PMSMSizing);        // 2 inputs
    auto* nd = g.findNode(dt);
    auto* ns = g.findNode(sz);
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(0), ns->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(1), ns->inputAttrId(1)));
}
static void test_edge_input_occupied() {
    std::cout << "[10] Input already connected  (R5)\n";
    GrammarParser p;
    auto n3 = src(3); auto n2 = tx(2);
    // Edge from node 3 to node 2 already exists
    std::vector<Edge> existing = {
        Edge{ 1, 3, 2, n3.outputAttrId(), n2.inputAttrId(0) }
    };
    // Now try a different source → same sink input port
    auto err = p.validateEdge(src(1), n2, existing);
    EXPECT_RULE(err, "R5");
}

// ---- R6 — port-type sub-language compatibility -----------------------
// Object3D emite Geometry; Gain consume Signal — cualquier intento de
// cruzar los sub-lenguajes sin pasar por TransformObject debe fallar
// con R6 y un mensaje del dominio del problema.
static void test_edge_signal_into_geometry_rejected() {
    std::cout << "[10a] Signal → Geometry input rejected  (R6)\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto sineN     = src(1, NodeType::SineSignal);          // out: Signal
    auto xformN    = tx (2, NodeType::TransformObject);     // in 0: Geometry
    auto err = p.validateEdge(sineN, xformN, no_edges,
                              /*toIsParam=*/false,
                              /*fromPortIdx=*/0, /*toPortIdx=*/0);
    EXPECT_RULE(err, "R6");
    EXPECT_TRUE(err && err->message.find("Transform Object") != std::string::npos);
}
static void test_edge_geometry_into_signal_rejected() {
    std::cout << "[10b] Geometry → Signal input rejected  (R6)\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto objN  = src(1, NodeType::Object3D);   // out 0: Geometry
    auto gainN = tx (2, NodeType::Gain);       // in 0: Signal
    auto err = p.validateEdge(objN, gainN, no_edges);
    EXPECT_RULE(err, "R6");
}
static void test_edge_geometry_into_param_rejected() {
    std::cout << "[10c] Geometry → param pin rejected  (R6)\n";
    // Params son escalares (Signal); enchufar una geometría a un param
    // pin viola R6 — el toIsParam=true no esquiva R6.
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto objN  = src(1, NodeType::Object3D);
    auto gainN = tx (2, NodeType::Gain);
    auto err = p.validateEdge(objN, gainN, no_edges,
                              /*toIsParam=*/true,
                              /*fromPortIdx=*/0, /*toPortIdx=*/0);
    EXPECT_RULE(err, "R6");
}
static void test_edge_geometry_to_geometry_valid() {
    std::cout << "[10d] Geometry → Geometry input valid (TransformObject port 0)\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto objN   = src(1, NodeType::Object3D);
    auto xformN = tx (2, NodeType::TransformObject);
    // port 0 de TransformObject es Geometry — debe pasar.
    EXPECT_VALID(p.validateEdge(objN, xformN, no_edges,
                                /*toIsParam=*/false,
                                /*fromPortIdx=*/0, /*toPortIdx=*/0));
}
static void test_edge_signal_into_transform_signal_port_rejected_now() {
    std::cout << "[10e] Signal → TransformObject port 1 ahora RECHAZADO (port 1 es vec(3), etapa 4)\n";
    // Tras etapa 4 del upgrade gramatical, los puertos rotation/
    // translation/scale del TransformObject son vec(3), no scalar.
    // Cablear directamente un scalar dispara R6 (scalar ≠ vec(3));
    // el usuario debe usar `CombineXYZ` como bridge.
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto sineN  = src(1, NodeType::SineSignal);
    auto xformN = tx (2, NodeType::TransformObject);
    auto err = p.validateEdge(sineN, xformN, no_edges,
                              /*toIsParam=*/false,
                              /*fromPortIdx=*/0, /*toPortIdx=*/1);
    EXPECT_RULE(err, "R6");
}
static void test_edge_combinexyz_into_transform_signal_port_valid() {
    std::cout << "[10e2] CombineXYZ → TransformObject port 1 OK (vec3 ↔ vec3)\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto combN  = tx(1, NodeType::CombineXYZ);
    auto xformN = tx(2, NodeType::TransformObject);
    EXPECT_VALID(p.validateEdge(combN, xformN, no_edges,
                                /*toIsParam=*/false,
                                /*fromPortIdx=*/0, /*toPortIdx=*/1));
}
static void test_edge_geometry_into_scene_output_valid() {
    std::cout << "[10f] TransformObject geometry out → SceneOutput valid\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto xformN = tx(1, NodeType::TransformObject);   // out 0: Geometry
    auto sceneN = sk(2, NodeType::SceneOutput);       // in 0: Geometry
    EXPECT_VALID(p.validateEdge(xformN, sceneN, no_edges,
                                /*toIsParam=*/false,
                                /*fromPortIdx=*/0, /*toPortIdx=*/0));
}
// R6 a través de NodeGraph::tryAddEdge — confirma que el cableo
// pasa las port indices correctas al validador desde la UI/canvas.
// ---- TypeExpr — fundación gramatical (etapa 1) ---------------------------
// Tests headless del aparato nuevo.  Coexiste con el enum PortType
// existente; las pruebas verifican typeMatches/describeType/pinColorFromType
// sin tocar el grafo ni el registry.
static void test_typeexpr_scalar_matches_scalar() {
    std::cout << "[60] typeMatches: scalar ↔ scalar\n";
    EXPECT_TRUE(typeMatches(exprScalar(), exprScalar()));
}
static void test_typeexpr_vec3_matches_vec3() {
    std::cout << "[61] typeMatches: vec(3) ↔ vec(3)\n";
    EXPECT_TRUE(typeMatches(exprVec(3), exprVec(3)));
}
static void test_typeexpr_vec3_vs_vec2() {
    std::cout << "[62] typeMatches: vec(3) NO match vec(2)\n";
    EXPECT_FALSE(typeMatches(exprVec(3), exprVec(2)));
}
static void test_typeexpr_scalar_vs_vec3() {
    std::cout << "[63] typeMatches: scalar NO match vec(3) (sin broadcast)\n";
    EXPECT_FALSE(typeMatches(exprScalar(), exprVec(3)));
}
static void test_typeexpr_matrix() {
    std::cout << "[64] typeMatches: mat(4,4) ↔ mat(4,4); ≠ mat(3,3); ≠ vec(4)\n";
    EXPECT_TRUE (typeMatches(exprMat(4,4), exprMat(4,4)));
    EXPECT_FALSE(typeMatches(exprMat(4,4), exprMat(3,3)));
    EXPECT_FALSE(typeMatches(exprMat(4,4), exprVec(4)));
}
static void test_typeexpr_geometry() {
    std::cout << "[65] typeMatches: geometry ↔ geometry; NO match scalar\n";
    EXPECT_TRUE (typeMatches(exprGeometry(), exprGeometry()));
    EXPECT_FALSE(typeMatches(exprGeometry(), exprScalar()));
    EXPECT_FALSE(typeMatches(exprGeometry(), exprVec(3)));
    EXPECT_FALSE(typeMatches(exprScalar(),   exprGeometry()));
}
static void test_typeexpr_describe() {
    std::cout << "[66] describeType produces canonical strings\n";
    EXPECT_TRUE(describeType(exprScalar())   == "scalar");
    EXPECT_TRUE(describeType(exprVec(3))     == "vec(3)");
    EXPECT_TRUE(describeType(exprVec(7))     == "vec(7)");
    EXPECT_TRUE(describeType(exprMat(4,4))   == "mat(4,4)");
    EXPECT_TRUE(describeType(exprMat(2,3))   == "mat(2,3)");
    EXPECT_TRUE(describeType(exprGeometry()) == "geometry");
}
static void test_typeexpr_predicates() {
    std::cout << "[67] isGeometryType / isScalarType\n";
    EXPECT_TRUE (isScalarType  (exprScalar()));
    EXPECT_FALSE(isScalarType  (exprVec(3)));
    EXPECT_FALSE(isScalarType  (exprGeometry()));
    EXPECT_TRUE (isGeometryType(exprGeometry()));
    EXPECT_FALSE(isGeometryType(exprScalar()));
    EXPECT_FALSE(isGeometryType(exprVec(3)));
}
static void test_typeexpr_pin_colors_distinct() {
    std::cout << "[68] pinColorFromType: scalar/vec/mat/geometry colors distintos\n";
    const auto cS = pinColorFromType(exprScalar());
    const auto cV = pinColorFromType(exprVec(3));
    const auto cM = pinColorFromType(exprMat(4,4));
    const auto cG = pinColorFromType(exprGeometry());
    EXPECT_TRUE(cS != cV);
    EXPECT_TRUE(cS != cM);
    EXPECT_TRUE(cS != cG);
    EXPECT_TRUE(cV != cM);
    EXPECT_TRUE(cV != cG);
    EXPECT_TRUE(cM != cG);
}
// ---- Etapa 4 — sólo el test de tipos: no requiere StubResolver -----------
static void test_vec3_nodes_registered_with_correct_types() {
    std::cout << "[70] Vec3Constant/CombineXYZ/SeparateXYZ: tipos de puerto correctos\n";
    EXPECT_TRUE(typeMatches(outputPortTypeOf(NodeType::Vec3Constant, 0), exprVec(3)));
    for (int p = 0; p < 3; ++p)
        EXPECT_TRUE(typeMatches(inputPortTypeOf(NodeType::CombineXYZ, p), exprScalar()));
    EXPECT_TRUE(typeMatches(outputPortTypeOf(NodeType::CombineXYZ, 0), exprVec(3)));
    EXPECT_TRUE(typeMatches(inputPortTypeOf(NodeType::SeparateXYZ, 0), exprVec(3)));
    for (int p = 0; p < 3; ++p)
        EXPECT_TRUE(typeMatches(outputPortTypeOf(NodeType::SeparateXYZ, p), exprScalar()));
    EXPECT_TRUE(typeMatches(inputPortTypeOf(NodeType::TransformObject, 0), exprGeometry()));
    EXPECT_TRUE(typeMatches(inputPortTypeOf(NodeType::TransformObject, 1), exprVec(3)));
    EXPECT_TRUE(typeMatches(inputPortTypeOf(NodeType::TransformObject, 2), exprVec(3)));
    EXPECT_TRUE(typeMatches(inputPortTypeOf(NodeType::TransformObject, 3), exprVec(3)));
}
static void test_vec3_r6_rejects_scalar_to_vec3() {
    std::cout << "[72] R6: scalar → vec(3) port rechazado con mensaje describiendo tipos\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto sineN  = src(1, NodeType::SineSignal);
    auto xformN = tx (2, NodeType::TransformObject);
    auto err = p.validateEdge(sineN, xformN, no_edges,
                              /*toIsParam=*/false,
                              /*fromPortIdx=*/0, /*toPortIdx=*/1);
    EXPECT_RULE(err, "R6");
    EXPECT_TRUE(err && err->message.find("scalar") != std::string::npos);
    EXPECT_TRUE(err && err->message.find("vec(3)") != std::string::npos);
}
static void test_vec3_r6_accepts_vec3_to_vec3() {
    std::cout << "[73] R6: vec(3) → vec(3) port aceptado\n";
    GrammarParser p;
    std::vector<Edge> no_edges;
    auto vcN    = src(1, NodeType::Vec3Constant);
    auto xformN = tx (2, NodeType::TransformObject);
    EXPECT_VALID(p.validateEdge(vcN, xformN, no_edges,
                                /*toIsParam=*/false,
                                /*fromPortIdx=*/0, /*toPortIdx=*/1));
}

// ---- Etapa 6A — álgebra de Unit (SI exponents + magnitude) -------------
using scinodes::Unit;
using scinodes::unitDimensionless;
using scinodes::unitMeter;
using scinodes::unitKilogram;
using scinodes::unitSecond;
using scinodes::unitAmpere;
using scinodes::unitWithPrefix;

static void test_unit_default_is_dimensionless() {
    std::cout << "[80] Unit default = adimensional (todos los exp 0)\n";
    Unit u;
    EXPECT_TRUE(u.isDimensionless());
    EXPECT_TRUE(u.magnitude == 1.0);
    EXPECT_TRUE(u.sameDimension(unitDimensionless()));
}
static void test_unit_base_constructors_distinct() {
    std::cout << "[81] Cada unidad SI base ocupa un exponente único\n";
    EXPECT_FALSE(unitMeter().sameDimension(unitKilogram()));
    EXPECT_FALSE(unitMeter().sameDimension(unitSecond()));
    EXPECT_FALSE(unitMeter().sameDimension(unitAmpere()));
    EXPECT_FALSE(unitKilogram().sameDimension(unitSecond()));
    EXPECT_FALSE(unitSecond().sameDimension(unitAmpere()));
}
static void test_unit_prefix_preserves_dimension() {
    std::cout << "[82] km, cm, mm: misma dimensión que m, distinta magnitud\n";
    Unit km = unitWithPrefix(unitMeter(), 1000.0);
    Unit cm = unitWithPrefix(unitMeter(), 0.01);
    Unit mm = unitWithPrefix(unitMeter(), 0.001);
    EXPECT_TRUE(km.sameDimension(unitMeter()));
    EXPECT_TRUE(cm.sameDimension(unitMeter()));
    EXPECT_TRUE(mm.sameDimension(unitMeter()));
    // 100 cm = 1 m dimensionalmente (mismo exp); magnitudes distintas.
    EXPECT_TRUE(std::fabs(cm.magnitude - 0.01) < 1e-12);
    EXPECT_TRUE(std::fabs(km.magnitude - 1000.0) < 1e-9);
}
static void test_unit_multiplication_VA_equals_W() {
    std::cout << "[83] V·A == W dimensionalmente\n";
    // V = m²·kg·s⁻³·A⁻¹.  W = m²·kg·s⁻³.
    Unit volt  = Unit{ {2,1,-3,-1,0,0,0}, 1.0 };
    Unit amp   = unitAmpere();
    Unit watt  = Unit{ {2,1,-3, 0,0,0,0}, 1.0 };
    Unit va    = volt * amp;
    EXPECT_TRUE(va.sameDimension(watt));
    EXPECT_TRUE(va == watt);   // misma magnitud también
}
static void test_unit_multiplication_Nm_equals_J() {
    std::cout << "[84] N·m == J dimensionalmente\n";
    // N = kg·m·s⁻².  J = kg·m²·s⁻².
    Unit newton = Unit{ {1,1,-2, 0,0,0,0}, 1.0 };
    Unit meter  = unitMeter();
    Unit joule  = Unit{ {2,1,-2, 0,0,0,0}, 1.0 };
    Unit nm     = newton * meter;
    EXPECT_TRUE(nm.sameDimension(joule));
}
static void test_unit_division_V_per_A_equals_Ohm() {
    std::cout << "[85] V / A == Ω dimensionalmente\n";
    // V = m²·kg·s⁻³·A⁻¹.  Ω = m²·kg·s⁻³·A⁻².
    Unit volt = Unit{ {2,1,-3,-1,0,0,0}, 1.0 };
    Unit amp  = unitAmpere();
    Unit ohm  = Unit{ {2,1,-3,-2,0,0,0}, 1.0 };
    Unit vov  = volt / amp;
    EXPECT_TRUE(vov.sameDimension(ohm));
}
static void test_unit_division_inverse_dimensions() {
    std::cout << "[86] 1/s tiene exp(s)=-1 — base de Hz, rad/s\n";
    Unit one_per_s = unitDimensionless() / unitSecond();
    EXPECT_TRUE(one_per_s.exp[2] == -1);
    // Las otras dimensiones quedan en 0.
    EXPECT_TRUE(one_per_s.exp[0] == 0 && one_per_s.exp[1] == 0);
}
static void test_unit_pow_square_meter() {
    std::cout << "[87] m^2 = área (exp(m)=2)\n";
    Unit area = unitMeter().pow(2);
    EXPECT_TRUE(area.exp[0] == 2);
    EXPECT_TRUE(area.magnitude == 1.0);
}
static void test_unit_pow_negative_inverse() {
    std::cout << "[88] m^(-1) y (1/m) producen el mismo Unit\n";
    Unit inv_a = unitMeter().pow(-1);
    Unit inv_b = unitDimensionless() / unitMeter();
    EXPECT_TRUE(inv_a.sameDimension(inv_b));
    EXPECT_TRUE(inv_a == inv_b);
}
static void test_unit_dimensionless_is_neutral_for_multiplication() {
    std::cout << "[89] dimensionless * X == X (neutro del producto)\n";
    Unit m  = unitMeter();
    Unit r  = unitDimensionless() * m;
    EXPECT_TRUE(r == m);
}
static void test_unit_radians_per_second_equals_Hz_dimensionally() {
    std::cout << "[90] rad/s ≡ Hz dimensionalmente (rad es adimensional)\n";
    Unit rad    = unitDimensionless();
    Unit rad_s  = rad / unitSecond();
    Unit hz     = unitDimensionless() / unitSecond();
    EXPECT_TRUE(rad_s.sameDimension(hz));
}
// ---- Etapa 6B — parser gramatical de unidades textuales ---------------
using scinodes::parseUnit;
using scinodes::ParseUnitResult;

static void test_unit_parse_empty_is_error() {
    std::cout << "[100] parseUnit(\"\") rechaza con error claro\n";
    auto r = parseUnit("");
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(r.error.find("Empty") != std::string::npos);
}
static void test_unit_parse_base_units() {
    std::cout << "[101] parseUnit reconoce las 7 bases SI\n";
    EXPECT_TRUE(parseUnit("m").ok());
    EXPECT_TRUE(parseUnit("kg").ok());
    EXPECT_TRUE(parseUnit("s").ok());
    EXPECT_TRUE(parseUnit("A").ok());
    EXPECT_TRUE(parseUnit("K").ok());
    EXPECT_TRUE(parseUnit("mol").ok());
    EXPECT_TRUE(parseUnit("cd").ok());
    // Y produce los exponentes correctos
    auto u = parseUnit("m"); EXPECT_TRUE(u.unit.exp[0] == 1);
    auto k = parseUnit("kg"); EXPECT_TRUE(k.unit.exp[1] == 1);
}
static void test_unit_parse_derived() {
    std::cout << "[102] parseUnit reconoce derivados (V, W, J, N, Pa, Hz, Ω)\n";
    EXPECT_TRUE(parseUnit("V").ok());
    EXPECT_TRUE(parseUnit("W").ok());
    EXPECT_TRUE(parseUnit("J").ok());
    EXPECT_TRUE(parseUnit("N").ok());
    EXPECT_TRUE(parseUnit("Pa").ok());
    EXPECT_TRUE(parseUnit("Hz").ok());
    EXPECT_TRUE(parseUnit("rad").ok());
    EXPECT_TRUE(parseUnit("deg").ok());
}
static void test_unit_parse_prefix() {
    std::cout << "[103] parseUnit reconoce prefijos: kV = 1000 V dim, kHz, ms\n";
    auto kV = parseUnit("kV"); EXPECT_TRUE(kV.ok());
    auto V  = parseUnit("V");
    EXPECT_TRUE(kV.unit.sameDimension(V.unit));
    EXPECT_TRUE(std::fabs(kV.unit.magnitude - 1e3) < 1e-9);

    auto ms = parseUnit("ms"); EXPECT_TRUE(ms.ok());
    EXPECT_TRUE(ms.unit.exp[2] == 1);   // segundo
    EXPECT_TRUE(std::fabs(ms.unit.magnitude - 1e-3) < 1e-12);

    auto cm = parseUnit("cm"); EXPECT_TRUE(cm.ok());
    EXPECT_TRUE(cm.unit.exp[0] == 1);
    EXPECT_TRUE(std::fabs(cm.unit.magnitude - 1e-2) < 1e-12);
}
static void test_unit_parse_da_prefix_longest_match() {
    std::cout << "[104] 'da' (deca) tiene precedencia sobre 'd' (deci)\n";
    auto daA = parseUnit("daA");
    EXPECT_TRUE(daA.ok());
    EXPECT_TRUE(std::fabs(daA.unit.magnitude - 1e1) < 1e-9);
    auto dA = parseUnit("dA");
    EXPECT_TRUE(dA.ok());
    EXPECT_TRUE(std::fabs(dA.unit.magnitude - 1e-1) < 1e-9);
}
static void test_unit_parse_product_center_dot() {
    std::cout << "[105] parseUnit(\"V·A\") == W dimensionalmente\n";
    auto va = parseUnit("V·A"); EXPECT_TRUE(va.ok());
    auto w  = parseUnit("W");   EXPECT_TRUE(w.ok());
    EXPECT_TRUE(va.unit.sameDimension(w.unit));
}
static void test_unit_parse_product_asterisk() {
    std::cout << "[106] parseUnit(\"V*A\") == W (alias ASCII para ·)\n";
    auto va = parseUnit("V*A"); EXPECT_TRUE(va.ok());
    auto w  = parseUnit("W");
    EXPECT_TRUE(va.unit.sameDimension(w.unit));
}
static void test_unit_parse_division() {
    std::cout << "[107] parseUnit(\"V/A\") == Ω dimensionalmente\n";
    auto ohm  = parseUnit("V/A"); EXPECT_TRUE(ohm.ok());
    EXPECT_TRUE(ohm.unit.exp[0] == 2);   // m²
    EXPECT_TRUE(ohm.unit.exp[1] == 1);   // kg
    EXPECT_TRUE(ohm.unit.exp[2] == -3);  // s⁻³
    EXPECT_TRUE(ohm.unit.exp[3] == -2);  // A⁻²
    auto omega = parseUnit("Ω");
    EXPECT_TRUE(ohm.unit.sameDimension(omega.unit));
}
static void test_unit_parse_power() {
    std::cout << "[108] parseUnit(\"m^2\") = área; \"m^-1\" = 1/m\n";
    auto m2 = parseUnit("m^2"); EXPECT_TRUE(m2.ok());
    EXPECT_TRUE(m2.unit.exp[0] == 2);
    auto inv = parseUnit("m^-1"); EXPECT_TRUE(inv.ok());
    EXPECT_TRUE(inv.unit.exp[0] == -1);
}
static void test_unit_parse_complex_expression() {
    std::cout << "[109] parseUnit(\"kg·m/s^2\") == N dimensionalmente\n";
    auto n  = parseUnit("kg·m/s^2"); EXPECT_TRUE(n.ok());
    auto N  = parseUnit("N");
    EXPECT_TRUE(n.unit.sameDimension(N.unit));
}
static void test_unit_parse_parens() {
    std::cout << "[110] parseUnit(\"kg·m^2/(A·s^3)\") == V dim\n";
    auto v = parseUnit("kg·m^2/(A·s^3)");
    EXPECT_TRUE(v.ok());
    auto V = parseUnit("V");
    EXPECT_TRUE(v.unit.sameDimension(V.unit));
}
static void test_unit_parse_Nm_equals_J() {
    std::cout << "[111] parseUnit(\"N·m\") == J dimensionalmente\n";
    auto nm = parseUnit("N·m"); EXPECT_TRUE(nm.ok());
    auto J  = parseUnit("J");
    EXPECT_TRUE(nm.unit.sameDimension(J.unit));
}
static void test_unit_parse_rad_per_s_distinct_from_Hz() {
    std::cout << "[112] parseUnit(\"rad/s\") ≠ Hz desde la 8ª dim (etapa 6I.L)\n";
    // Phantom angle exponent: rad/s tiene exp[7]=1, Hz exp[7]=0.
    // Antes eran intercambiables (mismo s⁻¹); ahora distinguibles.
    auto rs = parseUnit("rad/s"); EXPECT_TRUE(rs.ok());
    auto hz = parseUnit("Hz");
    EXPECT_FALSE(rs.unit.sameDimension(hz.unit));
    EXPECT_TRUE(rs.unit.exp[7] == 1);
    EXPECT_TRUE(hz.unit.exp[7] == 0);
}
static void test_unit_parse_whitespace_ignored() {
    std::cout << "[113] Whitespace alrededor de operadores ignorado\n";
    auto a = parseUnit("V · A"); EXPECT_TRUE(a.ok());
    auto b = parseUnit("V·A");
    EXPECT_TRUE(a.unit == b.unit);
    auto c = parseUnit("kg · m / s^2"); EXPECT_TRUE(c.ok());
    EXPECT_TRUE(c.unit.sameDimension(parseUnit("N").unit));
}
static void test_unit_parse_unknown_rejects() {
    std::cout << "[114] parseUnit(\"xyz\") rechaza con mensaje\n";
    auto r = parseUnit("xyz");
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(!r.error.empty());
}
static void test_unit_parse_unclosed_paren_rejects() {
    std::cout << "[115] parseUnit(\"(V·A\") rechaza por falta de ')'\n";
    auto r = parseUnit("(V·A");
    EXPECT_FALSE(r.ok());
}

// ---- Etapa 6C — catálogo canónico + toCanonicalString ------------------
static void test_units_catalog_matches_parser() {
    std::cout << "[116] Cada constante del catálogo == parseUnit(símbolo)\n";
    EXPECT_TRUE(parseUnit("V").unit  == scinodes::units::kVolt);
    EXPECT_TRUE(parseUnit("W").unit  == scinodes::units::kWatt);
    EXPECT_TRUE(parseUnit("J").unit  == scinodes::units::kJoule);
    EXPECT_TRUE(parseUnit("N").unit  == scinodes::units::kNewton);
    EXPECT_TRUE(parseUnit("Hz").unit == scinodes::units::kHertz);
    EXPECT_TRUE(parseUnit("Pa").unit == scinodes::units::kPascal);
    EXPECT_TRUE(parseUnit("Ω").unit  == scinodes::units::kOhm);
    EXPECT_TRUE(parseUnit("m").unit  == scinodes::units::kMeter);
    EXPECT_TRUE(parseUnit("kg").unit == scinodes::units::kKilogram);
    EXPECT_TRUE(parseUnit("s").unit  == scinodes::units::kSecond);
    EXPECT_TRUE(parseUnit("A").unit  == scinodes::units::kAmpere);
    EXPECT_TRUE(parseUnit("K").unit  == scinodes::units::kKelvin);
}
static void test_units_catalog_compound_names() {
    std::cout << "[117] kRadianPerSec = kRadian / kSecond; kNewtonMeter ≡ J dim\n";
    // Etapa 6I.L: rad/s = rad × s⁻¹ (lleva el exp[7]=1).  Ya NO ≡ Hz.
    EXPECT_TRUE(scinodes::units::kRadianPerSec.sameDimension(
        scinodes::units::kRadian / scinodes::units::kSecond));
    EXPECT_FALSE(scinodes::units::kRadianPerSec.sameDimension(
        scinodes::units::kHertz));
    EXPECT_TRUE(scinodes::units::kNewtonMeter.sameDimension(
        scinodes::units::kNewton * scinodes::units::kMeter));
    // N·m ≡ J dim (no involucra angle).
    EXPECT_TRUE(scinodes::units::kNewtonMeter.sameDimension(
        scinodes::units::kJoule));
}
static void test_unit_toString_dimensionless_vs_angle() {
    std::cout << "[118] toCanonicalString: adim → \"\"; rad → \"rad\"\n";
    // Phantom angle exponent: dimensionless puro y rad ya NO son
    // intercambiables.  El display refleja la diferencia.
    EXPECT_TRUE(scinodes::units::kDimensionless.toCanonicalString() == "");
    EXPECT_TRUE(scinodes::units::kRadian.toCanonicalString() == "rad");
}
static void test_unit_toString_named_units() {
    std::cout << "[119] toCanonicalString reconoce V, W, J, N, Hz, Ω, ...\n";
    EXPECT_TRUE(scinodes::units::kVolt.toCanonicalString()    == "V");
    EXPECT_TRUE(scinodes::units::kWatt.toCanonicalString()    == "W");
    EXPECT_TRUE(scinodes::units::kJoule.toCanonicalString()   == "J");
    EXPECT_TRUE(scinodes::units::kNewton.toCanonicalString()  == "N");
    EXPECT_TRUE(scinodes::units::kHertz.toCanonicalString()   == "Hz");
    EXPECT_TRUE(scinodes::units::kOhm.toCanonicalString()     == "Ohm");
    EXPECT_TRUE(scinodes::units::kMeter.toCanonicalString()   == "m");
    EXPECT_TRUE(scinodes::units::kKilogram.toCanonicalString() == "kg");
}
static void test_unit_toString_with_prefix() {
    std::cout << "[120] toCanonicalString con prefix: kV, mV, ms, GHz\n";
    auto kV = parseUnit("kV"); EXPECT_TRUE(kV.ok());
    EXPECT_TRUE(kV.unit.toCanonicalString() == "kV");

    auto mV = parseUnit("mV"); EXPECT_TRUE(mV.ok());
    EXPECT_TRUE(mV.unit.toCanonicalString() == "mV");

    auto ms = parseUnit("ms"); EXPECT_TRUE(ms.ok());
    EXPECT_TRUE(ms.unit.toCanonicalString() == "ms");

    auto GHz = parseUnit("GHz"); EXPECT_TRUE(GHz.ok());
    EXPECT_TRUE(GHz.unit.toCanonicalString() == "GHz");
}
static void test_unit_toString_decomposition() {
    std::cout << "[121] toCanonicalString para no-named: m^2·kg/(s^3·A)\n";
    // Un torque dividido por área eléctrica: V·s = kg·m²/(s²·A).
    // No es un símbolo nombrado en mi catálogo, así que cae a
    // decomposición.
    Unit u = scinodes::units::kVolt * scinodes::units::kSecond;
    // V·s exp = (2,1,-2,-1,0,0,0).
    EXPECT_TRUE(u.exp[0] == 2 && u.exp[1] == 1 && u.exp[2] == -2 && u.exp[3] == -1);
    std::string s = u.toCanonicalString();
    // El display debería contener "m" y "kg" y un "/" antes del
    // denominador.
    EXPECT_TRUE(s.find("m^2") != std::string::npos);
    EXPECT_TRUE(s.find("kg")  != std::string::npos);
    EXPECT_TRUE(s.find("/")   != std::string::npos);
    EXPECT_TRUE(s.find("s^2") != std::string::npos);
    EXPECT_TRUE(s.find("A")   != std::string::npos);
}
// ---- Etapa 6D — unidades declaradas en el registry ----------------------
static void test_registry_declares_voltage_source_volts() {
    std::cout << "[123] VoltageSource declara output [V]\n";
    auto& def = nodeRegistry().at(NodeType::VoltageSource);
    EXPECT_TRUE(hasDeclaredOutputUnit(def, 0));
    EXPECT_TRUE(outputPortUnitOf(def, 0).sameDimension(scinodes::units::kVolt));
}
static void test_registry_declares_current_source_amperes() {
    std::cout << "[124] CurrentSource declara output [A]\n";
    auto& def = nodeRegistry().at(NodeType::CurrentSource);
    EXPECT_TRUE(hasDeclaredOutputUnit(def, 0));
    EXPECT_TRUE(outputPortUnitOf(def, 0).sameDimension(scinodes::units::kAmpere));
}
static void test_registry_dcmotor_units() {
    std::cout << "[125] DCMotor: input [V], output [rad/s]\n";
    auto& def = nodeRegistry().at(NodeType::DCMotorModel);
    EXPECT_TRUE(hasDeclaredInputUnit(def, 0));
    EXPECT_TRUE(hasDeclaredOutputUnit(def, 0));
    EXPECT_TRUE(inputPortUnitOf(def, 0).sameDimension(scinodes::units::kVolt));
    EXPECT_TRUE(outputPortUnitOf(def, 0).sameDimension(scinodes::units::kRadianPerSec));
}
static void test_registry_polimorphic_nodes_have_no_units() {
    std::cout << "[126] Gain, Sum, Step, Sine: sin unidades declaradas (polimórficos)\n";
    auto& gain = nodeRegistry().at(NodeType::Gain);
    EXPECT_FALSE(hasDeclaredInputUnit(gain, 0));
    EXPECT_FALSE(hasDeclaredOutputUnit(gain, 0));
    auto& sum = nodeRegistry().at(NodeType::Summation);
    EXPECT_FALSE(hasDeclaredInputUnit(sum, 0));
    EXPECT_FALSE(hasDeclaredOutputUnit(sum, 0));
    auto& step = nodeRegistry().at(NodeType::StepSignal);
    EXPECT_FALSE(hasDeclaredOutputUnit(step, 0));
    auto& sine = nodeRegistry().at(NodeType::SineSignal);
    EXPECT_FALSE(hasDeclaredOutputUnit(sine, 0));
}
// ---- Etapa 6E — propagación dimensional (analyzeUnits) ----------------
static void test_dimanalyzer_voltage_source_seeds_volts() {
    std::cout << "[128] analyzeUnits: VoltageSource.out se inicializa como V\n";
    NodeGraph g;
    int v = g.addNode(NodeType::VoltageSource);
    auto* nv = g.findNode(v);
    auto a = scinodes::analyzeUnits(g);
    EXPECT_TRUE(a.ok());
    EXPECT_TRUE(a.isResolved(nv->outputAttrId(0)));
    EXPECT_TRUE(a.unitAt(nv->outputAttrId(0)).sameDimension(scinodes::units::kVolt));
}
static void test_dimanalyzer_backward_propagation_into_polymorphic() {
    std::cout << "[129] DCMotor.in [V] propaga hacia atrás a Gain.in y Gain.out\n";
    // StepSignal → Gain → DCMotor
    NodeGraph g;
    int s = g.addNode(NodeType::StepSignal);
    int K = g.addNode(NodeType::Gain);
    int M = g.addNode(NodeType::DCMotorModel);
    auto* ns = g.findNode(s);
    auto* nK = g.findNode(K);
    auto* nM = g.findNode(M);
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(0), nK->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nK->outputAttrId(0), nM->inputAttrId(0)));

    auto a = scinodes::analyzeUnits(g);
    EXPECT_TRUE(a.ok());
    // El Gain polimórfico hereda V por backward propagation desde DCMotor.in.
    EXPECT_TRUE(a.isResolved(nK->inputAttrId(0)));
    EXPECT_TRUE(a.isResolved(nK->outputAttrId(0)));
    EXPECT_TRUE(a.unitAt(nK->outputAttrId(0)).sameDimension(scinodes::units::kVolt));
    EXPECT_TRUE(a.unitAt(nK->inputAttrId(0)).sameDimension(scinodes::units::kVolt));
    // Y el Step.out también, por el mismo camino.
    EXPECT_TRUE(a.unitAt(ns->outputAttrId(0)).sameDimension(scinodes::units::kVolt));
}
static void test_dimanalyzer_forward_propagation() {
    std::cout << "[130] VoltageSource → Gain → Oscilloscope: Gain heredera V forward\n";
    NodeGraph g;
    int V = g.addNode(NodeType::VoltageSource);
    int K = g.addNode(NodeType::Gain);
    int O = g.addNode(NodeType::Oscilloscope);
    auto* nV = g.findNode(V);
    auto* nK = g.findNode(K);
    auto* nO = g.findNode(O);
    g.tryAddEdge(nV->outputAttrId(0), nK->inputAttrId(0));
    g.tryAddEdge(nK->outputAttrId(0), nO->inputAttrId(0));

    auto a = scinodes::analyzeUnits(g);
    EXPECT_TRUE(a.ok());
    EXPECT_TRUE(a.unitAt(nK->inputAttrId(0)).sameDimension(scinodes::units::kVolt));
    EXPECT_TRUE(a.unitAt(nK->outputAttrId(0)).sameDimension(scinodes::units::kVolt));
    EXPECT_TRUE(a.unitAt(nO->inputAttrId(0)).sameDimension(scinodes::units::kVolt));
}
static void test_dimanalyzer_conflict_VA_into_Sum() {
    std::cout << "[131] Conflicto: V → Sum:0 y A → Sum:1\n";
    NodeGraph g;
    int V = g.addNode(NodeType::VoltageSource);
    int I = g.addNode(NodeType::CurrentSource);
    int S = g.addNode(NodeType::Summation);
    auto* nV = g.findNode(V);
    auto* nI = g.findNode(I);
    auto* nS = g.findNode(S);
    g.tryAddEdge(nV->outputAttrId(0), nS->inputAttrId(0));
    g.tryAddEdge(nI->outputAttrId(0), nS->inputAttrId(1));

    auto a = scinodes::analyzeUnits(g);
    EXPECT_FALSE(a.ok());
    EXPECT_TRUE(!a.conflicts.empty());
}
static void test_dimanalyzer_dcmotor_output_radps_propagates() {
    std::cout << "[132] DCMotor.out [rad/s] propaga forward al GearTransmission\n";
    NodeGraph g;
    int V = g.addNode(NodeType::VoltageSource);
    int M = g.addNode(NodeType::DCMotorModel);
    int G = g.addNode(NodeType::GearTransmission);
    auto* nV = g.findNode(V);
    auto* nM = g.findNode(M);
    auto* nG = g.findNode(G);
    g.tryAddEdge(nV->outputAttrId(0), nM->inputAttrId(0));
    g.tryAddEdge(nM->outputAttrId(0), nG->inputAttrId(0));

    auto a = scinodes::analyzeUnits(g);
    EXPECT_TRUE(a.ok());
    EXPECT_TRUE(a.unitAt(nM->outputAttrId(0)).sameDimension(scinodes::units::kRadianPerSec));
    EXPECT_TRUE(a.unitAt(nG->inputAttrId(0)).sameDimension(scinodes::units::kRadianPerSec));
    EXPECT_TRUE(a.unitAt(nG->outputAttrId(0)).sameDimension(scinodes::units::kRadianPerSec));
}
static void test_dimanalyzer_isolated_polymorphic_unresolved() {
    std::cout << "[133] Step → Oscilloscope sin nodos dimensionados: queda sin resolver\n";
    NodeGraph g;
    int s = g.addNode(NodeType::StepSignal);
    int o = g.addNode(NodeType::Oscilloscope);
    auto* ns = g.findNode(s);
    auto* no = g.findNode(o);
    g.tryAddEdge(ns->outputAttrId(0), no->inputAttrId(0));

    auto a = scinodes::analyzeUnits(g);
    EXPECT_TRUE(a.ok());   // sin declaraciones, sin conflictos
    EXPECT_FALSE(a.isResolved(ns->outputAttrId(0)));
    EXPECT_FALSE(a.isResolved(no->inputAttrId(0)));
}
static void test_dimanalyzer_feedback_loop_detects_conflict() {
    std::cout << "[134] Lazo control con rad/s feeding back a Sum recibiendo V: analyzeUnits detecta el conflicto\n";
    NodeGraph g;
    int s = g.addNode(NodeType::StepSignal);
    int sum = g.addNode(NodeType::Summation);
    int pid = g.addNode(NodeType::PIDController);
    int m = g.addNode(NodeType::DCMotorModel);
    int integ = g.addNode(NodeType::Integrator);
    int K = g.addNode(NodeType::Gain);
    auto nodeOf = [&](int id) -> const NodeInstance* { return g.findNode(id); };
    g.tryAddEdge(nodeOf(s)->outputAttrId(0),   nodeOf(sum)->inputAttrId(0));
    g.tryAddEdge(nodeOf(sum)->outputAttrId(0), nodeOf(pid)->inputAttrId(0));
    g.tryAddEdge(nodeOf(pid)->outputAttrId(0), nodeOf(m)->inputAttrId(0));
    g.tryAddEdge(nodeOf(m)->outputAttrId(0),   nodeOf(integ)->inputAttrId(0));
    g.tryAddEdge(nodeOf(integ)->outputAttrId(0), nodeOf(K)->inputAttrId(0));
    g.tryAddEdge(nodeOf(K)->outputAttrId(0),     nodeOf(sum)->inputAttrId(1));

    // Con R7 OFF (default), el lazo se construye y analyzeUnits
    // detecta el conflicto post-hoc.  Etapa 6G permitirá rechazo
    // pre-hoc via opt-in cuando PID sea unit-transformer per-instance.
    auto a = scinodes::analyzeUnits(g);
    EXPECT_FALSE(a.ok());
}
static void test_r7_optin_rejects_current_into_voltage_input() {
    std::cout << "[135] R7 (opt-in) rechaza CurrentSource[A] → DCMotor.in[V]\n";
    NodeGraph g;
    g.setDimensionalEnforcement(true);
    int I = g.addNode(NodeType::CurrentSource);
    int M = g.addNode(NodeType::DCMotorModel);
    auto* nI = g.findNode(I);
    auto* nM = g.findNode(M);
    auto err = g.tryAddEdge(nI->outputAttrId(0), nM->inputAttrId(0));
    EXPECT_RULE(err, "R7");
}
static void test_r7_optin_accepts_voltage_into_voltage_input() {
    std::cout << "[136] R7 (opt-in) acepta VoltageSource[V] → DCMotor.in[V]\n";
    NodeGraph g;
    g.setDimensionalEnforcement(true);
    int V = g.addNode(NodeType::VoltageSource);
    int M = g.addNode(NodeType::DCMotorModel);
    auto* nV = g.findNode(V);
    auto* nM = g.findNode(M);
    EXPECT_VALID(g.tryAddEdge(nV->outputAttrId(0), nM->inputAttrId(0)));
}
static void test_r7_optin_accepts_polymorphic_chain() {
    std::cout << "[137] R7 (opt-in) acepta Step→Gain→DCMotor (heredan V backward)\n";
    NodeGraph g;
    g.setDimensionalEnforcement(true);
    int s = g.addNode(NodeType::StepSignal);
    int K = g.addNode(NodeType::Gain);
    int M = g.addNode(NodeType::DCMotorModel);
    auto* ns = g.findNode(s);
    auto* nK = g.findNode(K);
    auto* nM = g.findNode(M);
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(0), nK->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nK->outputAttrId(0), nM->inputAttrId(0)));
}
// ---- Etapa 6G — overrides per-instance --------------------------------
static void test_override_seeds_polymorphic_port() {
    std::cout << "[139] Override en input 0 de StepSignal lo dimensiona como V\n";
    NodeGraph g;
    int s = g.addNode(NodeType::StepSignal);
    auto* ns = g.findNode(s);
    // StepSignal es polimórfico — su único output (port 0) sin
    // declaración del registry.  Override lo pinea a V.
    auto* nsMut = const_cast<NodeInstance*>(ns);
    nsMut->portUnitOverrides[portKeyForOutput(0)] = "V";

    auto a = scinodes::analyzeUnits(g);
    EXPECT_TRUE(a.ok());
    EXPECT_TRUE(a.isResolved(ns->outputAttrId(0)));
    EXPECT_TRUE(a.unitAt(ns->outputAttrId(0)).sameDimension(scinodes::units::kVolt));
}
static void test_override_ignored_on_dimensioned_node() {
    std::cout << "[140] Override en VoltageSource.out es IGNORADO (registry wins)\n";
    NodeGraph g;
    int V = g.addNode(NodeType::VoltageSource);
    auto* nV = g.findNode(V);
    auto* nVMut = const_cast<NodeInstance*>(nV);
    // Intento de corromper VoltageSource.out a A — el analyzer debe
    // respetar la declaración del registry (V).
    nVMut->portUnitOverrides[portKeyForOutput(0)] = "A";

    auto a = scinodes::analyzeUnits(g);
    EXPECT_TRUE(a.ok());
    EXPECT_TRUE(a.unitAt(nV->outputAttrId(0)).sameDimension(scinodes::units::kVolt));
}
static void test_override_pid_as_unit_transformer_closes_loop() {
    std::cout << "[141] PID con override in=rad, out=V cierra el lazo de control "
                 "sin conflicto R7\n";
    NodeGraph g;
    g.setDimensionalEnforcement(true);   // opt-in R7 para este test
    int s = g.addNode(NodeType::StepSignal);
    int sum = g.addNode(NodeType::Summation);
    int pid = g.addNode(NodeType::PIDController);
    int m = g.addNode(NodeType::DCMotorModel);

    // Override del PID: input "error" en rad, output actuación en V.
    auto* npid = g.findNode(pid);
    auto* npidMut = const_cast<NodeInstance*>(npid);
    npidMut->portUnitOverrides[portKeyForInput(0)]  = "rad";
    npidMut->portUnitOverrides[portKeyForOutput(0)] = "V";

    auto* ns = g.findNode(s);
    auto* nsum = g.findNode(sum);
    auto* nm = g.findNode(m);

    // Step → Sum:0; Sum.out → PID.in; PID.out → DCMotor.in
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(0),  nsum->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nsum->outputAttrId(0), npid->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(npid->outputAttrId(0), nm->inputAttrId(0)));
    // Step.out y Sum se pintan rad (backward desde PID.in).
    // DCMotor.in queda V (declarado en registry).  No hay conflict.

    auto a = scinodes::analyzeUnits(g);
    EXPECT_TRUE(a.ok());
    EXPECT_TRUE(a.unitAt(ns->outputAttrId(0)).sameDimension(scinodes::units::kRadian));
    EXPECT_TRUE(a.unitAt(npid->outputAttrId(0)).sameDimension(scinodes::units::kVolt));
}
// ---- Etapa 6H — conversores DegToRad/RadToDeg --------------------------
static void test_converter_degtorad_units_declared() {
    std::cout << "[143] DegToRad declara in=deg, out=rad\n";
    auto& def = nodeRegistry().at(NodeType::DegToRad);
    EXPECT_TRUE(hasDeclaredInputUnit(def, 0));
    EXPECT_TRUE(hasDeclaredOutputUnit(def, 0));
    EXPECT_TRUE(inputPortUnitOf(def, 0).sameDimension(scinodes::units::kDegree));
    EXPECT_TRUE(outputPortUnitOf(def, 0).sameDimension(scinodes::units::kRadian));
}
static void test_converter_radtodeg_units_declared() {
    std::cout << "[144] RadToDeg declara in=rad, out=deg\n";
    auto& def = nodeRegistry().at(NodeType::RadToDeg);
    EXPECT_TRUE(inputPortUnitOf(def, 0).sameDimension(scinodes::units::kRadian));
    EXPECT_TRUE(outputPortUnitOf(def, 0).sameDimension(scinodes::units::kDegree));
}
static void test_converter_codegen_emits_factor() {
    std::cout << "[145] CodeGen DegToRad/RadToDeg emiten factor constante correcto\n";
    NodeGraph g;
    int step = g.addNode(NodeType::StepSignal);
    int deg2rad = g.addNode(NodeType::DegToRad);
    int rad2deg = g.addNode(NodeType::RadToDeg);
    int osc1 = g.addNode(NodeType::Oscilloscope);
    int osc2 = g.addNode(NodeType::Oscilloscope);
    auto* ns = g.findNode(step);
    auto* nd = g.findNode(deg2rad);
    auto* nr = g.findNode(rad2deg);
    auto* no1 = g.findNode(osc1);
    auto* no2 = g.findNode(osc2);
    g.tryAddEdge(ns->outputAttrId(0), nd->inputAttrId(0));
    g.tryAddEdge(nd->outputAttrId(0), no1->inputAttrId(0));
    g.tryAddEdge(ns->outputAttrId(0), nr->inputAttrId(0));
    g.tryAddEdge(nr->outputAttrId(0), no2->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // π/180 ≈ 0.0174533 — el script debe contener este factor.
    EXPECT_TRUE(plan.script.find("0.017453") != std::string::npos);
    // 180/π ≈ 57.2958 — el otro factor.
    EXPECT_TRUE(plan.script.find("57.295") != std::string::npos);
}
static void test_converter_r7_accepts_explicit_conversion() {
    std::cout << "[146] R7 ON: deg signal → DegToRad → rad signal sin conflict\n";
    NodeGraph g;
    g.setDimensionalEnforcement(true);
    int step = g.addNode(NodeType::StepSignal);
    int conv = g.addNode(NodeType::DegToRad);
    auto* ns = g.findNode(step);
    auto* nc = g.findNode(conv);
    // Pin Step.out a deg via override per-instance.
    const_cast<NodeInstance*>(ns)->portUnitOverrides[portKeyForOutput(0)] = "deg";

    // Step (deg) → DegToRad.in (deg)  — match dimensional (ambos
    // dimensionless con magnitud distinta; sameDimension los acepta).
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(0), nc->inputAttrId(0)));
}

static void test_override_serialization_roundtrip() {
    std::cout << "[142] Round-trip de portUnitOverrides via ScnSerializer\n";
    NodeGraph g1;
    int s = g1.addNode(NodeType::StepSignal);
    auto* ns = g1.findNode(s);
    auto* nsMut = const_cast<NodeInstance*>(ns);
    nsMut->portUnitOverrides[portKeyForOutput(0)] = "V";

    ScnPositions pos1;
    std::string text = ScnSerializer::serialize(g1, pos1);

    NodeGraph g2;
    ScnPositions pos2;
    auto report = ScnSerializer::deserialize(text, g2, pos2);
    EXPECT_TRUE(report.ok);

    const auto& nodes = g2.nodes();
    EXPECT_TRUE(nodes.size() == 1);
    EXPECT_TRUE(nodes[0].portUnitOverrides.count(portKeyForOutput(0)) == 1);
    auto it = nodes[0].portUnitOverrides.find(portKeyForOutput(0));
    EXPECT_TRUE(it->second == "V");
}

static void test_override_rad_preserves_text_through_serialization() {
    std::cout << "[142b] Round-trip de 'rad' (dimensionless en SI) NO se pierde\n";
    // El bug que motivó el storage-as-text: `rad` tiene
    // toCanonicalString() == "" porque es adimensional × 1.0; con el
    // storage como `Unit` el round-trip lo perdía silenciosamente.
    NodeGraph g1;
    int s = g1.addNode(NodeType::StepSignal);
    auto* ns = g1.findNode(s);
    const_cast<NodeInstance*>(ns)->portUnitOverrides[portKeyForOutput(0)] = "rad";

    ScnPositions pos1;
    std::string text = ScnSerializer::serialize(g1, pos1);
    // Verificación cruda: el .scn debe contener "rad" en el slot, no "".
    EXPECT_TRUE(text.find("\"rad\"") != std::string::npos);

    NodeGraph g2;
    ScnPositions pos2;
    auto report = ScnSerializer::deserialize(text, g2, pos2);
    EXPECT_TRUE(report.ok);
    const auto& nodes = g2.nodes();
    auto it = nodes[0].portUnitOverrides.find(portKeyForOutput(0));
    EXPECT_TRUE(it != nodes[0].portUnitOverrides.end());
    EXPECT_TRUE(it->second == "rad");
}

static void test_r7_default_off_lets_through_conflict() {
    std::cout << "[138] R7 default OFF: edges con conflicto se aceptan, analyzeUnits los detecta post-hoc\n";
    NodeGraph g;   // default OFF
    int I = g.addNode(NodeType::CurrentSource);
    int M = g.addNode(NodeType::DCMotorModel);
    auto* nI = g.findNode(I);
    auto* nM = g.findNode(M);
    auto err = g.tryAddEdge(nI->outputAttrId(0), nM->inputAttrId(0));
    EXPECT_VALID(err);  // sin R7 enforcement, el edge pasa
    auto a = scinodes::analyzeUnits(g);
    EXPECT_FALSE(a.ok());  // pero el analyzer reporta el conflicto
}

static void test_registry_gear_transmission_radps() {
    std::cout << "[127] GearTransmission: input y output [rad/s]\n";
    auto& def = nodeRegistry().at(NodeType::GearTransmission);
    EXPECT_TRUE(hasDeclaredInputUnit(def, 0));
    EXPECT_TRUE(hasDeclaredOutputUnit(def, 0));
    EXPECT_TRUE(inputPortUnitOf(def, 0).sameDimension(scinodes::units::kRadianPerSec));
}

static void test_unit_toString_roundtrip_through_parser() {
    std::cout << "[122] Round-trip parseUnit(toCanonicalString(u)).unit == u (named)\n";
    // Si toCanonicalString produce un símbolo del parser, el round-trip
    // debe recuperar el mismo Unit.  Verificamos con los nombrados.
    for (const auto& u : {
            scinodes::units::kVolt,    scinodes::units::kWatt,
            scinodes::units::kJoule,   scinodes::units::kNewton,
            scinodes::units::kHertz,   scinodes::units::kPascal,
            scinodes::units::kOhm,     scinodes::units::kMeter,
            scinodes::units::kKilogram, scinodes::units::kSecond,
            scinodes::units::kAmpere,  scinodes::units::kKelvin }) {
        std::string s = u.toCanonicalString();
        auto re = parseUnit(s);
        EXPECT_TRUE(re.ok());
        EXPECT_TRUE(re.unit == u);
    }
}

static void test_unit_equality_distinguishes_magnitude() {
    std::cout << "[91] operator== distingue 100cm vs 1m (misma dim, distinta mag)\n";
    Unit cm = unitWithPrefix(unitMeter(), 0.01);
    Unit m  = unitMeter();
    EXPECT_TRUE (cm.sameDimension(m));  // R7 las aceptaría
    EXPECT_FALSE(cm == m);              // strict equality NO
}

static void test_typeexpr_vec_size_irrelevant_to_color() {
    std::cout << "[69] pinColorFromType: vec(2) y vec(3) comparten color (familia 'vector')\n";
    // vec(N) genérico — color violeta sin importar la N.  La forma
    // específica se distingue por shape, no por color.
    EXPECT_TRUE(pinColorFromType(exprVec(2)) == pinColorFromType(exprVec(3)));
    EXPECT_TRUE(pinColorFromType(exprVec(7)) == pinColorFromType(exprVec(3)));
}

static void test_graph_r6_via_tryAddEdge() {
    std::cout << "[10g] NodeGraph::tryAddEdge propaga port indices al validador (R6)\n";
    NodeGraph g;
    int s  = g.addNode(NodeType::SineSignal);
    int xf = g.addNode(NodeType::TransformObject);
    // Pointers se obtienen DESPUÉS de los addNode: addNode invalida
    // punteros previos al vector m_nodes cuando éste resfacclea.
    auto* ns = g.findNode(s);
    auto* nx = g.findNode(xf);
    // SineSignal (Signal out) → TransformObject port 0 (Geometry in)
    // viola R6 — NodeGraph debe pasar las port indices correctas al
    // parser para que la regla dispare.
    auto err = g.tryAddEdge(ns->outputAttrId(0), nx->inputAttrId(0));
    EXPECT_RULE(err, "R6");
}

// -----------------------------------------------------------------------
// Section 2 — Graph-level validation (NodeGraph)
// -----------------------------------------------------------------------
static void test_graph_empty() {
    std::cout << "[11] Empty graph\n";
    NodeGraph g;
    EXPECT_TRUE(g.grammarState() == GrammarState::Empty);
}
static void test_graph_incomplete_no_edges() {
    std::cout << "[12] Nodes but no edges → Incomplete\n";
    NodeGraph g;
    g.addNode(NodeType::SineSignal);
    g.addNode(NodeType::Oscilloscope);
    EXPECT_TRUE(g.grammarState() == GrammarState::Incomplete);
}
static void test_graph_valid_source_sink() {
    std::cout << "[13] Source → Sink direct → Valid\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int k = g.addNode(NodeType::Oscilloscope);
    auto ns = g.findNode(s); auto nk = g.findNode(k);
    auto err = g.tryAddEdge(ns->outputAttrId(), nk->inputAttrId(0));
    EXPECT_VALID(err);
    EXPECT_TRUE(g.grammarState() == GrammarState::Valid);
}
static void test_graph_valid_chain() {
    std::cout << "[14] Source → Transformer → Sink → Valid\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    int k = g.addNode(NodeType::Oscilloscope);
    auto ns = g.findNode(s); auto nt = g.findNode(t); auto nk = g.findNode(k);
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0)));
    EXPECT_TRUE(g.grammarState() == GrammarState::Valid);
}
static void test_graph_rejected_sink_to_tx() {
    std::cout << "[15] Rejected edge does not corrupt graph state\n";
    NodeGraph g;
    int k = g.addNode(NodeType::Oscilloscope);
    int t = g.addNode(NodeType::Gain);
    auto nk = g.findNode(k); auto nt = g.findNode(t);
    auto err = g.tryAddEdge(nk->outputAttrId(), nt->inputAttrId(0)); // Sink → Tx: invalid
    EXPECT_INVALID(err);
    EXPECT_TRUE(g.edgeCount() == 0);
    EXPECT_TRUE(g.grammarState() == GrammarState::Incomplete);
}

// -----------------------------------------------------------------------
// Section 3 — Undo / Redo via NodeGraph + UndoRedoStack
// -----------------------------------------------------------------------
static void test_undo_add_node() {
    std::cout << "[16] Undo add-node restores empty graph\n";
    NodeGraph g;
    UndoRedoStack h;

    auto before = g.snapshot();
    g.addNode(NodeType::SineSignal);
    h.record(before);

    EXPECT_TRUE(g.nodeCount() == 1);
    auto prev = h.undo(g.snapshot());
    EXPECT_TRUE(prev.has_value());
    g.restoreSnapshot(*prev);
    EXPECT_TRUE(g.nodeCount() == 0);
}
static void test_undo_add_edge() {
    std::cout << "[17] Undo add-edge removes it\n";
    NodeGraph g;
    UndoRedoStack h;

    int s = g.addNode(NodeType::SineSignal);
    int k = g.addNode(NodeType::Oscilloscope);

    auto before = g.snapshot();
    g.tryAddEdge(g.findNode(s)->outputAttrId(), g.findNode(k)->inputAttrId(0));
    h.record(before);

    EXPECT_TRUE(g.edgeCount() == 1);
    auto prev = h.undo(g.snapshot());
    g.restoreSnapshot(*prev);
    EXPECT_TRUE(g.edgeCount() == 0);
}
static void test_redo() {
    std::cout << "[18] Redo re-applies add-node\n";
    NodeGraph g;
    UndoRedoStack h;

    auto before = g.snapshot();
    g.addNode(NodeType::Gain);
    h.record(before);

    // Undo
    auto prev = h.undo(g.snapshot());
    g.restoreSnapshot(*prev);
    EXPECT_TRUE(g.nodeCount() == 0);

    // Redo
    auto next = h.redo(g.snapshot());
    g.restoreSnapshot(*next);
    EXPECT_TRUE(g.nodeCount() == 1);
}
static void test_undo_stack_limit() {
    std::cout << "[19] UndoRedoStack caps at MAX_DEPTH\n";
    UndoRedoStack h;
    NodeGraph g;
    for (int i = 0; i < UndoRedoStack::MAX_DEPTH + 10; ++i)
        h.record(g.snapshot());
    // Undo MAX_DEPTH times — the first 10 are gone
    int count = 0;
    while (h.canUndo()) { h.undo(g.snapshot()); ++count; }
    EXPECT_TRUE(count == UndoRedoStack::MAX_DEPTH);
}

// -----------------------------------------------------------------------
// Section 4 — ScnSerializer round-trip and load validation
// -----------------------------------------------------------------------
static void test_serializer_roundtrip() {
    std::cout << "[20] Serializer round-trip preserves graph\n";

    NodeGraph    g;
    ScnPositions pos;

    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(s, "Frequency", 7.5);
    g.setParam(t, "K", 2.25);

    auto* ns = g.findNode(s);
    auto* nt = g.findNode(t);
    auto* nk = g.findNode(k);
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0)));

    pos[s] = { 10.f, 20.f };
    pos[t] = { 110.f, 20.f };
    pos[k] = { 210.f, 20.f };

    std::string text = ScnSerializer::serialize(g, pos);

    NodeGraph    g2;
    ScnPositions pos2;
    LoadReport report = ScnSerializer::deserialize(text, g2, pos2);

    EXPECT_TRUE(report.ok);
    EXPECT_TRUE(report.nodesLoaded == 3);
    EXPECT_TRUE(report.edgesLoaded == 2);
    EXPECT_TRUE(report.rejectedEdges.empty());
    EXPECT_TRUE(report.finalState == GrammarState::Valid);

    EXPECT_TRUE(g2.nodeCount() == 3);
    EXPECT_TRUE(g2.edgeCount() == 2);

    // Param values survived
    auto* ns2 = g2.findNode(s);
    auto* nt2 = g2.findNode(t);
    EXPECT_TRUE(ns2 && std::fabs(ns2->params.at("Frequency") - 7.5) < 1e-9);
    EXPECT_TRUE(nt2 && std::fabs(nt2->params.at("K") - 2.25) < 1e-9);

    // Positions survived
    EXPECT_TRUE(pos2.size() == 3);
    EXPECT_TRUE(std::fabs(pos2[t].x - 110.f) < 1e-3f);
}

// ---- imported-object catalog (esquema 0.5) -------------------------------
static void test_serializer_objects_roundtrip() {
    std::cout << "[20a] Serializer: catálogo de objetos 3D sobrevive el round-trip\n";
    NodeGraph    g;
    ScnPositions pos;
    g.addImportedObject({ "Motor DC",
                          "examples/dc_motor/dc.gltf",
                          {"shaft","housing","terminals"} });
    g.addImportedObject({ "Encoder", "examples/enc.gltf", {} });

    std::string text = ScnSerializer::serialize(g, pos);

    NodeGraph    g2;
    ScnPositions pos2;
    LoadReport report = ScnSerializer::deserialize(text, g2, pos2);
    EXPECT_TRUE(report.ok);
    EXPECT_TRUE(g2.importedObjects().size() == 2);
    const auto* obj = g2.findImportedObject("Motor DC");
    EXPECT_TRUE(obj && obj->path == "examples/dc_motor/dc.gltf");
    EXPECT_TRUE(obj && obj->parts.size() == 3);
    EXPECT_TRUE(obj && obj->parts[1] == "housing");

    const auto* enc = g2.findImportedObject("Encoder");
    EXPECT_TRUE(enc && enc->parts.empty());
}
static void test_serializer_legacy_0_4_loads_empty_catalog() {
    std::cout << "[20b] Serializer: .scn versión 0.4 carga con catálogo vacío (no error)\n";
    const char* legacy = R"({
      "scnodes_version": "0.4",
      "next_node_id": 2,
      "nodes": [{"id":1, "type":"SineSignal", "position":[0,0], "params":{}}],
      "edges": []
    })";
    NodeGraph    g;
    ScnPositions pos;
    LoadReport report = ScnSerializer::deserialize(legacy, g, pos);
    EXPECT_TRUE(report.ok);
    // 0.4 ahora es legacy reconocido — NO debería figurar como version mismatch.
    EXPECT_TRUE(report.unknownTypes.empty());
    EXPECT_TRUE(g.importedObjects().empty());
}
static void test_serializer_objects_emitted_only_if_populated() {
    std::cout << "[20c] Serializer: 'objects' NO se emite si el catálogo está vacío\n";
    NodeGraph    g;
    ScnPositions pos;
    g.addNode(NodeType::SineSignal);
    std::string text = ScnSerializer::serialize(g, pos);
    EXPECT_TRUE(text.find("\"objects\"") == std::string::npos);
}
static void test_serializer_geometry_nodes_roundtrip() {
    std::cout << "[20e] Serializer: grafo Geometry (Object3D → TransformObject → SceneOutput) round-trip\n";
    NodeGraph    g;
    ScnPositions pos;
    int oId = g.addNode(NodeType::Object3D);
    int xId = g.addNode(NodeType::TransformObject);
    int sId = g.addNode(NodeType::SceneOutput);
    auto* no = g.findNode(oId);
    auto* nx = g.findNode(xId);
    auto* ns = g.findNode(sId);
    // Object3D.out → TransformObject.in[0] (Geometry→Geometry, OK)
    EXPECT_VALID(g.tryAddEdge(no->outputAttrId(0), nx->inputAttrId(0)));
    // TransformObject.out → SceneOutput.in[0] (Geometry→Geometry, OK)
    EXPECT_VALID(g.tryAddEdge(nx->outputAttrId(0), ns->inputAttrId(0)));

    std::string text = ScnSerializer::serialize(g, pos);

    NodeGraph    g2;
    ScnPositions pos2;
    LoadReport report = ScnSerializer::deserialize(text, g2, pos2);
    EXPECT_TRUE(report.ok);
    EXPECT_TRUE(report.nodesLoaded == 3);
    EXPECT_TRUE(report.edgesLoaded == 2);
    EXPECT_TRUE(report.rejectedEdges.empty());

    // Tipos correctos tras roundtrip
    EXPECT_TRUE(g2.findNode(oId) && g2.findNode(oId)->type == NodeType::Object3D);
    EXPECT_TRUE(g2.findNode(xId) && g2.findNode(xId)->type == NodeType::TransformObject);
    EXPECT_TRUE(g2.findNode(sId) && g2.findNode(sId)->type == NodeType::SceneOutput);
}
static void test_serializer_objects_addremove_dedup() {
    std::cout << "[20d] addImportedObject reemplaza por nombre; remove elimina\n";
    NodeGraph g;
    g.addImportedObject({ "Motor DC", "old.gltf", {"shaft"} });
    g.addImportedObject({ "Motor DC", "new.gltf", {"shaft","housing"} });
    EXPECT_TRUE(g.importedObjects().size() == 1);
    const auto* m = g.findImportedObject("Motor DC");
    EXPECT_TRUE(m && m->path == "new.gltf");
    EXPECT_TRUE(m && m->parts.size() == 2);

    g.removeImportedObject("Motor DC");
    EXPECT_TRUE(g.importedObjects().empty());
    EXPECT_TRUE(g.findImportedObject("Motor DC") == nullptr);
}

// ---- SceneCollector — walker del sub-grafo Geometry ---------------------
// Stub in-memory del resolver — devuelve un DeviceAsset fijo por nombre,
// sin tocar disco.  Permite testear collectScene en headless.
namespace {
class StubResolver : public scinodes::ISceneAssetResolver {
public:
    void put(const std::string& name, scinodes::DeviceAsset asset) {
        m_byName[name] = std::move(asset);
    }
    const scinodes::DeviceAsset* resolveByName(const std::string& name) const override {
        auto it = m_byName.find(name);
        return (it == m_byName.end()) ? nullptr : &it->second;
    }
private:
    std::unordered_map<std::string, scinodes::DeviceAsset> m_byName;
};

// Stub minimal de ISimSession — sólo implementa lectura de buffers
// para paso 5c.  El resto de métodos pure-virtual son no-ops o
// devuelven defaults; los tests del walker no los llaman.
class StubBridge : public scinodes::ISimSession {
public:
    void setSample(int sinkNodeId, float v, int channel = 0) {
        m_buffers[{sinkNodeId, channel}] = { v };
    }
    // ISimSession overrides
    bool reset(const NodeGraph&) override { return true; }
    bool reset(const NodeGraph&, const CodegenSeedState&) override { return true; }
    void stop() override {}
    void clearBuffers() override {}
    bool startSolverThread(float) override { return true; }
    void stopSolverThread() override {}
    void setPaused(bool) override {}
    bool isPaused() const override { return false; }
    bool captureState(CodegenSeedState&) override { return false; }
    bool sendParameter(const std::vector<int>&, int, double) override { return true; }
    Status status() const override { return Status::Ready; }
    const std::string& lastError() const override { return m_err; }
    float time() const override { return 0.f; }
    int offendingNodeId() const override { return 0; }
    bool isProducing() const override { return true; }
    std::vector<float> buffer(int sinkNodeId, int channel = 0) const override {
        auto it = m_buffers.find({sinkNodeId, channel});
        if (it == m_buffers.end()) return {};
        return it->second;
    }
    int writeIndex(int sinkNodeId, int channel = 0) const override {
        auto it = m_buffers.find({sinkNodeId, channel});
        return it == m_buffers.end() ? 0 : (int)it->second.size();
    }
    int channelCount(int) const override { return 1; }
    float solverDt() const override { return 0.01f; }
private:
    struct Key { int n; int c;
        bool operator==(const Key& o) const { return n == o.n && c == o.c; } };
    struct Hash { size_t operator()(const Key& k) const {
        return std::hash<int>()(k.n) ^ (std::hash<int>()(k.c) << 1); } };
    std::unordered_map<Key, std::vector<float>, Hash> m_buffers;
    std::string m_err;
};
}  // anon

static void test_scene_empty_graph_yields_no_renderables() {
    std::cout << "[40] collectScene: grafo vacío → 0 renderables\n";
    NodeGraph g;
    StubResolver r;
    EXPECT_TRUE(scinodes::collectScene(g, r).empty());
}
static void test_scene_object_into_scene_output_one_renderable() {
    std::cout << "[41] collectScene: Object3D → SceneOutput emite 1 renderable\n";
    NodeGraph g;
    int o = g.addNode(NodeType::Object3D);
    int s = g.addNode(NodeType::SceneOutput);
    auto* no = g.findNode(o);
    auto* ns = g.findNode(s);
    EXPECT_VALID(g.tryAddEdge(no->outputAttrId(0), ns->inputAttrId(0)));

    StubResolver r;
    scinodes::DeviceAsset motor;
    motor.path       = "fake.gltf";
    motor.deviceType = "DCMotor";
    r.put("Motor DC", std::move(motor));
    // El Object3D referencia "Motor DC" — el resolver responde.
    g.setStringParam(o, "objectRef", "Motor DC");

    auto items = scinodes::collectScene(g, r);
    EXPECT_TRUE(items.size() == 1);
    EXPECT_TRUE(items[0].asset != nullptr);
    EXPECT_TRUE(items[0].asset->deviceType == "DCMotor");
    EXPECT_TRUE(items[0].sourceObject3DId == o);
    EXPECT_TRUE(items[0].sinkId == s);
    EXPECT_TRUE(items[0].objectRef == "Motor DC");
    EXPECT_TRUE(items[0].partName.empty());
}
static void test_scene_objectref_with_part_splits() {
    std::cout << "[42] collectScene: 'objectName/part' separa name y partName\n";
    NodeGraph g;
    int o = g.addNode(NodeType::Object3D);
    int s = g.addNode(NodeType::SceneOutput);
    auto* no = g.findNode(o);
    auto* ns = g.findNode(s);
    g.tryAddEdge(no->outputAttrId(0), ns->inputAttrId(0));
    g.setStringParam(o, "objectRef", "Motor DC/shaft");

    StubResolver r;
    scinodes::DeviceAsset asset;
    r.put("Motor DC", std::move(asset));

    auto items = scinodes::collectScene(g, r);
    EXPECT_TRUE(items.size() == 1);
    EXPECT_TRUE(items[0].objectRef == "Motor DC/shaft");
    EXPECT_TRUE(items[0].partName  == "shaft");
}
static void test_scene_missing_catalog_entry_yields_null_asset() {
    std::cout << "[43] collectScene: objectRef no resuelto → renderable con asset=null\n";
    NodeGraph g;
    int o = g.addNode(NodeType::Object3D);
    int s = g.addNode(NodeType::SceneOutput);
    auto* no = g.findNode(o);
    auto* ns = g.findNode(s);
    g.tryAddEdge(no->outputAttrId(0), ns->inputAttrId(0));
    g.setStringParam(o, "objectRef", "no-existe");

    StubResolver r;   // resolver vacío
    auto items = scinodes::collectScene(g, r);
    // Devuelve el renderable como placeholder (asset=null) — la UI
    // decide pintar bounding-box.  Un null aquí NO es un error fatal.
    EXPECT_TRUE(items.size() == 1);
    EXPECT_TRUE(items[0].asset == nullptr);
    EXPECT_TRUE(items[0].objectRef == "no-existe");
}
static void test_scene_object_through_transform_to_output() {
    std::cout << "[44] collectScene: Object3D → TransformObject → SceneOutput acumula identidad\n";
    NodeGraph g;
    int o = g.addNode(NodeType::Object3D);
    int x = g.addNode(NodeType::TransformObject);
    int s = g.addNode(NodeType::SceneOutput);
    auto* no = g.findNode(o);
    auto* nx = g.findNode(x);
    auto* ns = g.findNode(s);
    EXPECT_VALID(g.tryAddEdge(no->outputAttrId(0), nx->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nx->outputAttrId(0), ns->inputAttrId(0)));

    StubResolver r;
    scinodes::DeviceAsset a; a.path = "x.gltf";
    r.put("X", std::move(a));
    g.setStringParam(o, "objectRef", "X");

    auto items = scinodes::collectScene(g, r);
    EXPECT_TRUE(items.size() == 1);
    // Paso 5a: TransformObject usa defaults (identity).  Paso 5b lo
    // reemplazará con lecturas del bridge.
    EXPECT_TRUE(std::fabs(items[0].rotation[0])    < 1e-9f);
    EXPECT_TRUE(std::fabs(items[0].translation[1]) < 1e-9f);
    EXPECT_TRUE(std::fabs(items[0].scale[2] - 1.f) < 1e-9f);
    EXPECT_TRUE(items[0].sourceObject3DId == o);
}
static void test_scene_two_objects_one_output() {
    std::cout << "[45] collectScene: dos Object3D al mismo SceneOutput emiten 2 renderables\n";
    NodeGraph g;
    int o1 = g.addNode(NodeType::Object3D);
    int o2 = g.addNode(NodeType::Object3D);
    int s  = g.addNode(NodeType::SceneOutput);
    auto* no1 = g.findNode(o1);
    auto* no2 = g.findNode(o2);
    auto* ns  = g.findNode(s);
    // SceneOutput tiene 1 input declarado, pero multi-input se hereda
    // del comportamiento de R5 sobre multi-input nodes; no se necesita
    // un workaround aquí — tryAddEdge para o2 falla con R5 a menos que
    // el catálogo expanda inputPorts.  Para el test, asumimos
    // arquitectura final: SceneOutput acepta N entradas vía un único
    // puerto.  Por ahora ESTE caso prueba el código del WALKER: si
    // hubiera dos aristas entrantes al mismo sink (no permitido por R5
    // hoy), el walker las recorrería ambas.
    //
    // Lo simulamos con DOS SceneOutput separados — cada uno con un
    // Object3D — y verificamos que el walker emita los dos items.
    int s2  = g.addNode(NodeType::SceneOutput);
    auto* ns2 = g.findNode(s2);
    EXPECT_VALID(g.tryAddEdge(no1->outputAttrId(0), ns->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(no2->outputAttrId(0), ns2->inputAttrId(0)));

    StubResolver r;
    scinodes::DeviceAsset a; r.put("X", std::move(a));
    g.setStringParam(o1, "objectRef", "X");
    g.setStringParam(o2, "objectRef", "X");

    auto items = scinodes::collectScene(g, r);
    EXPECT_TRUE(items.size() == 2);
    // sinkIds distintos (cada Object3D salió de su propio SceneOutput).
    EXPECT_TRUE(items[0].sinkId != items[1].sinkId);
}
// ---- Paso 5c + etapa 4: TransformObject lee transforms del bridge -------
static void test_scene_transform_reads_rotation_from_bridge_tap() {
    std::cout << "[47] collectScene: TransformObject port1(vec3) lee rotación via CombineXYZ + Sink tap\n";
    // Topología (post-etapa 4):
    //   Sine ─┬─→ Oscilloscope                       (tap)
    //         └─→ CombineXYZ:y ─→ TransformObject:1  (vec3)
    //   Object3D ─→ TransformObject:0 ─→ SceneOutput
    //
    // El bridge tiene un sample 1.57 en el Oscilloscope.  El walker
    // recorre TransformObject:1 → CombineXYZ.y → Sine → busca un Sink
    // tap (Oscilloscope) → lee 1.57.  rotation[1] = 1.57 (componente Y).
    NodeGraph g;
    int sineId  = g.addNode(NodeType::SineSignal);
    int oscId   = g.addNode(NodeType::Oscilloscope);
    int combId  = g.addNode(NodeType::CombineXYZ);
    int objId   = g.addNode(NodeType::Object3D);
    int xfId    = g.addNode(NodeType::TransformObject);
    int sceneId = g.addNode(NodeType::SceneOutput);

    auto* ns  = g.findNode(sineId);
    auto* no  = g.findNode(oscId);
    auto* nc  = g.findNode(combId);
    auto* nobj= g.findNode(objId);
    auto* nx  = g.findNode(xfId);
    auto* nsc = g.findNode(sceneId);

    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(0), no->inputAttrId(0)));   // tap
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(0), nc->inputAttrId(1)));   // y
    EXPECT_VALID(g.tryAddEdge(nc->outputAttrId(0), nx->inputAttrId(1)));   // vec3 → rot
    EXPECT_VALID(g.tryAddEdge(nobj->outputAttrId(0), nx->inputAttrId(0))); // geom
    EXPECT_VALID(g.tryAddEdge(nx->outputAttrId(0),   nsc->inputAttrId(0)));

    StubResolver r;
    scinodes::DeviceAsset a; r.put("X", std::move(a));
    g.setStringParam(objId, "objectRef", "X");

    StubBridge b;
    b.setSample(oscId, 1.57f);

    auto items = scinodes::collectScene(g, r, &b);
    EXPECT_TRUE(items.size() == 1);
    // rotation[1] (Y) = 1.57 vía CombineXYZ.y.  X/Z no cableados = 0.
    EXPECT_TRUE(std::fabs(items[0].rotation[0]) < 1e-9f);
    EXPECT_TRUE(std::fabs(items[0].rotation[1] - 1.57f) < 1e-5f);
    EXPECT_TRUE(std::fabs(items[0].rotation[2]) < 1e-9f);
}
static void test_scene_transform_without_tap_stays_identity() {
    std::cout << "[48] collectScene: TransformObject sin Sink tap → identidad\n";
    NodeGraph g;
    int sineId  = g.addNode(NodeType::SineSignal);
    int combId  = g.addNode(NodeType::CombineXYZ);
    int objId   = g.addNode(NodeType::Object3D);
    int xfId    = g.addNode(NodeType::TransformObject);
    int sceneId = g.addNode(NodeType::SceneOutput);

    auto* ns  = g.findNode(sineId);
    auto* nc  = g.findNode(combId);
    auto* nobj= g.findNode(objId);
    auto* nx  = g.findNode(xfId);
    auto* nsc = g.findNode(sceneId);

    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(0),  nc->inputAttrId(1)));
    EXPECT_VALID(g.tryAddEdge(nc->outputAttrId(0),  nx->inputAttrId(1)));
    EXPECT_VALID(g.tryAddEdge(nobj->outputAttrId(0),nx->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nx->outputAttrId(0),  nsc->inputAttrId(0)));

    StubResolver r;
    scinodes::DeviceAsset a; r.put("X", std::move(a));
    g.setStringParam(objId, "objectRef", "X");

    StubBridge b;  // sin samples
    auto items = scinodes::collectScene(g, r, &b);
    EXPECT_TRUE(items.size() == 1);
    EXPECT_TRUE(std::fabs(items[0].rotation[1]) < 1e-9f);
}
static void test_vec3_walker_reads_vec3_constant() {
    std::cout << "[71] collectScene: Vec3Constant aporta (x,y,z) al rotation del TransformObject\n";
    NodeGraph g;
    int vcId    = g.addNode(NodeType::Vec3Constant);
    int objId   = g.addNode(NodeType::Object3D);
    int xfId    = g.addNode(NodeType::TransformObject);
    int sceneId = g.addNode(NodeType::SceneOutput);
    g.setParam(vcId, "x", 0.1);
    g.setParam(vcId, "y", 0.2);
    g.setParam(vcId, "z", 0.3);

    auto* nv  = g.findNode(vcId);
    auto* nobj= g.findNode(objId);
    auto* nx  = g.findNode(xfId);
    auto* nsc = g.findNode(sceneId);

    EXPECT_VALID(g.tryAddEdge(nv->outputAttrId(0),   nx->inputAttrId(1)));
    EXPECT_VALID(g.tryAddEdge(nobj->outputAttrId(0), nx->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nx->outputAttrId(0),   nsc->inputAttrId(0)));

    StubResolver r;
    scinodes::DeviceAsset a; r.put("X", std::move(a));
    g.setStringParam(objId, "objectRef", "X");

    auto items = scinodes::collectScene(g, r);
    EXPECT_TRUE(items.size() == 1);
    EXPECT_TRUE(std::fabs(items[0].rotation[0] - 0.1f) < 1e-6f);
    EXPECT_TRUE(std::fabs(items[0].rotation[1] - 0.2f) < 1e-6f);
    EXPECT_TRUE(std::fabs(items[0].rotation[2] - 0.3f) < 1e-6f);
}

// ---- Etapa 6I.A — Quantity = (escalar × unidad) ------------------------
#include "../src/core/Quantity.hpp"
using scinodes::Quantity;
using scinodes::parseQuantity;
using scinodes::toDisplayString;

static void test_quantity_parse_number_with_unit() {
    std::cout << "[147] parseQuantity(\"1V\") = {1.0, V}\n";
    auto r = parseQuantity("1V");
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.quantity.value == 1.0);
    EXPECT_TRUE(r.quantity.unit.sameDimension(scinodes::units::kVolt));
}
static void test_quantity_parse_centimeters() {
    std::cout << "[148] parseQuantity(\"100cm\") preserva value=100 y "
                 "magnitud cm (toSI = 1.0 m)\n";
    auto r = parseQuantity("100cm");
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.quantity.value == 100.0);
    // El parser de unidades trata cm como meter × 1e-2.
    EXPECT_TRUE(r.quantity.unit.sameDimension(scinodes::units::kMeter));
    EXPECT_TRUE(std::fabs(r.quantity.toSI() - 1.0) < 1e-12);
}
static void test_quantity_parse_bare_number_is_dimensionless() {
    std::cout << "[149] parseQuantity(\"0.5\") = {0.5, dimensionless}\n";
    auto r = parseQuantity("0.5");
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.quantity.value == 0.5);
    EXPECT_TRUE(r.quantity.isDimensionless());
}
static void test_quantity_parse_bare_unit_implies_value_one() {
    std::cout << "[150] parseQuantity(\"V\") = {1.0, V} (convención Blender)\n";
    auto r = parseQuantity("V");
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.quantity.value == 1.0);
    EXPECT_TRUE(r.quantity.unit.sameDimension(scinodes::units::kVolt));
}
static void test_quantity_parse_with_space() {
    std::cout << "[151] parseQuantity(\".5 kg\") tolera whitespace y \".5\"\n";
    auto r = parseQuantity(".5 kg");
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.quantity.value == 0.5);
    EXPECT_TRUE(r.quantity.unit.sameDimension(scinodes::units::kKilogram));
}
static void test_quantity_parse_negative_signed() {
    std::cout << "[152] parseQuantity(\"-3 m/s\") = {-3, m/s}\n";
    auto r = parseQuantity("-3 m/s");
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.quantity.value == -3.0);
    // m/s = m × s⁻¹
    EXPECT_TRUE(r.quantity.unit.exp[0] == 1);
    EXPECT_TRUE(r.quantity.unit.exp[2] == -1);
}
static void test_quantity_parse_scientific_notation() {
    std::cout << "[153] parseQuantity(\"1e3 Hz\") = {1000, Hz}\n";
    auto r = parseQuantity("1e3 Hz");
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.quantity.value == 1000.0);
    EXPECT_TRUE(r.quantity.unit.sameDimension(scinodes::units::kHertz));
}
static void test_quantity_parse_compound_unit() {
    std::cout << "[154] parseQuantity(\"2.5 V/s\") = {2.5, V/s}\n";
    auto r = parseQuantity("2.5 V/s");
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.quantity.value == 2.5);
    // V/s: V tiene exp=(2,1,-3,-1,0,0,0); /s suma s^-1 → (2,1,-4,-1,0,0,0).
    EXPECT_TRUE(r.quantity.unit.exp[2] == -4);
}
static void test_quantity_parse_center_dot() {
    std::cout << "[155] parseQuantity(\"1 N\\xC2\\xB7m\") = {1, N·m} ≡ J dim\n";
    auto r = parseQuantity("1 N\xC2\xB7m");
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.quantity.value == 1.0);
    EXPECT_TRUE(r.quantity.unit.sameDimension(scinodes::units::kJoule));
}
static void test_quantity_parse_empty_is_error() {
    std::cout << "[156] parseQuantity(\"\") rechaza con error\n";
    auto r = parseQuantity("");
    EXPECT_FALSE(r.ok());
}
static void test_quantity_parse_unknown_unit_is_error() {
    std::cout << "[157] parseQuantity(\"1 xyz\") rechaza por unidad desconocida\n";
    auto r = parseQuantity("1 xyz");
    EXPECT_FALSE(r.ok());
}
static void test_quantity_equivalent_si_across_prefixes() {
    std::cout << "[158] {100, cm}.equivalentSI({1, m}) = true (misma longitud SI)\n";
    auto a = parseQuantity("100cm");
    auto b = parseQuantity("1m");
    EXPECT_TRUE(a.ok() && b.ok());
    EXPECT_TRUE(a.quantity.equivalentSI(b.quantity));
    EXPECT_FALSE(a.quantity == b.quantity);  // estrictamente distintos
}
static void test_quantity_equivalent_si_different_dim_is_false() {
    std::cout << "[159] equivalentSI rechaza dimensiones distintas (1m vs 1s)\n";
    auto a = parseQuantity("1m");
    auto b = parseQuantity("1s");
    EXPECT_TRUE(a.ok() && b.ok());
    EXPECT_FALSE(a.quantity.equivalentSI(b.quantity));
}
static void test_quantity_to_display_string() {
    std::cout << "[160] toDisplayString round-trips por canonicalización\n";
    auto q1 = parseQuantity("1V").quantity;
    EXPECT_TRUE(toDisplayString(q1) == "1 V");
    auto q2 = parseQuantity("0.5").quantity;
    EXPECT_TRUE(toDisplayString(q2) == "0.5");
    // 100cm canonicaliza a m con magnitude 0.01 → display = "100 cm".
    auto q3 = parseQuantity("100cm").quantity;
    EXPECT_TRUE(toDisplayString(q3).find("100") != std::string::npos);
    EXPECT_TRUE(toDisplayString(q3).find("cm") != std::string::npos);
}
static void test_quantity_to_si_with_kilo_prefix() {
    std::cout << "[161] {2, kV}.toSI() = 2000 (kV magnitude = 1e3)\n";
    auto r = parseQuantity("2kV");
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(std::fabs(r.quantity.toSI() - 2000.0) < 1e-9);
}
static void test_quantity_round_trip_all_si_prefixes() {
    std::cout << "[162] Round-trip de TODOS los prefijos SI: parseQuantity → "
                 "toDisplayString preserva el prefijo\n";
    // Espejar la tabla del parser de Unit.cpp.  Si parseUnit acepta el
    // prefijo, toCanonicalString debe poder emitirlo de vuelta — sin
    // eso el display cae a (×factor) y rompe la promesa "lo que tipeás
    // es lo que ves".
    struct C { const char* in; const char* contains; };
    const C cases[] = {
        {"1Ym",  "Ym"},  {"1Zm",  "Zm"},  {"1Em",  "Em"},  {"1Pm",  "Pm"},
        {"1Tm",  "Tm"},  {"1Gm",  "Gm"},  {"1Mm",  "Mm"},  {"1km",  "km"},
        {"1hm",  "hm"},  {"1dam", "dam"},
        {"1dm",  "dm"},  {"1cm",  "cm"},  {"1mm",  "mm"},
        {"1μm",  "μm"},  {"1nm",  "nm"},  {"1pm",  "pm"},
        {"1fm",  "fm"},  {"1am",  "am"},  {"1zm",  "zm"},  {"1ym",  "ym"},
        // Mismo con volt (verifica que no es exclusivo del meter)
        {"3.3kV",  "kV"}, {"5mV",   "mV"}, {"100μV", "μV"},
    };
    for (const auto& c : cases) {
        auto r = parseQuantity(c.in);
        EXPECT_TRUE(r.ok());
        const std::string disp = toDisplayString(r.quantity);
        if (disp.find(c.contains) == std::string::npos) {
            std::cout << "    " << c.in << " → '" << disp
                      << "' (no contiene '" << c.contains << "')\n";
        }
        EXPECT_TRUE(disp.find(c.contains) != std::string::npos);
    }
}
static void test_quantity_parse_has_unit_flag() {
    std::cout << "[163b] hasUnit distingue \"5\" (false) de \"5V\" (true)\n";
    EXPECT_FALSE(parseQuantity("5").hasUnit);
    EXPECT_FALSE(parseQuantity("0.5").hasUnit);
    EXPECT_FALSE(parseQuantity("1e3").hasUnit);
    EXPECT_TRUE(parseQuantity("5V").hasUnit);
    EXPECT_TRUE(parseQuantity("5 V").hasUnit);
    EXPECT_TRUE(parseQuantity("V").hasUnit);          // unit sola
    EXPECT_TRUE(parseQuantity("100cm").hasUnit);
}
static void test_quantity_si_value_independent_of_typed_prefix() {
    std::cout << "[163] toSI() converge al mismo número para distintos "
                 "prefijos del mismo valor físico\n";
    // 100 cm == 1 m == 1000 mm == 1e6 μm ==... → toSI todos = 1.0 m
    auto a = parseQuantity("100cm").quantity;
    auto b = parseQuantity("1m").quantity;
    auto c = parseQuantity("1000mm").quantity;
    auto d = parseQuantity("1e6 μm").quantity;
    auto e = parseQuantity("1e9 nm").quantity;
    EXPECT_TRUE(std::fabs(a.toSI() - 1.0) < 1e-9);
    EXPECT_TRUE(std::fabs(b.toSI() - 1.0) < 1e-9);
    EXPECT_TRUE(std::fabs(c.toSI() - 1.0) < 1e-9);
    EXPECT_TRUE(std::fabs(d.toSI() - 1.0) < 1e-9);
    EXPECT_TRUE(std::fabs(e.toSI() - 1.0) < 1e-9);
    EXPECT_TRUE(a.equivalentSI(b));
    EXPECT_TRUE(b.equivalentSI(c));
    EXPECT_TRUE(c.equivalentSI(d));
}

// ---- Etapa 6I.B — Field + FieldDef + synthesizeFields ------------------
#include "../src/core/Field.hpp"
using scinodes::FieldDef;
using scinodes::FieldKind;
using scinodes::synthesizeFields;

static const FieldDef* findField(const std::vector<FieldDef>& fs,
                                  const std::string& name) {
    for (const auto& f : fs)
        if (f.name == name) return &f;
    return nullptr;
}

static void test_fields_order_inputs_params_outputs() {
    std::cout << "[164] synthesizeFields ordena inputs → params → outputs\n";
    const auto& def = nodeRegistry().at(NodeType::PIDController);
    auto fs = synthesizeFields(def);
    EXPECT_TRUE((int)fs.size() ==
                def.inputPorts + (int)def.params.size() + def.outputPorts);
    int idx = 0;
    for (int i = 0; i < def.inputPorts; ++i, ++idx)
        EXPECT_TRUE(fs[idx].kind == FieldKind::Input);
    for (size_t i = 0; i < def.params.size(); ++i, ++idx)
        EXPECT_TRUE(fs[idx].kind == FieldKind::Parameter);
    for (int i = 0; i < def.outputPorts; ++i, ++idx)
        EXPECT_TRUE(fs[idx].kind == FieldKind::Output);
}
static void test_fields_voltage_source_declares_volts() {
    std::cout << "[165] VoltageSource.out es Field con unit=V declarada\n";
    const auto& def = nodeRegistry().at(NodeType::VoltageSource);
    auto fs = synthesizeFields(def);
    const FieldDef* out = findField(fs, "out0");
    EXPECT_TRUE(out != nullptr);
    EXPECT_TRUE(out->kind == FieldKind::Output);
    EXPECT_FALSE(out->polymorphic);
    EXPECT_TRUE(out->defaultQuantity.unit.sameDimension(scinodes::units::kVolt));
}
static void test_fields_dc_motor_declares_in_v_out_radps() {
    std::cout << "[166] DCMotorModel: in0=V declarado, out0=rad/s declarado\n";
    const auto& def = nodeRegistry().at(NodeType::DCMotorModel);
    auto fs = synthesizeFields(def);
    const FieldDef* in0  = findField(fs, "in0");
    const FieldDef* out0 = findField(fs, "out0");
    EXPECT_TRUE(in0  != nullptr && out0 != nullptr);
    EXPECT_FALSE(in0->polymorphic);
    EXPECT_FALSE(out0->polymorphic);
    EXPECT_TRUE(in0->defaultQuantity.unit.sameDimension(scinodes::units::kVolt));
    EXPECT_TRUE(out0->defaultQuantity.unit.sameDimension(
        scinodes::units::kRadianPerSec));
}
static void test_fields_pid_inputs_are_polymorphic() {
    std::cout << "[167] PID: in0, in1, out0 marcan polymorphic=true\n";
    const auto& def = nodeRegistry().at(NodeType::PIDController);
    auto fs = synthesizeFields(def);
    EXPECT_TRUE(findField(fs, "in0")->polymorphic);
    EXPECT_TRUE(findField(fs, "in1")->polymorphic);
    EXPECT_TRUE(findField(fs, "out0")->polymorphic);
}
static void test_fields_pid_params_have_default_quantity() {
    std::cout << "[168] PID.Kp es Field Parameter con default = registry value\n";
    const auto& def = nodeRegistry().at(NodeType::PIDController);
    auto fs = synthesizeFields(def);
    const FieldDef* kp = findField(fs, "Kp");
    EXPECT_TRUE(kp != nullptr);
    EXPECT_TRUE(kp->kind == FieldKind::Parameter);
    EXPECT_TRUE(kp->defaultQuantity.value == def.params[0].defaultValue);
    EXPECT_TRUE(isScalarType(kp->type));
}
static void test_fields_transform_object_geometry_field_is_geometry_type() {
    std::cout << "[169] TransformObject.in0 es Field con type=Geometry\n";
    const auto& def = nodeRegistry().at(NodeType::TransformObject);
    auto fs = synthesizeFields(def);
    const FieldDef* geom = findField(fs, "in0");
    EXPECT_TRUE(geom != nullptr);
    EXPECT_TRUE(isGeometryType(geom->type));
}
static void test_fields_dc_motor_param_units_parse_from_string() {
    std::cout << "[170] DCMotor.Ra es Field Parameter con unit=Ohm parseada\n";
    const auto& def = nodeRegistry().at(NodeType::DCMotorModel);
    auto fs = synthesizeFields(def);
    const FieldDef* ra = findField(fs, "Ra");
    EXPECT_TRUE(ra != nullptr);
    EXPECT_TRUE(ra->kind == FieldKind::Parameter);
    EXPECT_FALSE(ra->polymorphic);
    // Ω y Ohm comparten dimensión (V/A).
    auto ohm = scinodes::parseUnit("Ohm");
    EXPECT_TRUE(ohm.ok());
    EXPECT_TRUE(ra->defaultQuantity.unit.sameDimension(ohm.unit));
}
static void test_fields_step_signal_polymorphic_output() {
    std::cout << "[171] StepSignal.out0 es Field polymorphic (sin declaración)\n";
    const auto& def = nodeRegistry().at(NodeType::StepSignal);
    auto fs = synthesizeFields(def);
    const FieldDef* out = findField(fs, "out0");
    EXPECT_TRUE(out != nullptr);
    EXPECT_TRUE(out->polymorphic);
}
static void test_fields_label_falls_back_to_index() {
    std::cout << "[172] Field.label cae a \"in N\" / \"out N\" cuando no hay "
                 "label declarado\n";
    const auto& def = nodeRegistry().at(NodeType::Summation);
    auto fs = synthesizeFields(def);
    const FieldDef* in0 = findField(fs, "in0");
    EXPECT_TRUE(in0 != nullptr);
    // Summation no declara labels específicos para sus inputs.
    EXPECT_TRUE(in0->label == "in 1");
}

// ---- Etapa 6I.C — displayUnits del proyecto ----------------------------
static void test_display_units_default_empty() {
    std::cout << "[173] NodeGraph nuevo arranca sin preferencias de display\n";
    NodeGraph g;
    EXPECT_TRUE(g.displayUnits().empty());
}
static void test_display_units_set_and_query() {
    std::cout << "[174] setDisplayUnit indexa por dim signature\n";
    NodeGraph g;
    g.setDisplayUnit(scinodes::units::kMeter);
    g.setDisplayUnit(scinodes::units::kVolt);
    EXPECT_TRUE(g.displayUnits().size() == 2);
    EXPECT_TRUE(g.displayUnits().count(scinodes::units::kMeter.exp) == 1);
    EXPECT_TRUE(g.displayUnits().count(scinodes::units::kVolt.exp) == 1);
}
static void test_display_units_replaces_same_dimension() {
    std::cout << "[175] setDisplayUnit(km) reemplaza setDisplayUnit(m) "
                 "(misma dim, distinto magnitude)\n";
    NodeGraph g;
    g.setDisplayUnit(scinodes::units::kMeter);   // 1 m
    auto km = scinodes::parseUnit("km").unit;
    g.setDisplayUnit(km);
    EXPECT_TRUE(g.displayUnits().size() == 1);
    auto it = g.displayUnits().find(km.exp);
    EXPECT_TRUE(it != g.displayUnits().end());
    EXPECT_TRUE(std::fabs(it->second.magnitude - 1e3) < 1e-12);
}
static void test_display_units_clear() {
    std::cout << "[176] clearDisplayUnit borra por dim signature\n";
    NodeGraph g;
    g.setDisplayUnit(scinodes::units::kMeter);
    g.setDisplayUnit(scinodes::units::kVolt);
    g.clearDisplayUnit(scinodes::units::kMeter.exp);
    EXPECT_TRUE(g.displayUnits().size() == 1);
    EXPECT_TRUE(g.displayUnits().count(scinodes::units::kVolt.exp) == 1);
}
static void test_display_units_canonicalize_converts_value() {
    std::cout << "[177] canonicalizeForDisplay convierte 100cm → 1m si "
                 "displayUnits.length = m\n";
    NodeGraph g;
    g.setDisplayUnit(scinodes::units::kMeter);
    auto q100cm = scinodes::parseQuantity("100cm").quantity;
    auto disp   = g.canonicalizeForDisplay(q100cm);
    EXPECT_TRUE(std::fabs(disp.value - 1.0) < 1e-9);
    EXPECT_TRUE(disp.unit == scinodes::units::kMeter);
}
static void test_display_units_canonicalize_no_preference_unchanged() {
    std::cout << "[178] canonicalizeForDisplay sin entry devuelve Quantity intacto\n";
    NodeGraph g;
    auto q = scinodes::parseQuantity("3.3kV").quantity;
    auto disp = g.canonicalizeForDisplay(q);
    EXPECT_TRUE(disp == q);
}
static void test_display_units_canonicalize_to_kilo_volts() {
    std::cout << "[179] canonicalizeForDisplay convierte 5000V → 5kV "
                 "si displayUnits.voltage = kV\n";
    NodeGraph g;
    auto kV = scinodes::parseUnit("kV").unit;
    g.setDisplayUnit(kV);
    auto q5000V = scinodes::parseQuantity("5000V").quantity;
    auto disp   = g.canonicalizeForDisplay(q5000V);
    EXPECT_TRUE(std::fabs(disp.value - 5.0) < 1e-9);
    EXPECT_TRUE(disp.unit == kV);
}
static void test_display_units_serialization_roundtrip() {
    std::cout << "[180] Round-trip de display_units por .scn\n";
    NodeGraph g1;
    g1.setDisplayUnit(scinodes::units::kMeter);
    g1.setDisplayUnit(scinodes::units::kVolt);
    g1.setDisplayUnit(scinodes::units::kHertz);
    ScnPositions pos1;
    std::string text = ScnSerializer::serialize(g1, pos1);
    EXPECT_TRUE(text.find("display_units") != std::string::npos);

    NodeGraph g2;
    ScnPositions pos2;
    auto report = ScnSerializer::deserialize(text, g2, pos2);
    EXPECT_TRUE(report.ok);
    EXPECT_TRUE(g2.displayUnits().size() == 3);
    EXPECT_TRUE(g2.displayUnits().count(scinodes::units::kMeter.exp) == 1);
    EXPECT_TRUE(g2.displayUnits().count(scinodes::units::kVolt.exp) == 1);
    EXPECT_TRUE(g2.displayUnits().count(scinodes::units::kHertz.exp) == 1);
}
static void test_display_units_idempotent_when_already_canonical() {
    std::cout << "[181] canonicalizeForDisplay sobre Quantity ya en la unidad "
                 "preferida no toca el valor\n";
    NodeGraph g;
    g.setDisplayUnit(scinodes::units::kMeter);
    auto q1m = scinodes::parseQuantity("1m").quantity;
    auto disp = g.canonicalizeForDisplay(q1m);
    EXPECT_TRUE(disp.value == 1.0);
    EXPECT_TRUE(disp.unit == scinodes::units::kMeter);
}

// ---- Etapa 6I.D.1 — NodeInstance::fields (Quantity) ---------------------
static void test_node_instance_seeds_fields_from_def() {
    std::cout << "[182] makeNode(PIDController) siembra `fields` con inputs, "
                 "params y outputs declarados\n";
    NodeGraph g;
    int id = g.addNode(NodeType::PIDController);
    const auto* n = g.findNode(id);
    EXPECT_TRUE(n != nullptr);
    // Defs: 2 inputs (in0, in1), 5 params (Kp, Ki, Kd, ...), 1 output (out0).
    const auto& def = nodeRegistry().at(NodeType::PIDController);
    const size_t expected = def.inputPorts + def.params.size() + def.outputPorts;
    EXPECT_TRUE(n->fields.size() == expected);
    EXPECT_TRUE(n->fields.count("in0") == 1);
    EXPECT_TRUE(n->fields.count("Kp")  == 1);
    EXPECT_TRUE(n->fields.count("out0") == 1);
}
static void test_node_instance_field_value_matches_param() {
    std::cout << "[183] fields[name].value == params[name] tras la construcción\n";
    NodeGraph g;
    int id = g.addNode(NodeType::DCMotorModel);
    const auto* n = g.findNode(id);
    EXPECT_TRUE(n->params.at("Ra")  == n->fields.at("Ra").value);
    EXPECT_TRUE(n->params.at("La")  == n->fields.at("La").value);
}
static void test_node_instance_field_unit_from_registry() {
    std::cout << "[184] fields[Ra].unit es Ohm (sembrada desde FieldDef)\n";
    NodeGraph g;
    int id = g.addNode(NodeType::DCMotorModel);
    const auto* n = g.findNode(id);
    auto ohm = scinodes::parseUnit("Ohm");
    EXPECT_TRUE(ohm.ok());
    EXPECT_TRUE(n->fields.at("Ra").unit.sameDimension(ohm.unit));
}
static void test_node_instance_setparam_mirrors_to_fields() {
    std::cout << "[185] setParam(Kp, 5.0) actualiza fields[Kp].value\n";
    NodeGraph g;
    int id = g.addNode(NodeType::PIDController);
    g.setParam(id, "Kp", 5.0);
    const auto* n = g.findNode(id);
    EXPECT_TRUE(n->params.at("Kp")        == 5.0);
    EXPECT_TRUE(n->fields.at("Kp").value  == 5.0);
}
static void test_node_instance_setparam_preserves_unit() {
    std::cout << "[186] setParam NO toca la unidad sembrada\n";
    NodeGraph g;
    int id = g.addNode(NodeType::DCMotorModel);
    const auto* n = g.findNode(id);
    const auto oldUnit = n->fields.at("Ra").unit;
    g.setParam(id, "Ra", 47.0);
    EXPECT_TRUE(n->fields.at("Ra").value == 47.0);
    EXPECT_TRUE(n->fields.at("Ra").unit  == oldUnit);
}
static void test_oscilloscope_unit_inferred_from_source() {
    std::cout << "[187a] El analyzer entrega la unidad del puerto Oscilloscope "
                 "inferida via el source\n";
    NodeGraph g;
    int v = g.addNode(NodeType::VoltageSource);     // out0 = V (declarado)
    int o = g.addNode(NodeType::Oscilloscope);      // 8 inputs polimórficos
    auto* nv = g.findNode(v);
    auto* no = g.findNode(o);
    auto err = g.tryAddEdge(nv->outputAttrId(0), no->inputAttrId(0));
    EXPECT_VALID(err);

    auto a = scinodes::analyzeUnits(g);
    EXPECT_TRUE(a.ok());
    const int inAttr = no->inputAttrId(0);
    EXPECT_TRUE(a.isResolved(inAttr));
    EXPECT_TRUE(a.unitAt(inAttr).sameDimension(scinodes::units::kVolt));
}
static void test_oscilloscope_unit_from_dc_motor_is_radps() {
    std::cout << "[187a2] DCMotor.out (rad/s) → Oscilloscope.in: unidad "
                 "resolvida es Hz (≡ rad/s en SI)\n";
    NodeGraph g;
    int v = g.addNode(NodeType::VoltageSource);
    int m = g.addNode(NodeType::DCMotorModel);
    int o = g.addNode(NodeType::Oscilloscope);
    auto* nv = g.findNode(v); auto* nm = g.findNode(m); auto* no = g.findNode(o);
    EXPECT_VALID(g.tryAddEdge(nv->outputAttrId(0), nm->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nm->outputAttrId(0), no->inputAttrId(0)));

    auto a = scinodes::analyzeUnits(g);
    EXPECT_TRUE(a.ok());
    const int inAttr = no->inputAttrId(0);
    EXPECT_TRUE(a.isResolved(inAttr));
    // rad/s y Hz comparten dimensión SI (s⁻¹).
    EXPECT_TRUE(a.unitAt(inAttr).sameDimension(
        scinodes::units::kRadianPerSec));
}
static void test_integrator_multiplies_unit_by_seconds() {
    std::cout << "[188] Integrator: in=rad/s → out=rad (phantom angle dim)\n";
    NodeGraph g;
    int v = g.addNode(NodeType::VoltageSource);
    int m = g.addNode(NodeType::DCMotorModel);
    int integ = g.addNode(NodeType::Integrator);
    auto* nv = g.findNode(v); auto* nm = g.findNode(m); auto* ni = g.findNode(integ);
    g.tryAddEdge(nv->outputAttrId(0), nm->inputAttrId(0));
    g.tryAddEdge(nm->outputAttrId(0), ni->inputAttrId(0));

    auto a = scinodes::analyzeUnits(g);
    EXPECT_TRUE(a.ok());
    EXPECT_TRUE(a.unitAt(nm->outputAttrId(0)).sameDimension(
        scinodes::units::kRadianPerSec));
    // Etapa 6I.L: out es kRadian (exp[7]=1), distinguible de adimensional.
    EXPECT_TRUE(a.isResolved(ni->outputAttrId(0)));
    auto outU = a.unitAt(ni->outputAttrId(0));
    EXPECT_TRUE(outU.sameDimension(scinodes::units::kRadian));
}
static void test_integrator_propagates_backward() {
    std::cout << "[189] Integrator.out=V·s  →  in=V (backward via factor)\n";
    NodeGraph g;
    int integ = g.addNode(NodeType::Integrator);
    auto* ni = g.findNode(integ);
    // Forzar la unidad de salida vía override per-instance: Vs.
    const_cast<NodeInstance*>(ni)->portUnitOverrides[
        portKeyForOutput(0)] = "V·s";

    auto a = scinodes::analyzeUnits(g);
    EXPECT_TRUE(a.isResolved(ni->inputAttrId(0)));
    EXPECT_TRUE(a.unitAt(ni->inputAttrId(0)).sameDimension(
        scinodes::units::kVolt));
}
static void test_differentiator_divides_unit_by_seconds() {
    std::cout << "[190] Differentiator: in=rad  →  out=rad/s\n";
    NodeGraph g;
    int v = g.addNode(NodeType::VoltageSource);    // V dimensional
    int diff = g.addNode(NodeType::Differentiator);
    auto* nv = g.findNode(v); auto* nd = g.findNode(diff);
    g.tryAddEdge(nv->outputAttrId(0), nd->inputAttrId(0));

    auto a = scinodes::analyzeUnits(g);
    EXPECT_TRUE(a.ok());
    EXPECT_TRUE(a.isResolved(nd->outputAttrId(0)));
    // d V/dt = V/s.  exp de V = (2,1,-3,-1); /s suma s^-1 → (2,1,-4,-1).
    auto outU = a.unitAt(nd->outputAttrId(0));
    EXPECT_TRUE(outU.exp[2] == -4);
}
static void test_domain_unit_default_seconds() {
    std::cout << "[191e] NodeGraph default domainUnit = s (time-domain)\n";
    NodeGraph g;
    EXPECT_TRUE(g.domainUnit() == scinodes::unitSecond());
}
static void test_domain_unit_drives_integrator_factor() {
    std::cout << "[191f] Integrator usa graph.domainUnit() — cambiar el "
                 "domain a Hz convierte la transformación\n";
    NodeGraph g;
    int v = g.addNode(NodeType::VoltageSource);
    int integ = g.addNode(NodeType::Integrator);
    auto* nv = g.findNode(v); auto* ni = g.findNode(integ);
    g.tryAddEdge(nv->outputAttrId(0), ni->inputAttrId(0));

    // Time-domain default: V × s
    {
        auto a = scinodes::analyzeUnits(g);
        EXPECT_TRUE(a.ok());
        auto outU = a.unitAt(ni->outputAttrId(0));
        auto expected = scinodes::units::kVolt * scinodes::unitSecond();
        EXPECT_TRUE(outU.sameDimension(expected));
    }
    // Cambiamos domain a frecuencia: out = V × Hz = V/s
    {
        g.setDomainUnit(scinodes::units::kHertz);
        auto a = scinodes::analyzeUnits(g);
        EXPECT_TRUE(a.ok());
        auto outU = a.unitAt(ni->outputAttrId(0));
        auto expected = scinodes::units::kVolt * scinodes::units::kHertz;
        EXPECT_TRUE(outU.sameDimension(expected));
    }
}
static void test_domain_unit_serialization_roundtrip() {
    std::cout << "[191g] Round-trip de domain_unit por .scn (default s no se "
                 "emite)\n";
    NodeGraph g1;
    // Default s: no debería aparecer "domain_unit" en el .scn.
    ScnPositions pos1;
    std::string text1 = ScnSerializer::serialize(g1, pos1);
    EXPECT_TRUE(text1.find("domain_unit") == std::string::npos);

    // Si cambiamos a Hz, sí se emite.
    g1.setDomainUnit(scinodes::units::kHertz);
    std::string text2 = ScnSerializer::serialize(g1, pos1);
    EXPECT_TRUE(text2.find("domain_unit") != std::string::npos);

    NodeGraph g2;
    ScnPositions pos2;
    auto report = ScnSerializer::deserialize(text2, g2, pos2);
    EXPECT_TRUE(report.ok);
    EXPECT_TRUE(g2.domainUnit().sameDimension(scinodes::units::kHertz));
}
static void test_phantom_angle_distinguishes_rad_from_dimensionless() {
    std::cout << "[191b] rad y dimensionless puro NO son sameDimension "
                 "(8ª dim del exp)\n";
    EXPECT_FALSE(scinodes::units::kRadian.sameDimension(
        scinodes::units::kDimensionless));
    EXPECT_TRUE(scinodes::units::kRadian.exp[7] == 1);
    EXPECT_TRUE(scinodes::units::kDimensionless.exp[7] == 0);
    // Magnitudes son ambas 1.0; lo único que las distingue es el exp[7].
    EXPECT_TRUE(scinodes::units::kRadian.magnitude == 1.0);
    EXPECT_TRUE(scinodes::units::kDimensionless.magnitude == 1.0);
}
static void test_phantom_angle_deg_and_rad_share_dimension() {
    std::cout << "[191c] rad y deg son sameDimension (ambos angle), pero "
                 "magnitudes distintas\n";
    EXPECT_TRUE(scinodes::units::kDegree.sameDimension(
        scinodes::units::kRadian));
    EXPECT_FALSE(scinodes::units::kDegree == scinodes::units::kRadian);
    EXPECT_TRUE(std::fabs(scinodes::units::kDegree.magnitude
                          - 3.14159265358979323846 / 180.0) < 1e-12);
}
static void test_phantom_angle_radps_distinct_from_hz() {
    std::cout << "[191d] rad/s y Hz tienen exp distintos en la 8ª dim\n";
    EXPECT_FALSE(scinodes::units::kRadianPerSec.sameDimension(
        scinodes::units::kHertz));
    EXPECT_TRUE(scinodes::units::kRadianPerSec.exp[2] == -1);
    EXPECT_TRUE(scinodes::units::kRadianPerSec.exp[7] == 1);
    EXPECT_TRUE(scinodes::units::kHertz.exp[2] == -1);
    EXPECT_TRUE(scinodes::units::kHertz.exp[7] == 0);
}
static void test_integrator_then_oscilloscope_walkthrough_e1() {
    std::cout << "[191] Walkthrough E1 simulado: Step→...→DCMotor→Integ→Osc, "
                 "Osc.in resuelve a adimensional (rad)\n";
    NodeGraph g;
    int step = g.addNode(NodeType::StepSignal);
    int sum  = g.addNode(NodeType::Summation);
    int pid  = g.addNode(NodeType::PIDController);
    int m    = g.addNode(NodeType::DCMotorModel);
    int integ= g.addNode(NodeType::Integrator);
    int osc  = g.addNode(NodeType::Oscilloscope);
    auto* ns = g.findNode(step); auto* nu = g.findNode(sum);
    auto* np = g.findNode(pid);  auto* nm = g.findNode(m);
    auto* ni = g.findNode(integ);auto* no = g.findNode(osc);
    g.tryAddEdge(ns->outputAttrId(0), nu->inputAttrId(0));
    g.tryAddEdge(nu->outputAttrId(0), np->inputAttrId(0));
    g.tryAddEdge(np->outputAttrId(0), nm->inputAttrId(0));
    g.tryAddEdge(nm->outputAttrId(0), ni->inputAttrId(0));
    g.tryAddEdge(ni->outputAttrId(0), no->inputAttrId(0));

    auto a = scinodes::analyzeUnits(g);
    EXPECT_TRUE(a.ok());
    // Toda la cadena hasta DCMotor.in resuelve a V (backward).
    EXPECT_TRUE(a.unitAt(ns->outputAttrId(0)).sameDimension(scinodes::units::kVolt));
    // DCMotor.out es rad/s (declarado).
    EXPECT_TRUE(a.unitAt(nm->outputAttrId(0)).sameDimension(scinodes::units::kRadianPerSec));
    // Integrator.out = rad/s × s = rad (8ª dim).  Distinguible de
    // adimensional puro y de Hz gracias al phantom angle exponent.
    EXPECT_TRUE(a.unitAt(ni->outputAttrId(0)).sameDimension(scinodes::units::kRadian));
    // Oscilloscope.in0 hereda rad por propagación.
    EXPECT_TRUE(a.unitAt(no->inputAttrId(0)).sameDimension(scinodes::units::kRadian));
}

static void test_unit_display_prefers_ascii_ohm() {
    std::cout << "[187b] toCanonicalString de la resistencia Ω rinde \"Ohm\" "
                 "(ASCII), no la letra griega\n";
    auto r = scinodes::parseUnit("Ohm");
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.unit.toCanonicalString() == "Ohm");
    // Y el alias griego también canonicaliza a Ohm.
    auto rg = scinodes::parseUnit("Ω");
    EXPECT_TRUE(rg.ok());
    EXPECT_TRUE(rg.unit.toCanonicalString() == "Ohm");
}
static void test_field_dimensional_acceptance_logic() {
    std::cout << "[187c] Lógica de aceptación del QuantityField widget\n";
    // Reproducimos el flujo del widget sin GUI:
    //   curQ      = unidad actual del field
    //   parsed    = lo que el usuario tipeó
    //   accept    = ¿se aceptaría el cambio?
    auto decide = [](const scinodes::Quantity& curQ,
                     scinodes::ParseQuantityResult pr) -> bool {
        if (!pr.ok()) return false;
        if (!pr.hasUnit) return true;                                 // bare number
        if (curQ.unit.isDimensionless()) {
            // Etapa 6I.P: ideal field — sólo acepta inputs dimensionless.
            return pr.quantity.unit.isDimensionless();
        }
        return pr.quantity.unit.sameDimension(curQ.unit);             // real field
    };

    const scinodes::Quantity ra{ 1.0, scinodes::parseUnit("Ohm").unit };

    // Caso (1): tipea sólo número → acepta, preserva unit.
    EXPECT_TRUE(decide(ra, scinodes::parseQuantity("5")));

    // Caso (2): tipea Ohm con prefijo → acepta (misma dim).
    EXPECT_TRUE(decide(ra, scinodes::parseQuantity("100mΩ")));
    EXPECT_TRUE(decide(ra, scinodes::parseQuantity("1kOhm")));

    // Caso (3): tipea OTRA dimensión sobre un campo Ohm → rechaza.
    EXPECT_FALSE(decide(ra, scinodes::parseQuantity("5m")));
    EXPECT_FALSE(decide(ra, scinodes::parseQuantity("5V")));
    EXPECT_FALSE(decide(ra, scinodes::parseQuantity("5s")));

    // Caso (4): ideal field (Kp) — SÓLO dimensionless.
    const scinodes::Quantity kp{ 1.0, scinodes::Unit{} };
    EXPECT_TRUE(decide(kp, scinodes::parseQuantity("5 V/V")));   // dimensionless
    EXPECT_TRUE(decide(kp, scinodes::parseQuantity("2")));        // bare scalar
    EXPECT_TRUE(decide(kp, scinodes::parseQuantity("0.5")));
    // 6I.P: rechaza unidades reales sobre un field ideal.
    EXPECT_FALSE(decide(kp, scinodes::parseQuantity("5 mV")));
    EXPECT_FALSE(decide(kp, scinodes::parseQuantity("1 rad/V")));
    EXPECT_FALSE(decide(kp, scinodes::parseQuantity("3 Ohm")));
    // 6I.P + 6I.L: rad NO es dimensionless (phantom angle).
    EXPECT_FALSE(decide(kp, scinodes::parseQuantity("1 rad")));
}
static void test_context_aware_prefix_in_ohm_field() {
    std::cout << "[187f] \"2k\" en field-Ohm = {2, kΩ}, toSI = 2000\n";
    auto ohm = scinodes::parseUnit("Ohm").unit;
    auto pr = scinodes::parseQuantity("2k", ohm);
    EXPECT_TRUE(pr.ok());
    EXPECT_TRUE(pr.hasUnit);
    EXPECT_TRUE(pr.quantity.value == 2.0);
    EXPECT_TRUE(pr.quantity.unit.sameDimension(ohm));
    EXPECT_TRUE(std::fabs(pr.quantity.toSI() - 2000.0) < 1e-9);
}
static void test_context_aware_prefix_in_volt_field() {
    std::cout << "[187g] \"3.3k\" en field-V = {3.3, kV}, toSI = 3300\n";
    auto pr = scinodes::parseQuantity("3.3k", scinodes::units::kVolt);
    EXPECT_TRUE(pr.ok());
    EXPECT_TRUE(std::fabs(pr.quantity.toSI() - 3300.0) < 1e-9);
}
static void test_context_aware_milli_prefix() {
    std::cout << "[187h] \"5m\" no entra al fallback porque \"m\" es meter "
                 "(parse normal le gana)\n";
    // "5m" parsea como {5, meter} con el parser normal — el fallback
    // sólo aplica si el normal FALLA.  En V-field el caller (GUI)
    // rechaza por dim mismatch; el parser no inventa.
    auto pr = scinodes::parseQuantity("5m", scinodes::units::kVolt);
    EXPECT_TRUE(pr.ok());
    EXPECT_TRUE(pr.quantity.unit.sameDimension(scinodes::units::kMeter));
    // Para mili-volts el usuario tipea explícito:
    auto pr2 = scinodes::parseQuantity("5mV", scinodes::units::kVolt);
    EXPECT_TRUE(pr2.ok());
    EXPECT_TRUE(std::fabs(pr2.quantity.toSI() - 0.005) < 1e-12);
}
static void test_context_aware_micro_prefix() {
    std::cout << "[187i] \"100μ\" en field-s = {100, μs}, toSI = 1e-4\n";
    auto pr = scinodes::parseQuantity("100μ", scinodes::units::kSecond);
    EXPECT_TRUE(pr.ok());
    EXPECT_TRUE(std::fabs(pr.quantity.toSI() - 1e-4) < 1e-15);
}
static void test_context_aware_explicit_unit_wins() {
    std::cout << "[187j] \"2 V\" sigue parseando como V incluso en Ohm-field "
                 "(el R7 del caller decide)\n";
    auto ohm = scinodes::parseUnit("Ohm").unit;
    auto pr = scinodes::parseQuantity("2 V", ohm);
    EXPECT_TRUE(pr.ok());
    // El parser NO miente — el usuario escribió V, parsea como V.
    // El R7 a nivel field (en la GUI) rechaza el commit por
    // dimensional mismatch con Ohm.
    EXPECT_TRUE(pr.quantity.unit.sameDimension(scinodes::units::kVolt));
    EXPECT_FALSE(pr.quantity.unit.sameDimension(ohm));
}
static void test_context_aware_bare_number_preserves() {
    std::cout << "[187k] \"3.14\" sin nada después usa contextUnit\n";
    auto ohm = scinodes::parseUnit("Ohm").unit;
    auto pr = scinodes::parseQuantity("3.14", ohm);
    EXPECT_TRUE(pr.ok());
    EXPECT_FALSE(pr.hasUnit);
    // El value se preserva; el caller (GUI) decide qué hacer con la
    // unidad cuando hasUnit=false (preservar la existente).
    EXPECT_TRUE(pr.quantity.value == 3.14);
}
static void test_field_bare_number_resets_to_canonical() {
    std::cout << "[187m] Sim del widget: \"1\" en field cuya unidad actual "
                 "tiene prefijo (mΩ) resetea a canónica (Ohm)\n";
    // Reproducimos lo que hace el QuantityField widget para validar
    // la semántica end-to-end sin GUI.
    auto applyEdit = [](const scinodes::Quantity& curQ,
                        const std::string& input) -> scinodes::Quantity {
        auto pr = scinodes::parseQuantity(input, curQ.unit);
        if (!pr.ok()) return curQ;
        scinodes::Quantity q = pr.quantity;
        if (!pr.hasUnit) {
            q.unit = curQ.unit;
            q.unit.magnitude = 1.0;   // 6I.N: canónico
        }
        return q;
    };

    scinodes::Quantity ra{ 5.0, scinodes::parseUnit("mOhm").unit };
    EXPECT_TRUE(std::fabs(ra.unit.magnitude - 1e-3) < 1e-12);  // mΩ
    auto next = applyEdit(ra, "1");
    EXPECT_TRUE(next.value == 1.0);
    EXPECT_TRUE(next.unit.magnitude == 1.0);                  // ahora Ω
    EXPECT_TRUE(next.unit.sameDimension(scinodes::units::kOhm));
    EXPECT_TRUE(next.toSI() == 1.0);
}
static void test_ideal_field_accepts_prefix_only() {
    std::cout << "[187o] Field ideal (Kp) acepta \"2k\" como 2000 dimensionless\n";
    // Validamos el flujo end-to-end del widget para el caso "prefix-only"
    // sobre un field ideal: la unidad sigue siendo dimensionless × 1000.
    auto kp_unit = scinodes::Unit{};  // dimensionless
    auto pr = scinodes::parseQuantity("2k", kp_unit);
    EXPECT_TRUE(pr.ok());
    EXPECT_TRUE(pr.quantity.unit.isDimensionless());
    EXPECT_TRUE(std::fabs(pr.quantity.toSI() - 2000.0) < 1e-9);
}
static void test_field_bare_number_resets_voltage_canonical() {
    std::cout << "[187n] \"5\" en field-V con override kV vuelve a V (mag=1)\n";
    scinodes::Quantity v{ 3.0, scinodes::parseUnit("kV").unit };
    auto applyEdit = [](const scinodes::Quantity& curQ,
                        const std::string& input) -> scinodes::Quantity {
        auto pr = scinodes::parseQuantity(input, curQ.unit);
        if (!pr.ok()) return curQ;
        scinodes::Quantity q = pr.quantity;
        if (!pr.hasUnit) {
            q.unit = curQ.unit;
            q.unit.magnitude = 1.0;
        }
        return q;
    };
    auto next = applyEdit(v, "5");
    EXPECT_TRUE(next.value == 5.0);
    EXPECT_TRUE(next.unit.magnitude == 1.0);
    EXPECT_TRUE(next.unit.sameDimension(scinodes::units::kVolt));
}
static void test_context_aware_invalid_prefix_falls_back_to_error() {
    std::cout << "[187l] \"2xyz\" en Ohm-field → error (no es prefijo válido)\n";
    auto pr = scinodes::parseQuantity("2xyz", scinodes::units::kOhm);
    EXPECT_FALSE(pr.ok());
}

static void test_codegen_emits_toSI_value_for_voltage_source() {
    std::cout << "[187d] CodeGen emite Voltage en SI: \"3 mV\" → 0.003, "
                 "\"3 V\" → 3 al script Scilab\n";
    NodeGraph g;
    int v = g.addNode(NodeType::VoltageSource);
    // Sembrado por makeNode con value=12.0 V por default.
    // Cambiamos a 3 mV vía setFieldQuantity (lo que hace el QuantityField widget).
    auto mvUnit = scinodes::parseQuantity("3 mV");
    EXPECT_TRUE(mvUnit.ok());
    g.setFieldQuantity(v, "Voltage", mvUnit.quantity);

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // El script debe contener "0.003" (3 mV en SI), NO "3" (raw).
    EXPECT_TRUE(plan.script.find("0.003") != std::string::npos);

    // Cambiamos a 3 V (sin prefijo) y verificamos que ahora emite 3.
    auto vUnit = scinodes::parseQuantity("3 V");
    g.setFieldQuantity(v, "Voltage", vUnit.quantity);
    auto plan2 = ScilabCodeGen::generate(g);
    // Buscamos la línea con paramVar y "= 3"
    EXPECT_TRUE(plan2.script.find("= 3;") != std::string::npos ||
                plan2.script.find("=3;")  != std::string::npos);
}
static void test_codegen_emits_toSI_value_for_length() {
    std::cout << "[187e] CodeGen emite 100cm como 1.0 m al solver\n";
    NodeGraph g;
    // Usamos VoltageSource con su "Int. Resistance" (Ohm) como proxy —
    // el comportamiento es genérico al campo, no a la unidad.  Cambiamos
    // a algo con prefijo: "100 mOhm" = 0.1 Ohm.
    int v = g.addNode(NodeType::VoltageSource);
    auto q = scinodes::parseQuantity("100 mOhm");
    EXPECT_TRUE(q.ok());
    g.setFieldQuantity(v, "Int. Resistance", q.quantity);
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // 100 mOhm en SI = 0.1 Ohm.
    EXPECT_TRUE(plan.script.find("0.1") != std::string::npos);
}
static void test_node_instance_port_fields_value_zero() {
    std::cout << "[187] fields[in0].value arranca en 0 (puertos sin default)\n";
    NodeGraph g;
    int id = g.addNode(NodeType::Summation);
    const auto* n = g.findNode(id);
    EXPECT_TRUE(n->fields.at("in0").value == 0.0);
    EXPECT_TRUE(n->fields.at("in1").value == 0.0);
    EXPECT_TRUE(n->fields.at("out0").value == 0.0);
}

// ---- Etapa 5: Vector Math (evalVec3At recursivo) -----------------------
namespace {
// Helper: arma un grafo "vec3 source → TransformObject → SceneOutput"
// y devuelve la rotation del único SceneRenderable resultante.  Útil
// para verificar cualquier op vec3 sin reescribir el boilerplate.
std::array<float, 3>
runVec3Pipeline(NodeGraph& g, int vec3SourceId, int vec3SourcePort,
                StubResolver& r) {
    int objId   = g.addNode(NodeType::Object3D);
    int xfId    = g.addNode(NodeType::TransformObject);
    int sceneId = g.addNode(NodeType::SceneOutput);
    auto* nobj = g.findNode(objId);
    auto* nx   = g.findNode(xfId);
    auto* nsc  = g.findNode(sceneId);
    auto* nsrc = g.findNode(vec3SourceId);
    // src → TransformObject:1 (rotation)
    g.tryAddEdge(nsrc->outputAttrId(vec3SourcePort), nx->inputAttrId(1));
    g.tryAddEdge(nobj->outputAttrId(0), nx->inputAttrId(0));
    g.tryAddEdge(nx->outputAttrId(0),   nsc->inputAttrId(0));

    scinodes::DeviceAsset a; r.put("X", std::move(a));
    g.setStringParam(objId, "objectRef", "X");

    auto items = scinodes::collectScene(g, r);
    if (items.empty()) return { 0.f, 0.f, 0.f };
    return items[0].rotation;
}
}

static void test_vec3_math_add() {
    std::cout << "[74] VectorAdd: (1,2,3) + (4,5,6) = (5,7,9)\n";
    NodeGraph g;
    int aId = g.addNode(NodeType::Vec3Constant);
    int bId = g.addNode(NodeType::Vec3Constant);
    int sId = g.addNode(NodeType::VectorAdd);
    g.setParam(aId, "x", 1.0); g.setParam(aId, "y", 2.0); g.setParam(aId, "z", 3.0);
    g.setParam(bId, "x", 4.0); g.setParam(bId, "y", 5.0); g.setParam(bId, "z", 6.0);
    auto* na = g.findNode(aId);
    auto* nb = g.findNode(bId);
    auto* ns = g.findNode(sId);
    g.tryAddEdge(na->outputAttrId(0), ns->inputAttrId(0));
    g.tryAddEdge(nb->outputAttrId(0), ns->inputAttrId(1));

    StubResolver r;
    auto rot = runVec3Pipeline(g, sId, 0, r);
    EXPECT_TRUE(std::fabs(rot[0] - 5.0f) < 1e-6f);
    EXPECT_TRUE(std::fabs(rot[1] - 7.0f) < 1e-6f);
    EXPECT_TRUE(std::fabs(rot[2] - 9.0f) < 1e-6f);
}
static void test_vec3_math_sub() {
    std::cout << "[75] VectorSub: (5,7,9) - (1,2,3) = (4,5,6)\n";
    NodeGraph g;
    int aId = g.addNode(NodeType::Vec3Constant);
    int bId = g.addNode(NodeType::Vec3Constant);
    int sId = g.addNode(NodeType::VectorSub);
    g.setParam(aId, "x", 5.0); g.setParam(aId, "y", 7.0); g.setParam(aId, "z", 9.0);
    g.setParam(bId, "x", 1.0); g.setParam(bId, "y", 2.0); g.setParam(bId, "z", 3.0);
    auto* na = g.findNode(aId);
    auto* nb = g.findNode(bId);
    auto* ns = g.findNode(sId);
    g.tryAddEdge(na->outputAttrId(0), ns->inputAttrId(0));
    g.tryAddEdge(nb->outputAttrId(0), ns->inputAttrId(1));

    StubResolver r;
    auto rot = runVec3Pipeline(g, sId, 0, r);
    EXPECT_TRUE(std::fabs(rot[0] - 4.0f) < 1e-6f);
    EXPECT_TRUE(std::fabs(rot[1] - 5.0f) < 1e-6f);
    EXPECT_TRUE(std::fabs(rot[2] - 6.0f) < 1e-6f);
}
static void test_vec3_math_cross() {
    std::cout << "[76] VectorCross: X × Y = Z\n";
    // (1,0,0) × (0,1,0) = (0,0,1) — base estándar de R3
    NodeGraph g;
    int aId = g.addNode(NodeType::Vec3Constant);
    int bId = g.addNode(NodeType::Vec3Constant);
    int sId = g.addNode(NodeType::VectorCross);
    g.setParam(aId, "x", 1.0);
    g.setParam(bId, "y", 1.0);
    auto* na = g.findNode(aId);
    auto* nb = g.findNode(bId);
    auto* ns = g.findNode(sId);
    g.tryAddEdge(na->outputAttrId(0), ns->inputAttrId(0));
    g.tryAddEdge(nb->outputAttrId(0), ns->inputAttrId(1));

    StubResolver r;
    auto rot = runVec3Pipeline(g, sId, 0, r);
    EXPECT_TRUE(std::fabs(rot[0]) < 1e-6f);
    EXPECT_TRUE(std::fabs(rot[1]) < 1e-6f);
    EXPECT_TRUE(std::fabs(rot[2] - 1.0f) < 1e-6f);
}
static void test_vec3_math_normalize() {
    std::cout << "[77] VectorNormalize: (3,4,0) → (0.6, 0.8, 0)\n";
    NodeGraph g;
    int aId = g.addNode(NodeType::Vec3Constant);
    int nId = g.addNode(NodeType::VectorNormalize);
    g.setParam(aId, "x", 3.0); g.setParam(aId, "y", 4.0);
    auto* na = g.findNode(aId);
    auto* nn = g.findNode(nId);
    g.tryAddEdge(na->outputAttrId(0), nn->inputAttrId(0));

    StubResolver r;
    auto rot = runVec3Pipeline(g, nId, 0, r);
    EXPECT_TRUE(std::fabs(rot[0] - 0.6f) < 1e-5f);
    EXPECT_TRUE(std::fabs(rot[1] - 0.8f) < 1e-5f);
    EXPECT_TRUE(std::fabs(rot[2]) < 1e-6f);
}
static void test_vec3_math_normalize_zero() {
    std::cout << "[78] VectorNormalize: (0,0,0) → (0,0,0) (sin singularidad)\n";
    NodeGraph g;
    int aId = g.addNode(NodeType::Vec3Constant);   // todo 0 default
    int nId = g.addNode(NodeType::VectorNormalize);
    auto* na = g.findNode(aId);
    auto* nn = g.findNode(nId);
    g.tryAddEdge(na->outputAttrId(0), nn->inputAttrId(0));

    StubResolver r;
    auto rot = runVec3Pipeline(g, nId, 0, r);
    EXPECT_TRUE(std::fabs(rot[0]) < 1e-9f);
    EXPECT_TRUE(std::fabs(rot[1]) < 1e-9f);
    EXPECT_TRUE(std::fabs(rot[2]) < 1e-9f);
}
static void test_vec3_math_chained() {
    std::cout << "[79] VectorMath encadenado: normalize(cross(X, Y)) = (0,0,1)\n";
    NodeGraph g;
    int xId  = g.addNode(NodeType::Vec3Constant);
    int yId  = g.addNode(NodeType::Vec3Constant);
    int cId  = g.addNode(NodeType::VectorCross);
    int nId  = g.addNode(NodeType::VectorNormalize);
    g.setParam(xId, "x", 1.0);
    g.setParam(yId, "y", 1.0);
    auto* nx = g.findNode(xId);
    auto* ny = g.findNode(yId);
    auto* nc = g.findNode(cId);
    auto* nn = g.findNode(nId);
    g.tryAddEdge(nx->outputAttrId(0), nc->inputAttrId(0));
    g.tryAddEdge(ny->outputAttrId(0), nc->inputAttrId(1));
    g.tryAddEdge(nc->outputAttrId(0), nn->inputAttrId(0));

    StubResolver r;
    auto rot = runVec3Pipeline(g, nId, 0, r);
    EXPECT_TRUE(std::fabs(rot[0]) < 1e-6f);
    EXPECT_TRUE(std::fabs(rot[1]) < 1e-6f);
    EXPECT_TRUE(std::fabs(rot[2] - 1.0f) < 1e-6f);
}

static void test_scene_transform_without_bridge_stays_identity() {
    std::cout << "[49] collectScene: sin bridge (null) → rotación identidad (backwards-compat)\n";
    NodeGraph g;
    int sineId  = g.addNode(NodeType::SineSignal);
    int oscId   = g.addNode(NodeType::Oscilloscope);
    int objId   = g.addNode(NodeType::Object3D);
    int xfId    = g.addNode(NodeType::TransformObject);
    int sceneId = g.addNode(NodeType::SceneOutput);

    auto* ns  = g.findNode(sineId);
    auto* no  = g.findNode(oscId);
    auto* nobj= g.findNode(objId);
    auto* nx  = g.findNode(xfId);
    auto* nsc = g.findNode(sceneId);

    g.tryAddEdge(ns->outputAttrId(0),   no->inputAttrId(0));
    g.tryAddEdge(ns->outputAttrId(0),   nx->inputAttrId(1));
    g.tryAddEdge(nobj->outputAttrId(0), nx->inputAttrId(0));
    g.tryAddEdge(nx->outputAttrId(0),   nsc->inputAttrId(0));

    StubResolver r;
    scinodes::DeviceAsset a; r.put("X", std::move(a));
    g.setStringParam(objId, "objectRef", "X");

    // Sin bridge: rotación = identidad aunque el grafo esté listo.
    auto items = scinodes::collectScene(g, r, /*bridge=*/nullptr);
    EXPECT_TRUE(items.size() == 1);
    EXPECT_TRUE(std::fabs(items[0].rotation[2]) < 1e-9f);
}

static void test_scene_signal_subgraph_ignored_by_walker() {
    std::cout << "[46] collectScene: cadena Signal en el mismo grafo NO produce renderables\n";
    NodeGraph g;
    int src = g.addNode(NodeType::SineSignal);
    int snk = g.addNode(NodeType::Oscilloscope);
    auto* ns = g.findNode(src);
    auto* nk = g.findNode(snk);
    g.tryAddEdge(ns->outputAttrId(), nk->inputAttrId(0));

    StubResolver r;
    EXPECT_TRUE(scinodes::collectScene(g, r).empty());
}

static void test_serializer_rejects_bad_edge() {
    std::cout << "[21] Loading a file with a Sink→Transformer edge "
                 "→ ok with rejected-edge report\n";

    // Hand-crafted .scn: Oscilloscope (id=1) → Gain (id=2). That edge is R1.
    const char* bad = R"({
      "scnodes_version": "0.3",
      "next_node_id": 3,
      "nodes": [
        {"id":1, "type":"Oscilloscope", "position":[0,0], "params":{"Time Window":5.0}},
        {"id":2, "type":"Gain",         "position":[100,0],"params":{"K":1.0}}
      ],
      "edges": [
        {"id":1, "from_node":1, "to_node":2, "to_port":0}
      ]
    })";

    NodeGraph    g;
    ScnPositions pos;
    LoadReport report = ScnSerializer::deserialize(bad, g, pos);

    EXPECT_TRUE(report.ok);                        // JSON parsed fine
    EXPECT_TRUE(report.nodesLoaded == 2);
    EXPECT_TRUE(report.edgesLoaded == 0);          // the bad edge was dropped
    EXPECT_TRUE(report.rejectedEdges.size() == 1);
    EXPECT_TRUE(report.rejectedEdges[0].rule == "R1");
    EXPECT_TRUE(report.hasViolations());

    EXPECT_TRUE(g.nodeCount() == 2);
    EXPECT_TRUE(g.edgeCount() == 0);
}

static void test_serializer_unknown_type() {
    std::cout << "[22] Unknown node type is reported, other nodes still load\n";

    const char* mixed = R"({
      "scnodes_version": "0.3",
      "nodes": [
        {"id":1, "type":"SineSignal",      "position":[0,0],"params":{}},
        {"id":2, "type":"HypotheticalNode","position":[0,0],"params":{}},
        {"id":3, "type":"Oscilloscope",    "position":[0,0],"params":{}}
      ],
      "edges": []
    })";

    NodeGraph    g;
    ScnPositions pos;
    LoadReport report = ScnSerializer::deserialize(mixed, g, pos);

    EXPECT_TRUE(report.ok);
    EXPECT_TRUE(report.nodesLoaded == 2);
    EXPECT_TRUE(report.unknownTypes.size() == 1);
    EXPECT_TRUE(report.unknownTypes[0] == "HypotheticalNode");
    EXPECT_TRUE(g.nodeCount() == 2);
}

static void test_serializer_fatal_json_error() {
    std::cout << "[23] Malformed JSON → fatal error, graph reset\n";
    NodeGraph    g;
    ScnPositions pos;
    LoadReport report = ScnSerializer::deserialize("{ not valid", g, pos);
    EXPECT_FALSE(report.ok);
    EXPECT_FALSE(report.fatalError.empty());
    EXPECT_TRUE(g.nodeCount() == 0);
}

// Section 4b — root metadata (title, description, tags) round-trips.
// Es la base sobre la que se apoya `IExampleLibrary`: si el serializer
// no preserva estos campos, el linear backend lee headers vacíos.
static void test_serializer_root_metadata_roundtrip() {
    std::cout << "[23b] Root metadata round-trip (title/description/tags)\n";

    NodeGraph    g;
    ScnPositions pos;
    g.addNode(NodeType::SineSignal);
    g.setTitle("Ogata 8-1");
    g.setDescription("Lazo canónico con realimentación unitaria.\nSegunda línea.");
    g.setTags({"control", "pid", "ogata"});

    std::string text = ScnSerializer::serialize(g, pos);

    NodeGraph    g2;
    ScnPositions pos2;
    LoadReport report = ScnSerializer::deserialize(text, g2, pos2);

    EXPECT_TRUE(report.ok);
    EXPECT_TRUE(g2.title() == "Ogata 8-1");
    EXPECT_TRUE(g2.description().find("realimentación") != std::string::npos);
    EXPECT_TRUE(g2.description().find("Segunda línea") != std::string::npos);
    EXPECT_TRUE(g2.tags().size() == 3);
    EXPECT_TRUE(g2.tags()[0] == "control");
    EXPECT_TRUE(g2.tags()[2] == "ogata");
}

// Cargar un .scn pre-metadata (formato 0.3) NO debe ensuciar los campos
// nuevos con basura — quedan vacíos para que el caller decida el fallback.
static void test_serializer_root_metadata_absent_legacy_file() {
    std::cout << "[23c] Legacy .scn sin metadata → campos root vacíos\n";

    const char* legacy = R"({
      "scnodes_version": "0.3",
      "next_node_id": 2,
      "nodes": [
        {"id":1, "type":"SineSignal", "position":[0,0],"params":{}}
      ],
      "edges": []
    })";

    NodeGraph    g;
    ScnPositions pos;
    LoadReport report = ScnSerializer::deserialize(legacy, g, pos);

    EXPECT_TRUE(report.ok);
    EXPECT_TRUE(g.title().empty());
    EXPECT_TRUE(g.description().empty());
    EXPECT_TRUE(g.tags().empty());
}

// -----------------------------------------------------------------------
// Section 5 — ScilabCodeGen (graph → .sce text)
// -----------------------------------------------------------------------
static void test_codegen_simple_chain() {
    std::cout << "[24] CodeGen: SineSignal → Gain → Oscilloscope\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(t, "K", 2.5);
    auto* ns = g.findNode(s); auto* nt = g.findNode(t); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 1
                && plan.sinkChannels[0].nodeId == k
                && plan.sinkChannels[0].channel == 0);
    EXPECT_TRUE(plan.script.find("READY")     != std::string::npos);
    EXPECT_TRUE(plan.script.find("STATE")     != std::string::npos);
    EXPECT_TRUE(plan.script.find("driver()")  != std::string::npos);
    // Gain's K param is now variabilized: p_<gain_id>_0 = 2.5
    std::string gainK = "p_" + std::to_string(t) + "_0";
    EXPECT_TRUE(plan.script.find(gainK + " = 2.5") != std::string::npos);
    EXPECT_TRUE(plan.script.find(gainK + " * v")   != std::string::npos);
}

static void test_codegen_every_type_supported() {
    std::cout << "[25] CodeGen: every registry NodeType is emittable (Signal sub-language)\n";
    // Los nodos del sub-lenguaje Geometry (Object3D, TransformObject,
    // SceneOutput) NO emiten código Scilab por diseño — viven en el
    // grafo de escena 3D, no en el pipeline del solver.  La cobertura
    // del codegen se reporta por separado en
    // `test_codegen_geometry_nodes_are_not_emittable` abajo.
    for (const auto& [type, def] : nodeRegistry()) {
        const TypeExpr outTE = outputPortTypeOf(def, 0);
        const TypeExpr inTE  = inputPortTypeOf(def, 0);
        const bool isGeometry = isGeometryType(outTE) || isGeometryType(inTE);
        if (isGeometry) continue;
        EXPECT_TRUE(ScilabCodeGen::isSupported(type));
    }
}
static void test_codegen_geometry_nodes_are_not_emittable() {
    std::cout << "[25b] CodeGen: nodos Geometry NO son emittable (sub-lenguaje aparte)\n";
    EXPECT_FALSE(ScilabCodeGen::isSupported(NodeType::Object3D));
    EXPECT_FALSE(ScilabCodeGen::isSupported(NodeType::TransformObject));
    EXPECT_FALSE(ScilabCodeGen::isSupported(NodeType::SceneOutput));
}

static void test_codegen_integrator_emits_dynamics() {
    std::cout << "[28] CodeGen: Integrator emits dynamics() and ode() call\n";
    NodeGraph g;
    int s = g.addNode(NodeType::StepSignal);
    int i = g.addNode(NodeType::Integrator);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(i, "Initial Cond.", 5.0);
    auto* ns = g.findNode(s); auto* ni = g.findNode(i); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), ni->inputAttrId(0));
    g.tryAddEdge(ni->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("function dxdt = dynamics") != std::string::npos);
    // Marker del integrador RK4 fijo con sub-stepping (antes era
    // ode("rk", ...) — el adaptativo se atoraba con denormals).
    EXPECT_TRUE(plan.script.find("k1 = dynamics(") != std::string::npos);
    // IC is now stored in a variable: p_<integ>_0 = 5
    std::string icVar = "p_" + std::to_string(i) + "_0";
    EXPECT_TRUE(plan.script.find(icVar + " = 5")  != std::string::npos);
    EXPECT_TRUE(plan.script.find("x = [" + icVar) != std::string::npos);
    // dxdt es un literal vectorial: la expresión del derivative del
    // integrador es el output del source (v<s>).
    EXPECT_TRUE(plan.script.find("v" + std::to_string(s)) != std::string::npos);
}

static void test_codegen_dcmotor_two_states() {
    std::cout << "[29] CodeGen: DCMotor allocates 2 state slots\n";
    NodeGraph g;
    int v = g.addNode(NodeType::VoltageSource);
    int m = g.addNode(NodeType::DCMotorModel);
    int k = g.addNode(NodeType::Oscilloscope);
    auto* nv = g.findNode(v); auto* nm = g.findNode(m); auto* nk = g.findNode(k);
    g.tryAddEdge(nv->outputAttrId(), nm->inputAttrId(0));
    g.tryAddEdge(nm->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // 2 state slots → x(1) y x(2) ambos aparecen en dynamics.  El
    // header `State vector length: 2` lo confirma estructuralmente.
    EXPECT_TRUE(plan.script.find("x(1)") != std::string::npos);
    EXPECT_TRUE(plan.script.find("x(2)") != std::string::npos);
    EXPECT_TRUE(plan.script.find("State vector length: 2") != std::string::npos);
}

static void test_codegen_stateless_skips_ode() {
    std::cout << "[30] CodeGen: stateless graphs skip the dynamics function\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int k = g.addNode(NodeType::Oscilloscope);
    auto* ns = g.findNode(s); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("function dxdt = dynamics") == std::string::npos);
    EXPECT_TRUE(plan.script.find("ode(\"rk\"")              == std::string::npos);
}

static void test_codegen_transfer_function2() {
    std::cout << "[38] CodeGen: TransferFunction2 — 2-state controllable form\n";
    NodeGraph g;
    int s  = g.addNode(NodeType::StepSignal);
    int tf = g.addNode(NodeType::TransferFunction2);
    int k  = g.addNode(NodeType::Oscilloscope);
    g.setParam(tf, "num[0]", 1.0);
    g.setParam(tf, "num[1]", 0.0);
    g.setParam(tf, "den[0]", 1.0);
    g.setParam(tf, "den[1]", 0.0);
    auto* ns = g.findNode(s); auto* nt = g.findNode(tf); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("State vector length: 2") != std::string::npos);
    // Controllable canonical: el primer slot del literal vectorial
    // empieza con x(2) (era `dxdt(1) = x(2)` antes del refactor a
    // literal vectorial).
    EXPECT_TRUE(plan.script.find("dxdt = [x(2);") != std::string::npos);
    // Output is a linear combo of states (pure-state, no feedthrough).
    std::string b0 = "p_" + std::to_string(tf) + "_0";
    EXPECT_TRUE(plan.script.find(b0 + "*x(1)")            != std::string::npos);
}

static void test_codegen_view3d_sink_supported() {
    std::cout << "[40c] CodeGen: View3DSink is a valid 1-channel sink\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int v = g.addNode(NodeType::View3DSink);
    auto* ns = g.findNode(s); auto* nv = g.findNode(v);
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(), nv->inputAttrId(0)));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 1);
    EXPECT_TRUE(plan.sinkChannels[0].nodeId == v
                && plan.sinkChannels[0].channel == 0);
    EXPECT_TRUE(g.grammarState() == GrammarState::Valid);
}

static void test_codegen_phaseportrait_two_channels() {
    std::cout << "[40b] CodeGen: PhasePortrait sink contributes 2 STATE channels\n";
    NodeGraph g;
    int sx = g.addNode(NodeType::SineSignal);
    int sy = g.addNode(NodeType::CurrentSource);
    int pp = g.addNode(NodeType::PhasePortrait);
    auto* nx = g.findNode(sx); auto* ny = g.findNode(sy); auto* np = g.findNode(pp);
    g.tryAddEdge(nx->outputAttrId(), np->inputAttrId(0));
    g.tryAddEdge(ny->outputAttrId(), np->inputAttrId(1));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 2);
    EXPECT_TRUE(plan.sinkChannels[0].nodeId == pp && plan.sinkChannels[0].channel == 0);
    EXPECT_TRUE(plan.sinkChannels[1].nodeId == pp && plan.sinkChannels[1].channel == 1);
    // Channel 1 variable v<pp>_1 must appear in the script.
    std::string v1 = "v" + std::to_string(pp) + "_1";
    EXPECT_TRUE(plan.script.find(v1 + " = ") != std::string::npos);
}

static void test_codegen_emits_nan_guard() {
    std::cout << "[39] CodeGen: STATE line carries nanid + emits isnan() guards\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    int k = g.addNode(NodeType::Oscilloscope);
    auto* ns = g.findNode(s); auto* nt = g.findNode(t); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("nanid = 0;")               != std::string::npos);
    EXPECT_TRUE(plan.script.find("STATE %d")                 != std::string::npos);
    EXPECT_TRUE(plan.script.find("\\n\", nanid")             != std::string::npos);
    EXPECT_TRUE(plan.script.find("isnan(v" + std::to_string(s)) != std::string::npos);
}

static void test_codegen_inverse_kinematics() {
    std::cout << "[37] CodeGen: InverseKinematics is 2-input / 2-output planar IK\n";
    NodeGraph g;
    int sx = g.addNode(NodeType::CurrentSource);   // target x
    int sy = g.addNode(NodeType::CurrentSource);   // target y
    int ik = g.addNode(NodeType::InverseKinematics);
    int k1 = g.addNode(NodeType::Oscilloscope);    // θ₁
    int k2 = g.addNode(NodeType::Oscilloscope);    // θ₂
    auto* nx = g.findNode(sx); auto* ny = g.findNode(sy);
    auto* ni = g.findNode(ik);
    auto* nk1 = g.findNode(k1); auto* nk2 = g.findNode(k2);
    EXPECT_VALID(g.tryAddEdge(nx->outputAttrId(),  ni->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(ny->outputAttrId(),  ni->inputAttrId(1)));
    // ik has two output ports: port 0 = θ₁, port 1 = θ₂.
    EXPECT_VALID(g.tryAddEdge(ni->outputAttrId(0), nk1->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(ni->outputAttrId(1), nk2->inputAttrId(0)));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // Two output ports on the IK node ⇒ two variables emitted, one of
    // them with the _1 suffix.
    std::string v0 = "v" + std::to_string(ik);
    std::string v1 = "v" + std::to_string(ik) + "_1";
    EXPECT_TRUE(plan.script.find(v0 + " = atan(") != std::string::npos);
    EXPECT_TRUE(plan.script.find(v1 + " = atan(") != std::string::npos);
    // Both sinks recorded, in column order.
    EXPECT_TRUE(plan.sinkChannels.size() == 2);
}

static void test_codegen_design_template_emits_four_outputs() {
    std::cout << "[58] Sizing: DesignTemplate emits 4 constant output ports\n";
    NodeGraph g;
    int dt = g.addNode(NodeType::DesignTemplate);
    // 4 sinks, one per output port.
    int k0 = g.addNode(NodeType::Oscilloscope);
    int k1 = g.addNode(NodeType::Oscilloscope);
    int k2 = g.addNode(NodeType::Oscilloscope);
    int k3 = g.addNode(NodeType::Oscilloscope);
    auto* nd = g.findNode(dt);
    g.setParam(dt, "Target Torque",  25.0);
    g.setParam(dt, "Target Speed",  300.0);
    g.setParam(dt, "Bus Voltage",   600.0);
    g.setParam(dt, "Cooling Class",   2.0);
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(0), g.findNode(k0)->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(1), g.findNode(k1)->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(2), g.findNode(k2)->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(3), g.findNode(k3)->inputAttrId(0)));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // 4 distinct sink channels in the STATE line.
    EXPECT_TRUE(plan.sinkChannels.size() == 4);
    // The param-init block must contain the four user-supplied defaults.
    EXPECT_TRUE(plan.script.find("= 25") != std::string::npos);
    EXPECT_TRUE(plan.script.find("= 300") != std::string::npos);
    EXPECT_TRUE(plan.script.find("= 600") != std::string::npos);
}

// PMSMSizing now carries Slot Count and Pole Count alongside the
// electromagnetic params. They're consumed by the View3D panel's
// procedural-mesh builder (which lives in src/ui and therefore can't
// be exercised from this headless harness). Here we verify the
// registry surface so a renaming or a default drift gets caught.
static void test_codegen_pmsm_efficiency_emits_loss_terms() {
    std::cout << "[59e] Sweep: PMSMEfficiency emits P_cu, P_fe, P_mech, P_out\n";
    NodeGraph g;
    int sT  = g.addNode(NodeType::StepSignal);
    int sW  = g.addNode(NodeType::StepSignal);
    int sK  = g.addNode(NodeType::StepSignal);
    int eff = g.addNode(NodeType::PMSMEfficiency);
    int sk  = g.addNode(NodeType::Oscilloscope);
    auto* ne = g.findNode(eff);
    g.tryAddEdge(g.findNode(sT)->outputAttrId(), ne->inputAttrId(0));
    g.tryAddEdge(g.findNode(sW)->outputAttrId(), ne->inputAttrId(1));
    g.tryAddEdge(g.findNode(sK)->outputAttrId(), ne->inputAttrId(2));
    g.tryAddEdge(ne->outputAttrId(), g.findNode(sk)->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // Copper loss term: 1.5 * R * Iq^2.
    EXPECT_TRUE(plan.script.find("1.5 * ") != std::string::npos);
    // Element-wise divide and the safety bool2s guard.
    EXPECT_TRUE(plan.script.find("bool2s(") != std::string::npos);
    EXPECT_TRUE(plan.script.find("./") != std::string::npos);
}

static void test_codegen_heatmap_sink_three_channels() {
    std::cout << "[59f] Sweep: HeatmapSink contributes 3 STATE channels\n";
    NodeGraph g;
    int sX  = g.addNode(NodeType::StepSignal);
    int sY  = g.addNode(NodeType::StepSignal);
    int sC  = g.addNode(NodeType::StepSignal);
    int hm  = g.addNode(NodeType::HeatmapSink);
    auto* nh = g.findNode(hm);
    g.tryAddEdge(g.findNode(sX)->outputAttrId(), nh->inputAttrId(0));
    g.tryAddEdge(g.findNode(sY)->outputAttrId(), nh->inputAttrId(1));
    g.tryAddEdge(g.findNode(sC)->outputAttrId(), nh->inputAttrId(2));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 3);
    EXPECT_TRUE(plan.sinkChannels[0].nodeId == hm
             && plan.sinkChannels[0].channel == 0);
    EXPECT_TRUE(plan.sinkChannels[2].channel == 2);
}

static void test_codegen_airgap_flux_density_state_and_harmonics() {
    std::cout << "[59d] Air-gap: AirgapFluxDensity emits sin(p*x), 3rd, slot harmonics\n";
    NodeGraph g;
    int src   = g.addNode(NodeType::StepSignal);
    int agf   = g.addNode(NodeType::AirgapFluxDensity);
    int scope = g.addNode(NodeType::Oscilloscope);
    auto* ns = g.findNode(src);
    auto* na = g.findNode(agf);
    auto* nk = g.findNode(scope);
    g.tryAddEdge(ns->outputAttrId(), na->inputAttrId(0));
    g.tryAddEdge(na->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // One state slot is allocated for theta — the script header records it.
    EXPECT_TRUE(plan.script.find("State vector length: 1") != std::string::npos);
    // Three sin() terms appear in the output expression: fundamental,
    // 3rd-magnet harmonic, slot-passing harmonic.
    std::string v = "v" + std::to_string(agf);
    EXPECT_TRUE(plan.script.find(v + " = ") != std::string::npos);
    // Count sin( occurrences in the output assignment line. A simple
    // substring count is enough — the codegen emits exactly three.
    size_t pos = plan.script.find(v + " =");
    EXPECT_TRUE(pos != std::string::npos);
    if (pos != std::string::npos) {
        size_t eol = plan.script.find(';', pos);
        std::string line = plan.script.substr(pos, eol - pos);
        int sinCount = 0;
        for (size_t i = 0; (i = line.find("sin(", i)) != std::string::npos; ++i)
            ++sinCount;
        EXPECT_TRUE(sinCount == 3);
    }
    // dxdt = src(0) → the StepSignal's variable name appears inside
    // the dxdt vector literal.
    EXPECT_TRUE(plan.script.find("v" + std::to_string(src))
                != std::string::npos);
}

static void test_codegen_pmsm_electromagnetic_emits_formulas() {
    std::cout << "[59c] EM: PMSMElectromagnetic emits Ke/L/Vrms/Tcog expressions\n";
    NodeGraph g;
    int sD   = g.addNode(NodeType::StepSignal);
    int sL   = g.addNode(NodeType::StepSignal);
    int sW   = g.addNode(NodeType::StepSignal);
    int em   = g.addNode(NodeType::PMSMElectromagnetic);
    int kKe  = g.addNode(NodeType::Oscilloscope);
    int kLp  = g.addNode(NodeType::Oscilloscope);
    int kV   = g.addNode(NodeType::Oscilloscope);
    int kTc  = g.addNode(NodeType::Oscilloscope);

    auto* ne = g.findNode(em);
    g.tryAddEdge(g.findNode(sD)->outputAttrId(), ne->inputAttrId(0));
    g.tryAddEdge(g.findNode(sL)->outputAttrId(), ne->inputAttrId(1));
    g.tryAddEdge(g.findNode(sW)->outputAttrId(), ne->inputAttrId(2));
    g.tryAddEdge(ne->outputAttrId(0), g.findNode(kKe)->inputAttrId(0));
    g.tryAddEdge(ne->outputAttrId(1), g.findNode(kLp)->inputAttrId(0));
    g.tryAddEdge(ne->outputAttrId(2), g.findNode(kV)->inputAttrId(0));
    g.tryAddEdge(ne->outputAttrId(3), g.findNode(kTc)->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 4);
    // Ke formula must reference omega-independent params (kw, Nph, p, Bg).
    EXPECT_TRUE(plan.script.find("sqrt(2)") != std::string::npos);    // Vrms
    EXPECT_TRUE(plan.script.find("4*%pi*1e-7") != std::string::npos); // mu0
    EXPECT_TRUE(plan.script.find("1.05") != std::string::npos);       // mu_r
}

static void test_pmsm_sizing_has_slot_and_pole_params() {
    std::cout << "[59b] Sizing: PMSMSizing exposes Slot Count + Pole Count params\n";
    const auto& def = nodeRegistry().at(NodeType::PMSMSizing);
    bool hasSlot = false, hasPole = false;
    double slotDefault = 0, poleDefault = 0;
    for (const auto& p : def.params) {
        if (p.name == "Slot Count") { hasSlot = true; slotDefault = p.defaultValue; }
        if (p.name == "Pole Count") { hasPole = true; poleDefault = p.defaultValue; }
    }
    EXPECT_TRUE(hasSlot);
    EXPECT_TRUE(hasPole);
    EXPECT_TRUE(std::abs(slotDefault - 12.0) < 1e-9);
    EXPECT_TRUE(std::abs(poleDefault -  4.0) < 1e-9);
}

static void test_codegen_pmsm_sizing_emits_cube_root() {
    std::cout << "[59] Sizing: PMSMSizing emits cube-root formula and rated-P expression\n";
    NodeGraph g;
    int dt   = g.addNode(NodeType::DesignTemplate);
    int sz   = g.addNode(NodeType::PMSMSizing);
    int sk_D = g.addNode(NodeType::Oscilloscope);
    int sk_L = g.addNode(NodeType::Oscilloscope);
    int sk_P = g.addNode(NodeType::Oscilloscope);

    auto* nd = g.findNode(dt);
    auto* ns = g.findNode(sz);
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(0), ns->inputAttrId(0)));   // T
    EXPECT_VALID(g.tryAddEdge(nd->outputAttrId(1), ns->inputAttrId(1)));   // omega
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(0), g.findNode(sk_D)->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(1), g.findNode(sk_L)->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(ns->outputAttrId(2), g.findNode(sk_P)->inputAttrId(0)));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("^(1.0/3.0)") != std::string::npos);
    // Rated-power expression: T * omega.
    std::string T = "v" + std::to_string(dt);          // port 0 alias
    std::string w = "v" + std::to_string(dt) + "_1";   // port 1 alias
    EXPECT_TRUE(plan.script.find(T + " * " + w) != std::string::npos);
    // Three sink channels in STATE.
    EXPECT_TRUE(plan.sinkChannels.size() == 3);
}

static void test_codegen_transfer_function_supported() {
    std::cout << "[36] CodeGen: TransferFunction emits 1-state ODE\n";
    NodeGraph g;
    int s = g.addNode(NodeType::StepSignal);
    int tf = g.addNode(NodeType::TransferFunction);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(tf, "num[0]", 2.5);
    g.setParam(tf, "den[0]", 3.0);
    g.setParam(tf, "den[1]", 1.0);
    auto* ns = g.findNode(s); auto* nt = g.findNode(tf); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("State vector length: 1") != std::string::npos);
    // (num[0]*input - den[0]*x) / den[1]
    std::string b  = "p_" + std::to_string(tf) + "_0";
    std::string a0 = "p_" + std::to_string(tf) + "_1";
    std::string a1 = "p_" + std::to_string(tf) + "_2";
    EXPECT_TRUE(plan.script.find("(" + b + "*v" + std::to_string(s)) != std::string::npos);
    EXPECT_TRUE(plan.script.find(" - " + a0 + "*x(") != std::string::npos);
    EXPECT_TRUE(plan.script.find(") / " + a1)        != std::string::npos);
    EXPECT_TRUE(plan.script.find(b + " = 2.5") != std::string::npos);
}

static void test_codegen_differentiator_supported() {
    std::cout << "[35] CodeGen: Differentiator emits 1-state filtered deriv\n";
    NodeGraph g;
    int s = g.addNode(NodeType::RampSignal);
    int d = g.addNode(NodeType::Differentiator);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(d, "Cutoff Freq.", 50.0);
    auto* ns = g.findNode(s); auto* nd = g.findNode(d); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nd->inputAttrId(0));
    g.tryAddEdge(nd->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("State vector length: 1") != std::string::npos);
    // Output expression uses ωc·(input − x).
    EXPECT_TRUE(plan.script.find("(2*%pi*p_" + std::to_string(d) + "_0)")
                != std::string::npos);
    EXPECT_TRUE(plan.script.find("p_" + std::to_string(d) + "_0 = 50")
                != std::string::npos);
}

static void test_codegen_closed_loop_through_integrator() {
    std::cout << "[33] CodeGen: cycle through Integrator generates cleanly\n";
    // Step → Sum(+,-) → Integrator → Scope
    //                      ↑                ↓
    //                      ←──────── (feedback) ────────←
    NodeGraph g;
    int step = g.addNode(NodeType::StepSignal);
    int sum  = g.addNode(NodeType::Summation);
    int integ = g.addNode(NodeType::Integrator);
    int scope = g.addNode(NodeType::Oscilloscope);
    g.setParam(sum, "Sign2", -1.0);
    auto* nstep  = g.findNode(step);
    auto* nsum   = g.findNode(sum);
    auto* ninteg = g.findNode(integ);
    auto* nscope = g.findNode(scope);

    EXPECT_VALID(g.tryAddEdge(nstep->outputAttrId(),  nsum->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nsum->outputAttrId(),   ninteg->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(ninteg->outputAttrId(), nscope->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(ninteg->outputAttrId(), nsum->inputAttrId(1)));   // feedback

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("function dxdt = dynamics") != std::string::npos);
    EXPECT_TRUE(plan.script.find("v" + std::to_string(sum))
                != std::string::npos);
}

static void test_codegen_algebraic_loop_rejected() {
    std::cout << "[34] CodeGen: algebraic loop (no pure-state) is rejected\n";
    // Sum(+,-) → Gain → back to Sum  (no Integrator/LPF/DCMotor in the cycle)
    NodeGraph g;
    int src  = g.addNode(NodeType::StepSignal);
    int sum  = g.addNode(NodeType::Summation);
    int gain = g.addNode(NodeType::Gain);
    int snk  = g.addNode(NodeType::Oscilloscope);
    auto* nsrc  = g.findNode(src);
    auto* nsum  = g.findNode(sum);
    auto* ngain = g.findNode(gain);
    auto* nsnk  = g.findNode(snk);
    EXPECT_VALID(g.tryAddEdge(nsrc->outputAttrId(),  nsum->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(nsum->outputAttrId(),  ngain->inputAttrId(0)));
    EXPECT_VALID(g.tryAddEdge(ngain->outputAttrId(), nsum->inputAttrId(1)));   // algebraic loop
    EXPECT_VALID(g.tryAddEdge(ngain->outputAttrId(), nsnk->inputAttrId(0)));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_FALSE(plan.error.empty());
    EXPECT_TRUE(plan.error.find("algebraic loop") != std::string::npos);
    EXPECT_TRUE(plan.script.empty());
}

static void test_codegen_param_dispatch_emitted() {
    std::cout << "[32] CodeGen: emits a `param N I V` dispatch branch\n";
    NodeGraph g;
    int s = g.addNode(NodeType::SineSignal);
    int t = g.addNode(NodeType::Gain);
    int k = g.addNode(NodeType::Oscilloscope);
    auto* ns = g.findNode(s); auto* nt = g.findNode(t); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), nt->inputAttrId(0));
    g.tryAddEdge(nt->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("elseif cmd == \"param\"") != std::string::npos);
    EXPECT_TRUE(plan.script.find("pn = mfscanf") != std::string::npos);
    // El dispatch ahora usa execstr para evitar la cascada de elseif
    // que rompía el parser de Scilab con muchos nodos.  Verificamos
    // que el codegen emita la línea genérica.
    EXPECT_TRUE(plan.script.find("execstr(\"p_\" + string(pn)") != std::string::npos);
}

static void test_codegen_pid_uses_state() {
    std::cout << "[31] CodeGen: PID output uses Kp*input + Ki*state\n";
    NodeGraph g;
    int s = g.addNode(NodeType::StepSignal);
    int p = g.addNode(NodeType::PIDController);
    int k = g.addNode(NodeType::Oscilloscope);
    g.setParam(p, "Kp", 3.0);
    g.setParam(p, "Ki", 0.5);
    auto* ns = g.findNode(s); auto* np = g.findNode(p); auto* nk = g.findNode(k);
    g.tryAddEdge(ns->outputAttrId(), np->inputAttrId(0));
    g.tryAddEdge(np->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // Kp = param 0, Ki = param 1 — both now variabilized.
    std::string Kp = "p_" + std::to_string(p) + "_0";
    std::string Ki = "p_" + std::to_string(p) + "_1";
    EXPECT_TRUE(plan.script.find(Kp + " = 3")            != std::string::npos);
    EXPECT_TRUE(plan.script.find(Ki + " = 0.5")          != std::string::npos);
    EXPECT_TRUE(plan.script.find(Kp + " * v" + std::to_string(s)) != std::string::npos);
    EXPECT_TRUE(plan.script.find(Ki + " * x(")           != std::string::npos);
}

static void test_codegen_empty_graph() {
    std::cout << "[26] CodeGen: empty graph yields no sinks, no error\n";
    NodeGraph g;
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.empty());
}

static void test_codegen_summation_two_inputs() {
    std::cout << "[27] CodeGen: Summation reads both input ports\n";
    NodeGraph g;
    int a = g.addNode(NodeType::SineSignal);
    int b = g.addNode(NodeType::StepSignal);
    int sum = g.addNode(NodeType::Summation);
    int k   = g.addNode(NodeType::Oscilloscope);
    auto* na = g.findNode(a); auto* nb = g.findNode(b);
    auto* nsum = g.findNode(sum); auto* nk = g.findNode(k);
    g.tryAddEdge(na->outputAttrId(),   nsum->inputAttrId(0));
    g.tryAddEdge(nb->outputAttrId(),   nsum->inputAttrId(1));
    g.tryAddEdge(nsum->outputAttrId(), nk->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("v" + std::to_string(a)) != std::string::npos);
    EXPECT_TRUE(plan.script.find("v" + std::to_string(b)) != std::string::npos);
}

// -----------------------------------------------------------------------
// Section 6 — FFT helper (scinodes::magnitudeSpectrum)
// -----------------------------------------------------------------------
static void test_fft_pow2_check() {
    std::cout << "[40] FFT: isPow2 utility\n";
    EXPECT_TRUE(scinodes::isPow2(1));
    EXPECT_TRUE(scinodes::isPow2(2));
    EXPECT_TRUE(scinodes::isPow2(64));
    EXPECT_FALSE(scinodes::isPow2(0));
    EXPECT_FALSE(scinodes::isPow2(3));
    EXPECT_FALSE(scinodes::isPow2(100));
}

static void test_fft_peak_at_expected_bin() {
    std::cout << "[41] FFT: pure sinusoid produces a clean peak at the expected bin\n";
    // Sample N = 64 points of sin(2*pi*k*t/N) over one full window, k=4.
    const int N = 64;
    const int k = 4;
    std::vector<float> samples(N);
    for (int i = 0; i < N; ++i)
        samples[i] = std::sin(2.0f * 3.14159265358979323846f * k * i / N);

    auto mag = scinodes::magnitudeSpectrum(samples.data(), N);
    EXPECT_TRUE((int)mag.size() == N/2 + 1);

    // Peak should be at bin k. Magnitude N/2 because energy is split with
    // the negative-frequency mirror (which we don't see in the one-sided
    // spectrum).
    int peakBin = static_cast<int>(
        std::max_element(mag.begin() + 1, mag.end()) - mag.begin());
    EXPECT_TRUE(peakBin == k);
    EXPECT_TRUE(std::fabs(mag[k] - N / 2.0f) < 1e-3f);
}

static void test_fft_dc_only_input() {
    std::cout << "[42] FFT: a constant signal produces all energy in DC bin\n";
    const int N = 32;
    std::vector<float> samples(N, 1.5f);
    auto mag = scinodes::magnitudeSpectrum(samples.data(), N);
    EXPECT_TRUE(std::fabs(mag[0] - N * 1.5f) < 1e-3f);
    for (int k = 1; k < (int)mag.size(); ++k)
        EXPECT_TRUE(mag[k] < 1e-3f);
}

static void test_fft_non_pow2_rejected() {
    std::cout << "[43] FFT: non-power-of-2 input returns an empty spectrum\n";
    std::vector<float> samples(48, 0.0f);
    auto mag = scinodes::magnitudeSpectrum(samples.data(), 48);
    EXPECT_TRUE(mag.empty());
}

// -----------------------------------------------------------------------
// Section 6 — CSV exporter
//
// The exporter takes a ring buffer + write index and emits chronologically
// ordered (time, value) rows. We round-trip a known buffer and verify the
// header, row count, timestamps, and value ordering.
// -----------------------------------------------------------------------
static std::string makeTempCsvPath() {
    char tmpl[] = "/tmp/scnodes_csv_XXXXXX";
    int fd = ::mkstemp(tmpl);
    if (fd >= 0) ::close(fd);
    return std::string(tmpl) + ".csv";
}

static std::vector<std::string> readLines(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream f(path);
    for (std::string line; std::getline(f, line); )
        out.push_back(line);
    return out;
}

static void test_csv_export_partial_buffer() {
    std::cout << "[44] CsvExport: partially-filled buffer writes only real samples\n";
    // 8-slot ring; 5 writes so chronological is buf[0..4].
    std::vector<float> buf(8, 0.0f);
    buf[0] = 1.0f; buf[1] = 2.0f; buf[2] = 3.0f; buf[3] = 4.0f; buf[4] = 5.0f;
    int wIdx = 5;

    auto path = makeTempCsvPath();
    std::string err;
    bool ok = scinodes::writeSinkCsv(path, buf, wIdx,
                                     /*latestTime*/ 1.0f, /*dt*/ 0.25f,
                                     "DataLogger #7", &err);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(err.empty());

    auto lines = readLines(path);
    // # Sink header, "time,value" header, then 5 data rows = 7 lines.
    EXPECT_TRUE(lines.size() == 7);
    EXPECT_TRUE(lines[0].find("# Sink: DataLogger #7") == 0);
    EXPECT_TRUE(lines[1] == "time,value");

    // First data row: t = 1.0 - 4*0.25 = 0.0, value = 1.0
    // Last  data row: t = 1.0,              value = 5.0
    double t0 = 0, v0 = 0, tN = 0, vN = 0;
    EXPECT_TRUE(std::sscanf(lines[2].c_str(), "%lf,%lf", &t0, &v0) == 2);
    EXPECT_TRUE(std::sscanf(lines[6].c_str(), "%lf,%lf", &tN, &vN) == 2);
    EXPECT_TRUE(std::abs(t0 - 0.0) < 1e-4);
    EXPECT_TRUE(std::abs(v0 - 1.0) < 1e-4);
    EXPECT_TRUE(std::abs(tN - 1.0) < 1e-4);
    EXPECT_TRUE(std::abs(vN - 5.0) < 1e-4);

    std::remove(path.c_str());
}

static void test_csv_export_wrapped_buffer() {
    std::cout << "[45] CsvExport: wrapped ring buffer is reordered chronologically\n";
    // 4-slot ring; writes 1..10 with values 0..9. Write k lands at slot
    // (k-1) % 4, so the final slots [0..3] hold {8, 9, 6, 7} and the
    // surviving chronological values are 6, 7, 8, 9.
    std::vector<float> buf = { 8.0f, 9.0f, 6.0f, 7.0f };
    int wIdx = 10;

    auto path = makeTempCsvPath();
    bool ok = scinodes::writeSinkCsv(path, buf, wIdx, /*latestTime*/ 3.0f,
                                     /*dt*/ 1.0f, "", nullptr);
    EXPECT_TRUE(ok);

    auto lines = readLines(path);
    // No # comment (empty label), so: header + 4 rows.
    EXPECT_TRUE(lines.size() == 5);
    EXPECT_TRUE(lines[0] == "time,value");

    double t[4], v[4];
    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE(std::sscanf(lines[1 + i].c_str(), "%lf,%lf",
                                &t[i], &v[i]) == 2);
    EXPECT_TRUE(std::abs(v[0] - 6.0) < 1e-4);
    EXPECT_TRUE(std::abs(v[1] - 7.0) < 1e-4);
    EXPECT_TRUE(std::abs(v[2] - 8.0) < 1e-4);
    EXPECT_TRUE(std::abs(v[3] - 9.0) < 1e-4);
    // Times: 0, 1, 2, 3.
    EXPECT_TRUE(std::abs(t[0] - 0.0) < 1e-4);
    EXPECT_TRUE(std::abs(t[3] - 3.0) < 1e-4);

    std::remove(path.c_str());
}

static void test_csv_export_empty_buffer() {
    std::cout << "[46] CsvExport: zero-write buffer emits only the header\n";
    std::vector<float> buf(16, 0.0f);
    auto path = makeTempCsvPath();
    EXPECT_TRUE(scinodes::writeSinkCsv(path, buf, /*wIdx*/ 0,
                                       0.0f, 0.01f, "", nullptr));
    auto lines = readLines(path);
    EXPECT_TRUE(lines.size() == 1);
    EXPECT_TRUE(lines[0] == "time,value");
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------
// Stage v0.9 thermal-network nodes — codegen smoke checks.
// -----------------------------------------------------------------------
static void test_codegen_joule_loss_emits_iq_squared() {
    std::cout << "[66] Thermal: JouleLoss emits 1.5 * R * (T/Ke)^2\n";
    NodeGraph g;
    int sT = g.addNode(NodeType::StepSignal);
    int sK = g.addNode(NodeType::StepSignal);
    int jl = g.addNode(NodeType::JouleLoss);
    int sk = g.addNode(NodeType::Oscilloscope);
    auto* nj = g.findNode(jl);
    g.tryAddEdge(g.findNode(sT)->outputAttrId(), nj->inputAttrId(0));
    g.tryAddEdge(g.findNode(sK)->outputAttrId(), nj->inputAttrId(1));
    g.tryAddEdge(nj->outputAttrId(), g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("1.5 * ") != std::string::npos);
    EXPECT_TRUE(plan.script.find("1e-12") != std::string::npos);   // Ke guard
}

static void test_codegen_core_loss_uses_electrical_frequency() {
    std::cout << "[67] Thermal: CoreLoss uses p*omega/(2*pi) for the frequency\n";
    NodeGraph g;
    int sW = g.addNode(NodeType::StepSignal);
    int sB = g.addNode(NodeType::StepSignal);
    int cl = g.addNode(NodeType::CoreLoss);
    int sk = g.addNode(NodeType::Oscilloscope);
    auto* nc = g.findNode(cl);
    g.tryAddEdge(g.findNode(sW)->outputAttrId(), nc->inputAttrId(0));
    g.tryAddEdge(g.findNode(sB)->outputAttrId(), nc->inputAttrId(1));
    g.tryAddEdge(nc->outputAttrId(), g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("/ (2 * %pi)") != std::string::npos);
}

static void test_codegen_mechanical_loss_emits_quadratic_term() {
    std::cout << "[68] Thermal: MechanicalLoss = K_visc*|ω| + K_drag*ω^2\n";
    NodeGraph g;
    int sW = g.addNode(NodeType::StepSignal);
    int ml = g.addNode(NodeType::MechanicalLoss);
    int sk = g.addNode(NodeType::Oscilloscope);
    g.tryAddEdge(g.findNode(sW)->outputAttrId(),
                 g.findNode(ml)->inputAttrId(0));
    g.tryAddEdge(g.findNode(ml)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("abs(") != std::string::npos);
    EXPECT_TRUE(plan.script.find("^2")    != std::string::npos);
}

static void test_codegen_thermal_node_sums_four_inputs() {
    std::cout << "[71] Thermal: ThermalNode sums 4 inputs and integrates / C\n";
    NodeGraph g;
    int s0 = g.addNode(NodeType::StepSignal);
    int s1 = g.addNode(NodeType::StepSignal);
    int tn = g.addNode(NodeType::ThermalNode);
    int sk = g.addNode(NodeType::Oscilloscope);
    g.tryAddEdge(g.findNode(s0)->outputAttrId(),
                 g.findNode(tn)->inputAttrId(0));
    g.tryAddEdge(g.findNode(s1)->outputAttrId(),
                 g.findNode(tn)->inputAttrId(1));
    g.tryAddEdge(g.findNode(tn)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("State vector length: 1") != std::string::npos);
    // dxdt es un literal vectorial; el único slot empieza con la
    // expresión `(v<s0> + v<s1> + 0.0 + 0.0) / p_<tn>_0` envuelta en
    // paréntesis.  Antes era `dxdt(1) = (...`; ahora es `dxdt = [(...`.
    EXPECT_TRUE(plan.script.find("dxdt = [(") != std::string::npos);
    EXPECT_TRUE(plan.script.find(" + 0.0 + 0.0") != std::string::npos);
}

static void test_codegen_maxwell_force_emits_mu0_form() {
    std::cout << "[75] Structural: MaxwellForce emits B^2 / (2 * 4*pi*1e-7)\n";
    NodeGraph g;
    int src = g.addNode(NodeType::StepSignal);
    int mf  = g.addNode(NodeType::MaxwellForce);
    int sk  = g.addNode(NodeType::Oscilloscope);
    g.tryAddEdge(g.findNode(src)->outputAttrId(),
                 g.findNode(mf)->inputAttrId(0));
    g.tryAddEdge(g.findNode(mf)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("^2 / (2 * 4 * %pi * 1e-7)") != std::string::npos);
}

static void test_codegen_tolerance_perturbator_uses_rand() {
    std::cout << "[78] MC: TolerancePerturbator emits rand()-based noise\n";
    NodeGraph g;
    int src = g.addNode(NodeType::StepSignal);
    int tp  = g.addNode(NodeType::TolerancePerturbator);
    int sk  = g.addNode(NodeType::Oscilloscope);
    g.tryAddEdge(g.findNode(src)->outputAttrId(),
                 g.findNode(tp)->inputAttrId(0));
    g.tryAddEdge(g.findNode(tp)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("(2 * rand() - 1)") != std::string::npos);
}

static void test_codegen_distribution_sink_records_input() {
    std::cout << "[79] MC: DistributionSink contributes 1 STATE channel\n";
    NodeGraph g;
    int src = g.addNode(NodeType::StepSignal);
    int ds  = g.addNode(NodeType::DistributionSink);
    g.tryAddEdge(g.findNode(src)->outputAttrId(),
                 g.findNode(ds)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 1);
    EXPECT_TRUE(plan.sinkChannels[0].nodeId == ds);
}

static void test_codegen_view3d_deformation_sink_three_channels() {
    std::cout << "[77] Structural: View3DDeformationSink contributes 3 channels\n";
    NodeGraph g;
    int sF = g.addNode(NodeType::StepSignal);
    int sM = g.addNode(NodeType::StepSignal);
    int sA = g.addNode(NodeType::StepSignal);
    int sk = g.addNode(NodeType::View3DDeformationSink);
    auto* ns = g.findNode(sk);
    g.tryAddEdge(g.findNode(sF)->outputAttrId(), ns->inputAttrId(0));
    g.tryAddEdge(g.findNode(sM)->outputAttrId(), ns->inputAttrId(1));
    g.tryAddEdge(g.findNode(sA)->outputAttrId(), ns->inputAttrId(2));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 3);
    EXPECT_TRUE(plan.sinkChannels[2].channel == 2);
}

static void test_codegen_modal_frequency_emits_shape_factor() {
    std::cout << "[76] Structural: ModalFrequency emits m*(m^2-1)/sqrt(m^2+1) shape\n";
    NodeGraph g;
    int src = g.addNode(NodeType::StepSignal);
    int mf  = g.addNode(NodeType::ModalFrequency);
    int sk  = g.addNode(NodeType::Oscilloscope);
    g.tryAddEdge(g.findNode(src)->outputAttrId(),
                 g.findNode(mf)->inputAttrId(0));
    g.tryAddEdge(g.findNode(mf)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    // The thin-ring shape factor + m<=1 guard must both appear.
    EXPECT_TRUE(plan.script.find("bool2s(") != std::string::npos);
    EXPECT_TRUE(plan.script.find("sqrt(") != std::string::npos);
    EXPECT_TRUE(plan.script.find("/ (12 * ") != std::string::npos);
}

static void test_codegen_cooling_system_three_outputs() {
    std::cout << "[73] Cooling: CoolingSystem emits 3 STATE channels\n";
    NodeGraph g;
    int cs = g.addNode(NodeType::CoolingSystem);
    int k0 = g.addNode(NodeType::Oscilloscope);
    int k1 = g.addNode(NodeType::Oscilloscope);
    int k2 = g.addNode(NodeType::Oscilloscope);
    auto* nc = g.findNode(cs);
    g.tryAddEdge(nc->outputAttrId(0), g.findNode(k0)->inputAttrId(0));
    g.tryAddEdge(nc->outputAttrId(1), g.findNode(k1)->inputAttrId(0));
    g.tryAddEdge(nc->outputAttrId(2), g.findNode(k2)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 3);
}

static void test_codegen_convective_cooling_h_of_flow() {
    std::cout << "[74] Cooling: ConvectiveCooling q = (h_0 + h_slope·flow)·ΔT\n";
    NodeGraph g;
    int sH = g.addNode(NodeType::StepSignal);
    int sC = g.addNode(NodeType::StepSignal);
    int sF = g.addNode(NodeType::StepSignal);
    int cc = g.addNode(NodeType::ConvectiveCooling);
    int kH = g.addNode(NodeType::Oscilloscope);
    int kC = g.addNode(NodeType::Oscilloscope);
    auto* nc = g.findNode(cc);
    g.tryAddEdge(g.findNode(sH)->outputAttrId(), nc->inputAttrId(0));
    g.tryAddEdge(g.findNode(sC)->outputAttrId(), nc->inputAttrId(1));
    g.tryAddEdge(g.findNode(sF)->outputAttrId(), nc->inputAttrId(2));
    g.tryAddEdge(nc->outputAttrId(0), g.findNode(kH)->inputAttrId(0));
    g.tryAddEdge(nc->outputAttrId(1), g.findNode(kC)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 2);
    // Dual output: the second is the negation, so both lines appear.
    std::string v0 = "v" + std::to_string(cc);
    std::string v1 = "v" + std::to_string(cc) + "_1";
    EXPECT_TRUE(plan.script.find(v0 + " = ") != std::string::npos);
    EXPECT_TRUE(plan.script.find(v1 + " = ") != std::string::npos);
}

static void test_codegen_thermal_resistance_two_outputs() {
    std::cout << "[72] Thermal: ThermalResistance emits q and -q on its two ports\n";
    NodeGraph g;
    int s1 = g.addNode(NodeType::StepSignal);
    int s2 = g.addNode(NodeType::StepSignal);
    int tr = g.addNode(NodeType::ThermalResistance);
    int kH = g.addNode(NodeType::Oscilloscope);
    int kC = g.addNode(NodeType::Oscilloscope);
    auto* nr = g.findNode(tr);
    g.tryAddEdge(g.findNode(s1)->outputAttrId(), nr->inputAttrId(0));
    g.tryAddEdge(g.findNode(s2)->outputAttrId(), nr->inputAttrId(1));
    g.tryAddEdge(nr->outputAttrId(0), g.findNode(kH)->inputAttrId(0));
    g.tryAddEdge(nr->outputAttrId(1), g.findNode(kC)->inputAttrId(0));

    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 2);
    // Two distinct output assignments for the resistance node.
    std::string v0 = "v" + std::to_string(tr);
    std::string v1 = "v" + std::to_string(tr) + "_1";
    EXPECT_TRUE(plan.script.find(v0 + " = (") != std::string::npos);
    EXPECT_TRUE(plan.script.find(v1 + " = (") != std::string::npos);
}

static void test_codegen_view3d_thermal_sink_records_one_channel() {
    std::cout << "[70] Thermal: View3DThermalSink contributes 1 STATE channel\n";
    NodeGraph g;
    int src = g.addNode(NodeType::StepSignal);
    int sk  = g.addNode(NodeType::View3DThermalSink);
    g.tryAddEdge(g.findNode(src)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.sinkChannels.size() == 1);
    EXPECT_TRUE(plan.sinkChannels[0].nodeId == sk);
    EXPECT_TRUE(plan.sinkChannels[0].channel == 0);
}

static void test_codegen_thermal_mass_has_state_and_initial_ambient() {
    std::cout << "[69] Thermal: ThermalMass declares 1 state, IC = T_ambient\n";
    NodeGraph g;
    int sP = g.addNode(NodeType::StepSignal);
    int tm = g.addNode(NodeType::ThermalMass);
    int sk = g.addNode(NodeType::Oscilloscope);
    g.setParam(tm, "Ambient Temperature", 300.0);
    g.tryAddEdge(g.findNode(sP)->outputAttrId(),
                 g.findNode(tm)->inputAttrId(0));
    g.tryAddEdge(g.findNode(tm)->outputAttrId(),
                 g.findNode(sk)->inputAttrId(0));
    auto plan = ScilabCodeGen::generate(g);
    EXPECT_TRUE(plan.error.empty());
    EXPECT_TRUE(plan.script.find("State vector length: 1") != std::string::npos);
    // The ODE references the slot variable and the ambient param.
    EXPECT_TRUE(plan.script.find("x(1)") != std::string::npos);
    EXPECT_TRUE(plan.script.find("dxdt = [") != std::string::npos);
}

// -----------------------------------------------------------------------
// Section 7 — CustomNodeRegistry (Stage v0.7 addRule() hook)
//
// The registry parses JSON descriptors and stores them keyed by type_id.
// Built-in node types are unaffected; this is a parallel registry that
// future palette/codegen wiring can read from.
// -----------------------------------------------------------------------
static void test_custom_node_registry_transformer_round_trip() {
    std::cout << "[49] CustomNodeRegistry: valid transformer JSON round-trips\n";
    scinodes::CustomNodeRegistry reg;
    reg.clear();

    const char* j = R"({
        "type_id": "DoubleGain",
        "label": "Double Gain",
        "description": "Multiplies the input by 2*k.",
        "category": "transformer",
        "input_ports": 1,
        "output_ports": 1,
        "params": [
            {"name": "k", "default": 1.0, "units": "1"}
        ],
        "expression": "2 * p_k * u1"
    })";
    std::string err;
    EXPECT_TRUE(reg.loadFromJsonString(j, &err));
    EXPECT_TRUE(err.empty());

    const auto* d = reg.find("DoubleGain");
    EXPECT_TRUE(d != nullptr);
    if (d) {
        EXPECT_TRUE(d->label == "Double Gain");
        EXPECT_TRUE(d->category == NodeCategory::Transformer);
        EXPECT_TRUE(d->inputPorts  == 1);
        EXPECT_TRUE(d->outputPorts == 1);
        EXPECT_TRUE(d->params.size() == 1);
        if (!d->params.empty()) {
            EXPECT_TRUE(d->params[0].name == "k");
            EXPECT_TRUE(std::abs(d->params[0].defaultValue - 1.0) < 1e-9);
            EXPECT_TRUE(d->params[0].unit == "1");
        }
        EXPECT_TRUE(d->expression == "2 * p_k * u1");
    }
}

static void test_custom_node_registry_source_no_inputs() {
    std::cout << "[50] CustomNodeRegistry: source descriptors omit inputs / expression\n";
    scinodes::CustomNodeRegistry reg;
    reg.clear();

    const char* j = R"({
        "type_id": "WhiteNoise",
        "label": "White Noise",
        "category": "source",
        "input_ports": 0,
        "output_ports": 1,
        "params": [{"name": "sigma", "default": 0.1}]
    })";
    EXPECT_TRUE(reg.loadFromJsonString(j, nullptr));
    const auto* d = reg.find("WhiteNoise");
    EXPECT_TRUE(d != nullptr && d->category == NodeCategory::Source);
    EXPECT_TRUE(d != nullptr && d->expression.empty());
}

static void test_custom_node_registry_rejects_malformed_json() {
    std::cout << "[51] CustomNodeRegistry: malformed JSON is rejected with an error\n";
    scinodes::CustomNodeRegistry reg;
    reg.clear();
    std::string err;
    EXPECT_FALSE(reg.loadFromJsonString("{ this is not json }", &err));
    EXPECT_FALSE(err.empty());
    EXPECT_TRUE(reg.typeIds().empty());
}

static void test_custom_node_registry_rejects_duplicate() {
    std::cout << "[52] CustomNodeRegistry: duplicate type_id is rejected\n";
    scinodes::CustomNodeRegistry reg;
    reg.clear();
    const char* j = R"({
        "type_id": "Foo", "label": "Foo", "category": "transformer",
        "input_ports": 1, "output_ports": 1, "expression": "u1"
    })";
    EXPECT_TRUE(reg.loadFromJsonString(j, nullptr));
    std::string err;
    EXPECT_FALSE(reg.loadFromJsonString(j, &err));
    EXPECT_TRUE(err.find("Duplicate") != std::string::npos);
}

static void test_custom_node_registry_rejects_missing_field() {
    std::cout << "[53] CustomNodeRegistry: missing required field is rejected\n";
    scinodes::CustomNodeRegistry reg;
    reg.clear();
    // No "label"
    const char* j = R"({
        "type_id": "Bar", "category": "transformer",
        "input_ports": 1, "output_ports": 1, "expression": "u1"
    })";
    std::string err;
    EXPECT_FALSE(reg.loadFromJsonString(j, &err));
    EXPECT_TRUE(err.find("label") != std::string::npos);
}

static void test_custom_node_registry_transformer_requires_expression() {
    std::cout << "[54] CustomNodeRegistry: transformer without expression rejected\n";
    scinodes::CustomNodeRegistry reg;
    reg.clear();
    const char* j = R"({
        "type_id": "Baz", "label": "Baz", "category": "transformer",
        "input_ports": 1, "output_ports": 1
    })";
    std::string err;
    EXPECT_FALSE(reg.loadFromJsonString(j, &err));
    EXPECT_TRUE(err.find("expression") != std::string::npos);
}

static void test_custom_node_registry_source_with_inputs_rejected() {
    std::cout << "[55] CustomNodeRegistry: source with input_ports>0 rejected\n";
    scinodes::CustomNodeRegistry reg;
    reg.clear();
    const char* j = R"({
        "type_id": "BadSrc", "label": "Bad Source", "category": "source",
        "input_ports": 2, "output_ports": 1
    })";
    std::string err;
    EXPECT_FALSE(reg.loadFromJsonString(j, &err));
    EXPECT_TRUE(err.find("input_ports") != std::string::npos);
}

static void test_custom_node_registry_load_from_file() {
    std::cout << "[56] CustomNodeRegistry: loadFromFile reads a real path\n";
    scinodes::CustomNodeRegistry reg;
    reg.clear();

    // Write a tmp file.
    char tmpl[] = "/tmp/scnodes_custom_XXXXXX";
    int fd = ::mkstemp(tmpl);
    EXPECT_TRUE(fd >= 0);
    const char* body = R"({
        "type_id": "FromFile", "label": "From File", "category": "transformer",
        "input_ports": 1, "output_ports": 1, "expression": "u1 + 1"
    })";
    ::write(fd, body, std::string(body).size());
    ::close(fd);

    std::string err;
    EXPECT_TRUE(reg.loadFromFile(tmpl, &err));
    EXPECT_TRUE(reg.find("FromFile") != nullptr);
    std::remove(tmpl);
}

static void test_custom_node_registry_clear() {
    std::cout << "[57] CustomNodeRegistry: clear() empties the registry\n";
    scinodes::CustomNodeRegistry reg;
    const char* j = R"({
        "type_id": "Tmp", "label": "Tmp", "category": "transformer",
        "input_ports": 1, "output_ports": 1, "expression": "u1"
    })";
    reg.clear();
    EXPECT_TRUE(reg.loadFromJsonString(j, nullptr));
    EXPECT_TRUE(reg.typeIds().size() == 1);
    reg.clear();
    EXPECT_TRUE(reg.typeIds().empty());
}

// -----------------------------------------------------------------------
// Section 8 — Performance budget
//
// Planner contract: grammar validation must complete in < 1 ms for a
// 256-node graph on the reference machine. We build the canonical
// Source → Transformer × 254 → Sink chain and time both validateGraph()
// (whole-graph reachability) and a worst-case single-edge insertion
// against the full edge list.
// -----------------------------------------------------------------------
static double bench_us(const std::function<void()>& fn, int iters) {
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    for (int i = 0; i < iters; ++i) fn();
    auto t1 = clock::now();
    return std::chrono::duration<double, std::micro>(t1 - t0).count()
           / static_cast<double>(iters);
}

static void test_grammar_perf_256_node_graph() {
    std::cout << "[48] Perf: 256-node graph validates in < 1 ms\n";
    constexpr int kNodes = 256;
    std::vector<NodeInstance> nodes;
    std::vector<Edge>         edges;
    nodes.reserve(kNodes);
    edges.reserve(kNodes - 1);

    nodes.push_back(makeNode(1, NodeType::SineSignal));        // source
    for (int i = 2; i < kNodes; ++i)
        nodes.push_back(makeNode(i, NodeType::Gain));          // transformers
    nodes.push_back(makeNode(kNodes, NodeType::Oscilloscope)); // sink
    for (int i = 1; i < kNodes; ++i)
        edges.push_back({ i, i, i + 1,
                          i * 1000 + 1, (i + 1) * 1000 + 0 });

    GrammarParser p;

    // Sanity: the chain is Valid before we measure timing.
    EXPECT_TRUE(p.validateGraph(nodes, edges) == GrammarState::Valid);

    // Warm-up pass to populate any caches.
    for (int i = 0; i < 5; ++i) (void)p.validateGraph(nodes, edges);

    double avgGraphUs = bench_us(
        [&]{ (void)p.validateGraph(nodes, edges); },
        200);
    std::cout << "      validateGraph(256 nodes): " << avgGraphUs << " us\n";
    EXPECT_TRUE(avgGraphUs < 1000.0);   // < 1 ms

    // Worst-case incremental edge check: validate one new candidate against
    // the existing 255-edge list. This is what the UI calls every time the
    // user drags a wire, so it has to stay cheap.
    NodeInstance extraSrc = makeNode(9001, NodeType::StepSignal);
    // Target a Gain node already wired into the chain — worst case is one
    // whose input port is full so R5 has to walk the edge list.
    NodeInstance extraDst = nodes[kNodes / 2 - 1];
    double avgEdgeUs = bench_us(
        [&]{ (void)p.validateEdge(extraSrc, extraDst, edges); },
        500);
    std::cout << "      validateEdge vs 255 edges: " << avgEdgeUs << " us\n";
    EXPECT_TRUE(avgEdgeUs < 1000.0);    // < 1 ms
}

// -----------------------------------------------------------------------
// CsvParamIO — round-trip a node's param block to/from CSV.
// -----------------------------------------------------------------------
static void test_csv_param_io_roundtrip_design_template() {
    std::cout << "[60] CsvParamIO: DesignTemplate round-trips through CSV\n";
    NodeGraph g;
    int dt = g.addNode(NodeType::DesignTemplate);
    g.setParam(dt, "Target Torque",  42.0);
    g.setParam(dt, "Target Speed",  314.159);
    g.setParam(dt, "Bus Voltage",   800.0);
    g.setParam(dt, "Cooling Class",   3.0);

    auto path = makeTempCsvPath();
    std::string err;
    EXPECT_TRUE(scinodes::writeNodeParamsCsv(
        path, *g.findNode(dt), "Design Template #1", &err));
    EXPECT_TRUE(err.empty());

    // Load into a fresh node and verify it sees the four written values.
    NodeInstance reloaded = makeNode(99, NodeType::DesignTemplate);
    std::vector<std::string> warns;
    EXPECT_TRUE(scinodes::readNodeParamsCsv(path, reloaded, &err, &warns));
    EXPECT_TRUE(err.empty());
    EXPECT_TRUE(warns.empty());
    EXPECT_TRUE(std::abs(reloaded.params.at("Target Torque")  -  42.0)    < 1e-9);
    EXPECT_TRUE(std::abs(reloaded.params.at("Target Speed")   - 314.159)  < 1e-6);
    EXPECT_TRUE(std::abs(reloaded.params.at("Bus Voltage")    - 800.0)    < 1e-9);
    EXPECT_TRUE(std::abs(reloaded.params.at("Cooling Class")  -   3.0)    < 1e-9);
    std::remove(path.c_str());
}

static void test_csv_param_io_unknown_keys_become_warnings() {
    std::cout << "[61] CsvParamIO: unknown parameter rows are surfaced as warnings\n";
    auto path = makeTempCsvPath();
    {
        std::ofstream f(path);
        f << "# manually-typed file\n"
          << "parameter,value,units\n"
          << "Target Torque,55,Nm\n"
          << "Bogus Key,1,?\n";
    }
    NodeInstance n = makeNode(7, NodeType::DesignTemplate);
    std::string err;
    std::vector<std::string> warns;
    EXPECT_TRUE(scinodes::readNodeParamsCsv(path, n, &err, &warns));
    EXPECT_TRUE(err.empty());
    EXPECT_TRUE(warns.size() == 1);
    EXPECT_TRUE(warns[0].find("Bogus Key") != std::string::npos);
    // Known key applied, unknown ignored, others left at defaults.
    EXPECT_TRUE(std::abs(n.params.at("Target Torque") - 55.0) < 1e-9);
    EXPECT_TRUE(std::abs(n.params.at("Target Speed")  - 150.0) < 1e-9);  // default
    std::remove(path.c_str());
}

static void test_csv_param_io_partial_input_keeps_other_defaults() {
    std::cout << "[62] CsvParamIO: partial CSV only changes the rows it lists\n";
    auto path = makeTempCsvPath();
    {
        std::ofstream f(path);
        f << "parameter,value\n"
          << "Bus Voltage,1000\n";   // single row, no units column
    }
    NodeInstance n = makeNode(3, NodeType::DesignTemplate);
    EXPECT_TRUE(scinodes::readNodeParamsCsv(path, n, nullptr, nullptr));
    EXPECT_TRUE(std::abs(n.params.at("Bus Voltage")    - 1000.0) < 1e-9);
    EXPECT_TRUE(std::abs(n.params.at("Target Torque") -   10.0) < 1e-9);   // default
    EXPECT_TRUE(std::abs(n.params.at("Target Speed")  -  150.0) < 1e-9);   // default
    std::remove(path.c_str());
}

static void test_csv_param_io_rejects_missing_header() {
    std::cout << "[63] CsvParamIO: missing header line is rejected\n";
    auto path = makeTempCsvPath();
    {
        std::ofstream f(path);
        f << "Target Torque,55,Nm\n";   // no header
    }
    NodeInstance n = makeNode(7, NodeType::DesignTemplate);
    std::string err;
    EXPECT_FALSE(scinodes::readNodeParamsCsv(path, n, &err, nullptr));
    EXPECT_TRUE(err.find("header") != std::string::npos);
    std::remove(path.c_str());
}

static void test_csv_param_io_fem_sidecar_round_trip() {
    std::cout << "[65] CsvParamIO: FEM-sidecar CSV imports into PMSMElectromagnetic\n";
    auto path = makeTempCsvPath();
    {
        // Format mirrors doc/fem_sidecar/pmsm_lumped_corrections.py output
        // (Carter coefficient + magnet leakage + end-winding corrections
        // applied to the sample_input.json defaults).
        std::ofstream f(path);
        f << "# SciNodes parameters from pmsm_lumped_corrections.py\n"
          << "# k_carter: 1.23931\n"
          << "# Bg_effective_T: 0.8075\n"
          << "# Ke_VsPerRad: 1.8411\n"
          << "parameter,value,units\n"
          << "Turns per Phase,100,\n"
          << "Winding Factor,0.95,\n"
          << "Pole Pairs,4,\n"
          << "Airgap Flux Density,0.8075,T\n"
          << "Mechanical Airgap,0.001,m\n"
          << "Magnet Thickness,0.003,m\n"
          << "Slot Count,24,\n";
    }
    NodeInstance n = makeNode(11, NodeType::PMSMElectromagnetic);
    std::string err;
    std::vector<std::string> warns;
    EXPECT_TRUE(scinodes::readNodeParamsCsv(path, n, &err, &warns));
    EXPECT_TRUE(err.empty());
    EXPECT_TRUE(warns.empty());
    // The corrected airgap flux density landed in the node.
    EXPECT_TRUE(std::abs(n.params.at("Airgap Flux Density") - 0.8075) < 1e-6);
    // Other defaults that the CSV listed are unchanged from their
    // sidecar-emitted values.
    EXPECT_TRUE(std::abs(n.params.at("Turns per Phase")  - 100.0) < 1e-9);
    EXPECT_TRUE(std::abs(n.params.at("Mechanical Airgap") - 0.001) < 1e-9);
    std::remove(path.c_str());
}

static void test_csv_param_io_rejects_non_numeric() {
    std::cout << "[64] CsvParamIO: non-numeric value reports an error and aborts\n";
    auto path = makeTempCsvPath();
    {
        std::ofstream f(path);
        f << "parameter,value,units\n"
          << "Target Torque,big number,Nm\n";
    }
    NodeInstance n = makeNode(7, NodeType::DesignTemplate);
    std::string err;
    EXPECT_FALSE(scinodes::readNodeParamsCsv(path, n, &err, nullptr));
    EXPECT_TRUE(err.find("Non-numeric") != std::string::npos);
    // Failed parse must NOT mutate the node — Target Torque stays at default.
    EXPECT_TRUE(std::abs(n.params.at("Target Torque") - 10.0) < 1e-9);
    std::remove(path.c_str());
}

static void test_csv_export_bad_path_reports_error() {
    std::cout << "[47] CsvExport: unwritable path returns false with error message\n";
    std::vector<float> buf(4, 1.0f);
    std::string err;
    bool ok = scinodes::writeSinkCsv(
        "/this/path/should/not/exist/scinodes_test.csv",
        buf, 4, 0.0f, 0.1f, "", &err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());
}

// -----------------------------------------------------------------------
int main() {
    std::cout << "=== GrammarParser + ScnSerializer + Fft unit tests ===\n\n";

    // Edge-level
    test_edge_source_to_transformer();
    test_edge_source_to_sink();
    test_edge_transformer_to_transformer();
    test_edge_transformer_to_sink();
    test_edge_sink_to_anything();
    test_edge_anything_to_source();
    test_edge_source_to_source();
    test_edge_self_loop();
    test_edge_duplicate();
    test_edge_multiport_same_pair_allowed();
    test_edge_input_occupied();
    test_edge_signal_into_geometry_rejected();
    test_edge_geometry_into_signal_rejected();
    test_edge_geometry_into_param_rejected();
    test_edge_geometry_to_geometry_valid();
    test_edge_signal_into_transform_signal_port_rejected_now();
    test_edge_combinexyz_into_transform_signal_port_valid();
    test_edge_geometry_into_scene_output_valid();
    test_graph_r6_via_tryAddEdge();

    // TypeExpr — fundación gramatical (etapa 1)
    test_typeexpr_scalar_matches_scalar();
    test_typeexpr_vec3_matches_vec3();
    test_typeexpr_vec3_vs_vec2();
    test_typeexpr_scalar_vs_vec3();
    test_typeexpr_matrix();
    test_typeexpr_geometry();
    test_typeexpr_describe();
    test_typeexpr_predicates();
    test_typeexpr_pin_colors_distinct();
    test_typeexpr_vec_size_irrelevant_to_color();

    // Etapa 6A — álgebra de Unit
    test_unit_default_is_dimensionless();
    test_unit_base_constructors_distinct();
    test_unit_prefix_preserves_dimension();
    test_unit_multiplication_VA_equals_W();
    test_unit_multiplication_Nm_equals_J();
    test_unit_division_V_per_A_equals_Ohm();
    test_unit_division_inverse_dimensions();
    test_unit_pow_square_meter();
    test_unit_pow_negative_inverse();
    test_unit_dimensionless_is_neutral_for_multiplication();
    test_unit_radians_per_second_equals_Hz_dimensionally();
    test_unit_equality_distinguishes_magnitude();

    // Etapa 6B — parser textual
    test_unit_parse_empty_is_error();
    test_unit_parse_base_units();
    test_unit_parse_derived();
    test_unit_parse_prefix();
    test_unit_parse_da_prefix_longest_match();
    test_unit_parse_product_center_dot();
    test_unit_parse_product_asterisk();
    test_unit_parse_division();
    test_unit_parse_power();
    test_unit_parse_complex_expression();
    test_unit_parse_parens();
    test_unit_parse_Nm_equals_J();
    test_unit_parse_rad_per_s_distinct_from_Hz();
    test_unit_parse_whitespace_ignored();
    test_unit_parse_unknown_rejects();
    test_unit_parse_unclosed_paren_rejects();

    // Etapa 6C — catálogo canónico + display
    test_units_catalog_matches_parser();
    test_units_catalog_compound_names();
    test_unit_toString_dimensionless_vs_angle();
    test_unit_toString_named_units();
    test_unit_toString_with_prefix();
    test_unit_toString_decomposition();
    test_unit_toString_roundtrip_through_parser();

    // Etapa 6D — unidades declaradas en el registry
    test_registry_declares_voltage_source_volts();
    test_registry_declares_current_source_amperes();
    test_registry_dcmotor_units();
    test_registry_polimorphic_nodes_have_no_units();
    test_registry_gear_transmission_radps();

    // Etapa 6E — propagación dimensional
    test_dimanalyzer_voltage_source_seeds_volts();
    test_dimanalyzer_backward_propagation_into_polymorphic();
    test_dimanalyzer_forward_propagation();
    test_dimanalyzer_conflict_VA_into_Sum();
    test_dimanalyzer_dcmotor_output_radps_propagates();
    test_dimanalyzer_isolated_polymorphic_unresolved();
    test_dimanalyzer_feedback_loop_detects_conflict();
    test_r7_optin_rejects_current_into_voltage_input();
    test_r7_optin_accepts_voltage_into_voltage_input();
    test_r7_optin_accepts_polymorphic_chain();
    test_r7_default_off_lets_through_conflict();

    // Etapa 6G — overrides per-instance
    test_override_seeds_polymorphic_port();
    test_override_ignored_on_dimensioned_node();
    test_override_pid_as_unit_transformer_closes_loop();
    test_override_serialization_roundtrip();
    test_override_rad_preserves_text_through_serialization();

    // Etapa 6H — conversores DegToRad / RadToDeg
    test_converter_degtorad_units_declared();
    test_converter_radtodeg_units_declared();
    test_converter_codegen_emits_factor();
    test_converter_r7_accepts_explicit_conversion();

    // Etapa 6I.A — Quantity + parseQuantity
    test_quantity_parse_number_with_unit();
    test_quantity_parse_centimeters();
    test_quantity_parse_bare_number_is_dimensionless();
    test_quantity_parse_bare_unit_implies_value_one();
    test_quantity_parse_with_space();
    test_quantity_parse_negative_signed();
    test_quantity_parse_scientific_notation();
    test_quantity_parse_compound_unit();
    test_quantity_parse_center_dot();
    test_quantity_parse_empty_is_error();
    test_quantity_parse_unknown_unit_is_error();
    test_quantity_equivalent_si_across_prefixes();
    test_quantity_equivalent_si_different_dim_is_false();
    test_quantity_to_display_string();
    test_quantity_to_si_with_kilo_prefix();
    test_quantity_round_trip_all_si_prefixes();
    test_quantity_parse_has_unit_flag();
    test_quantity_si_value_independent_of_typed_prefix();

    // Etapa 6I.B — FieldDef + synthesizeFields
    test_fields_order_inputs_params_outputs();
    test_fields_voltage_source_declares_volts();
    test_fields_dc_motor_declares_in_v_out_radps();
    test_fields_pid_inputs_are_polymorphic();
    test_fields_pid_params_have_default_quantity();
    test_fields_transform_object_geometry_field_is_geometry_type();
    test_fields_dc_motor_param_units_parse_from_string();
    test_fields_step_signal_polymorphic_output();
    test_fields_label_falls_back_to_index();

    // Etapa 6I.C — displayUnits del proyecto
    test_display_units_default_empty();
    test_display_units_set_and_query();
    test_display_units_replaces_same_dimension();
    test_display_units_clear();
    test_display_units_canonicalize_converts_value();
    test_display_units_canonicalize_no_preference_unchanged();
    test_display_units_canonicalize_to_kilo_volts();
    test_display_units_serialization_roundtrip();
    test_display_units_idempotent_when_already_canonical();

    // Etapa 6I.D.1 — NodeInstance::fields
    test_node_instance_seeds_fields_from_def();
    test_node_instance_field_value_matches_param();
    test_node_instance_field_unit_from_registry();
    test_node_instance_setparam_mirrors_to_fields();
    test_node_instance_setparam_preserves_unit();
    test_unit_display_prefers_ascii_ohm();
    test_field_dimensional_acceptance_logic();
    test_context_aware_prefix_in_ohm_field();
    test_context_aware_prefix_in_volt_field();
    test_context_aware_milli_prefix();
    test_context_aware_micro_prefix();
    test_context_aware_explicit_unit_wins();
    test_context_aware_bare_number_preserves();
    test_field_bare_number_resets_to_canonical();
    test_field_bare_number_resets_voltage_canonical();
    test_ideal_field_accepts_prefix_only();
    test_context_aware_invalid_prefix_falls_back_to_error();
    test_codegen_emits_toSI_value_for_voltage_source();
    test_codegen_emits_toSI_value_for_length();
    test_oscilloscope_unit_inferred_from_source();
    test_oscilloscope_unit_from_dc_motor_is_radps();

    // Etapa 6I.K — Integrator/Differentiator como unit-transformers
    test_integrator_multiplies_unit_by_seconds();
    test_integrator_propagates_backward();
    test_differentiator_divides_unit_by_seconds();
    test_domain_unit_default_seconds();
    test_domain_unit_drives_integrator_factor();
    test_domain_unit_serialization_roundtrip();
    test_phantom_angle_distinguishes_rad_from_dimensionless();
    test_phantom_angle_deg_and_rad_share_dimension();
    test_phantom_angle_radps_distinct_from_hz();
    test_integrator_then_oscilloscope_walkthrough_e1();
    test_node_instance_port_fields_value_zero();

    // Etapa 4 — nodos Vec3 + TransformObject vec3
    test_vec3_nodes_registered_with_correct_types();
    test_vec3_walker_reads_vec3_constant();
    test_vec3_r6_rejects_scalar_to_vec3();
    test_vec3_r6_accepts_vec3_to_vec3();

    // Graph-level
    test_graph_empty();
    test_graph_incomplete_no_edges();
    test_graph_valid_source_sink();
    test_graph_valid_chain();
    test_graph_rejected_sink_to_tx();

    // Undo/Redo
    test_undo_add_node();
    test_undo_add_edge();
    test_redo();
    test_undo_stack_limit();

    // ScnSerializer
    test_serializer_roundtrip();
    test_serializer_objects_roundtrip();
    test_serializer_legacy_0_4_loads_empty_catalog();
    test_serializer_objects_emitted_only_if_populated();
    test_serializer_geometry_nodes_roundtrip();
    test_serializer_objects_addremove_dedup();

    // SceneCollector — walker headless del sub-grafo Geometry
    test_scene_empty_graph_yields_no_renderables();
    test_scene_object_into_scene_output_one_renderable();
    test_scene_objectref_with_part_splits();
    test_scene_missing_catalog_entry_yields_null_asset();
    test_scene_object_through_transform_to_output();
    test_scene_two_objects_one_output();
    test_scene_signal_subgraph_ignored_by_walker();
    test_scene_transform_reads_rotation_from_bridge_tap();
    test_scene_transform_without_tap_stays_identity();
    test_vec3_walker_reads_vec3_constant();
    test_vec3_math_add();
    test_vec3_math_sub();
    test_vec3_math_cross();
    test_vec3_math_normalize();
    test_vec3_math_normalize_zero();
    test_vec3_math_chained();
    test_scene_transform_without_bridge_stays_identity();
    test_serializer_rejects_bad_edge();
    test_serializer_unknown_type();
    test_serializer_fatal_json_error();
    test_serializer_root_metadata_roundtrip();
    test_serializer_root_metadata_absent_legacy_file();

    // ScilabCodeGen
    test_codegen_simple_chain();
    test_codegen_every_type_supported();
    test_codegen_geometry_nodes_are_not_emittable();
    test_codegen_empty_graph();
    test_codegen_summation_two_inputs();
    test_codegen_integrator_emits_dynamics();
    test_codegen_dcmotor_two_states();
    test_codegen_stateless_skips_ode();
    test_codegen_pid_uses_state();
    test_codegen_param_dispatch_emitted();
    test_codegen_closed_loop_through_integrator();
    test_codegen_algebraic_loop_rejected();
    test_codegen_differentiator_supported();
    test_codegen_transfer_function_supported();
    test_codegen_transfer_function2();
    test_codegen_inverse_kinematics();
    test_codegen_emits_nan_guard();
    test_codegen_phaseportrait_two_channels();
    test_codegen_view3d_sink_supported();
    test_codegen_design_template_emits_four_outputs();
    test_codegen_pmsm_sizing_emits_cube_root();
    test_pmsm_sizing_has_slot_and_pole_params();
    test_codegen_pmsm_electromagnetic_emits_formulas();
    test_codegen_airgap_flux_density_state_and_harmonics();
    test_codegen_pmsm_efficiency_emits_loss_terms();
    test_codegen_heatmap_sink_three_channels();
    test_codegen_joule_loss_emits_iq_squared();
    test_codegen_core_loss_uses_electrical_frequency();
    test_codegen_mechanical_loss_emits_quadratic_term();
    test_codegen_thermal_mass_has_state_and_initial_ambient();
    test_codegen_view3d_thermal_sink_records_one_channel();
    test_codegen_thermal_node_sums_four_inputs();
    test_codegen_thermal_resistance_two_outputs();
    test_codegen_cooling_system_three_outputs();
    test_codegen_convective_cooling_h_of_flow();
    test_codegen_maxwell_force_emits_mu0_form();
    test_codegen_modal_frequency_emits_shape_factor();
    test_codegen_view3d_deformation_sink_three_channels();
    test_codegen_tolerance_perturbator_uses_rand();
    test_codegen_distribution_sink_records_input();

    // Fft helper
    test_fft_pow2_check();
    test_fft_peak_at_expected_bin();
    test_fft_dc_only_input();
    test_fft_non_pow2_rejected();

    // CsvExport
    test_csv_export_partial_buffer();
    test_csv_export_wrapped_buffer();
    test_csv_export_empty_buffer();
    test_csv_export_bad_path_reports_error();
    test_csv_param_io_roundtrip_design_template();
    test_csv_param_io_unknown_keys_become_warnings();
    test_csv_param_io_partial_input_keeps_other_defaults();
    test_csv_param_io_rejects_missing_header();
    test_csv_param_io_rejects_non_numeric();
    test_csv_param_io_fem_sidecar_round_trip();

    // CustomNodeRegistry (addRule hook)
    test_custom_node_registry_transformer_round_trip();
    test_custom_node_registry_source_no_inputs();
    test_custom_node_registry_rejects_malformed_json();
    test_custom_node_registry_rejects_duplicate();
    test_custom_node_registry_rejects_missing_field();
    test_custom_node_registry_transformer_requires_expression();
    test_custom_node_registry_source_with_inputs_rejected();
    test_custom_node_registry_load_from_file();
    test_custom_node_registry_clear();

    // Performance
    test_grammar_perf_256_node_graph();

    std::cout << "\n=== " << g_pass << " passed, " << g_fail << " failed ===\n";
    return g_fail > 0 ? 1 : 0;
}
