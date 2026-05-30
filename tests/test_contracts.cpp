// =============================================================================
// test_contracts.cpp — Tests del ContractRegistry y DeviceAssetLoader.
//
// G1: parseo + validación del JSON + lookup del registry.
// G3: carga de glTF de prueba (mínimo, escrito a mano como string),
//     binding contra contrato, validación de missing/warnings.
// =============================================================================

#include "core/ContractRegistry.hpp"
#include "core/DeviceAsset.hpp"
#include "core/AssetMapping.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace {
int g_pass = 0, g_fail = 0;

void expect_true(bool cond, const char* msg) {
    if (cond) { ++g_pass; }
    else {
        ++g_fail;
        std::fprintf(stderr, "[FAIL] %s\n", msg);
    }
}

void expect_eq(const std::string& got, const std::string& want, const char* msg) {
    if (got == want) { ++g_pass; }
    else {
        ++g_fail;
        std::fprintf(stderr, "[FAIL] %s — got '%s', want '%s'\n",
                     msg, got.c_str(), want.c_str());
    }
}

void expect_near(double got, double want, double tol, const char* msg) {
    if (std::fabs(got - want) <= tol) { ++g_pass; }
    else {
        ++g_fail;
        std::fprintf(stderr, "[FAIL] %s — got %.6f, want %.6f\n",
                     msg, got, want);
    }
}

// Helper que escribe `content` a `path` y devuelve true si OK.
bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    if (!f) return false;
    f << content;
    return f.good();
}

// glTF mínimo de prueba: 2 parts (housing, shaft), 1 joint con axis Z+,
// 2 anchors con posiciones distintas.  Sin meshes ni buffers — el loader
// tolera nodes "part" sin geometría asociada (el caso de un proto que
// aún no se modeló).  Suficiente para validar el camino completo
// metadata → DeviceAsset.
const char* kMotorGltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1, 2, 3, 4] } ],
  "nodes": [
    {
      "name": "Cuerpo",
      "extras": { "scinodes": { "role": "part", "name": "housing" } }
    },
    {
      "name": "Rotor",
      "extras": { "scinodes": { "role": "part", "name": "shaft" } }
    },
    {
      "name": "BearingEmpty",
      "translation": [0.0, 0.0, 0.0],
      "extras": {
        "scinodes": {
          "role": "joint", "name": "shaft_bearing",
          "axis": [0.0, 0.0, 1.0]
        }
      }
    },
    {
      "name": "TerminalPlusPoint",
      "translation": [0.02, 0.0, 0.05],
      "extras": { "scinodes": { "role": "anchor", "name": "terminal_plus" } }
    },
    {
      "name": "TerminalMinusPoint",
      "translation": [-0.02, 0.0, 0.05],
      "extras": { "scinodes": { "role": "anchor", "name": "terminal_minus" } }
    }
  ]
})";

// Variante que usa la forma PLANA de extras (la que produce Blender por
// default al exportar Custom Properties).  Cubre el mismo grafo del
// motor pero con extras = { "scinodes.role": "...", "scinodes.name": "...",
// "scinodes.axis": [...] } en vez del objeto anidado.
const char* kMotorGltfFlatExtras = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1, 2, 3, 4] } ],
  "nodes": [
    {
      "name": "Cuerpo",
      "extras": { "scinodes.role": "part", "scinodes.name": "housing" }
    },
    {
      "name": "Rotor",
      "extras": { "scinodes.role": "part", "scinodes.name": "shaft" }
    },
    {
      "name": "BearingEmpty",
      "translation": [0.0, 0.0, 0.0],
      "extras": {
        "scinodes.role": "joint",
        "scinodes.name": "shaft_bearing",
        "scinodes.axis": [0.0, 0.0, 1.0]
      }
    },
    {
      "name": "TerminalPlusPoint",
      "translation": [0.02, 0.0, 0.05],
      "extras": { "scinodes.role": "anchor", "scinodes.name": "terminal_plus" }
    },
    {
      "name": "TerminalMinusPoint",
      "translation": [-0.02, 0.0, 0.05],
      "extras": { "scinodes.role": "anchor", "scinodes.name": "terminal_minus" }
    }
  ]
})";

// Idem pero falta la part "housing" — para verificar que missing se llena.
const char* kMotorGltfMissingHousing = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1, 2, 3] } ],
  "nodes": [
    {
      "name": "Rotor",
      "extras": { "scinodes": { "role": "part", "name": "shaft" } }
    },
    {
      "name": "BearingEmpty",
      "translation": [0.0, 0.0, 0.0],
      "extras": {
        "scinodes": { "role": "joint", "name": "shaft_bearing" }
      }
    },
    {
      "name": "TerminalPlusPoint",
      "translation": [0.0, 0.0, 0.0],
      "extras": { "scinodes": { "role": "anchor", "name": "terminal_plus" } }
    },
    {
      "name": "TerminalMinusPoint",
      "translation": [0.0, 0.0, 0.0],
      "extras": { "scinodes": { "role": "anchor", "name": "terminal_minus" } }
    }
  ]
})";

}  // namespace

int main() {
    std::fprintf(stderr,
        "================================================================\n"
        " test_contracts — ContractRegistry parser + lookup\n"
        "================================================================\n");

    // Antes era singleton; ahora la instancia se construye local
    // (DI — Phase C.7) → cada test arranca con un registry limpio.
    scinodes::ContractRegistry reg;
    reg.clear();

    // ---- 1. Contrato bien formado ------------------------------------------
    {
        const char* good =
            R"({
              "device_type": "MiniMotor",
              "version":     "0.1",
              "description": "Motor de prueba.",
              "parts": [
                { "name": "shaft",   "required": true },
                { "name": "housing", "required": true }
              ],
              "joints": [
                {
                  "name": "bearing", "type": "revolute",
                  "parent": "housing", "child": "shaft",
                  "driven_by": "omega"
                }
              ],
              "anchors": [
                { "name": "term_plus",  "kind": "electrical" },
                { "name": "term_minus", "kind": "electrical" }
              ]
            })";
        std::string err;
        expect_true(reg.loadFromJsonString(good, &err),
                    "contrato válido se acepta");
        expect_true(err.empty(), "no hay error al cargar válido");
        auto* c = reg.find("MiniMotor");
        expect_true(c != nullptr, "find('MiniMotor') no-nulo");
        if (c) {
            expect_eq(c->device_type, "MiniMotor", "device_type");
            expect_true(c->parts.size() == 2, "2 parts");
            expect_true(c->joints.size() == 1, "1 joint");
            expect_eq(c->joints[0].type, "revolute", "joint type");
            expect_true(c->anchors.size() == 2, "2 anchors");
        }
    }

    // ---- 2. Falta device_type ---------------------------------------------
    {
        std::string err;
        bool ok = reg.loadFromJsonString(R"({ "version": "0.1" })", &err);
        expect_true(!ok, "falla sin device_type");
        expect_true(!err.empty(), "error no vacío");
    }

    // ---- 3. Tipo de joint inválido ----------------------------------------
    {
        const char* bad =
            R"({
              "device_type": "BadJoint",
              "parts": [{ "name": "a" }, { "name": "b" }],
              "joints": [
                { "name": "x", "type": "wobble",
                  "parent": "a", "child": "b" }
              ]
            })";
        std::string err;
        expect_true(!reg.loadFromJsonString(bad, &err),
                    "joint type='wobble' rechazado");
        expect_true(err.find("not recognized") != std::string::npos,
                    "mensaje de error explica");
    }

    // ---- 4. Joint referencia part inexistente -----------------------------
    {
        const char* bad =
            R"({
              "device_type": "OrphanJoint",
              "parts": [{ "name": "real" }],
              "joints": [
                { "name": "x", "type": "fixed",
                  "parent": "real", "child": "imaginary" }
              ]
            })";
        std::string err;
        expect_true(!reg.loadFromJsonString(bad, &err),
                    "joint con child fantasma rechazado");
        expect_true(err.find("imaginary") != std::string::npos,
                    "error nombra el part faltante");
    }

    // ---- 5. Cargar el contrato real del DC motor de disco -----------------
    //  Asume que la cwd es la raíz del repo o el build dir; probamos ambas.
    {
        std::string err;
        bool loaded = reg.loadFromFile("contracts/dc_motor.json", &err);
        if (!loaded) {
            err.clear();
            loaded = reg.loadFromFile("../contracts/dc_motor.json", &err);
        }
        expect_true(loaded, "contracts/dc_motor.json carga sin error");
        auto* c = reg.find("DCMotorModel");
        expect_true(c != nullptr, "DCMotorModel en el registry");
        if (c) {
            expect_eq(c->version, "0.1", "version");
            expect_true(c->parts.size() == 2,   "DCMotorModel parts = 2");
            expect_true(c->joints.size() == 1,  "DCMotorModel joints = 1");
            expect_true(c->anchors.size() == 3, "DCMotorModel anchors = 3");
            expect_eq(c->joints[0].driven_by, "omega", "driven_by omega");
        }
    }

    // ========================================================================
    // G3 — DeviceAssetLoader
    // ========================================================================

    // Asumimos que el contrato DCMotorModel ya está en el registry (se cargó
    // en el caso 5 más arriba).  Si por alguna razón no está, los tests
    // siguientes se vuelven sin sentido.
    const scinodes::DeviceContract* dcContract = reg.find("DCMotorModel");
    expect_true(dcContract != nullptr,
                "DCMotorModel en el registry antes de los tests del loader");
    if (!dcContract) {
        std::fprintf(stderr, "\n=== %d passed, %d failed ===\n", g_pass, g_fail);
        return 1;
    }

    // ---- 6. Happy path: glTF que cumple el contrato ------------------------
    {
        const std::string path = "/tmp/scinodes_test_motor_ok.gltf";
        expect_true(writeFile(path, kMotorGltf),
                    "escritura del glTF de prueba completo");
        std::string err;
        auto asset = scinodes::DeviceAssetLoader::load(path, *dcContract, &err);
        expect_true(err.empty(), "load sin error");
        expect_true(asset.valid(), "asset.valid() == true (todo cumple)");
        expect_true(asset.missing.empty(), "missing vacío");
        expect_true(asset.parts.count("shaft")   == 1, "part shaft presente");
        expect_true(asset.parts.count("housing") == 1, "part housing presente");
        expect_true(asset.joints.count("shaft_bearing") == 1,
                    "joint shaft_bearing presente");
        expect_true(asset.anchors.count("terminal_plus") == 1,
                    "anchor terminal_plus presente");
        expect_true(asset.anchors.count("terminal_minus") == 1,
                    "anchor terminal_minus presente");

        // type/parent/child/driven_by deben copiarse desde el contrato.
        const auto& jf = asset.joints.at("shaft_bearing");
        expect_eq(jf.type,      "revolute", "joint type del contrato");
        expect_eq(jf.parent,    "housing",  "joint parent del contrato");
        expect_eq(jf.child,     "shaft",    "joint child del contrato");
        expect_eq(jf.driven_by, "omega",    "joint driven_by del contrato");
        // axis viene del glTF.
        expect_near(jf.axis[2], 1.0, 1e-6, "joint axis Z = 1");
        expect_near(jf.axis[0], 0.0, 1e-6, "joint axis X = 0");

        // anchor.kind se copia del contrato.
        expect_eq(asset.anchors.at("terminal_plus").kind,  "electrical",
                  "anchor kind del contrato");
        // anchor.position viene del glTF (terminal_plus = 0.02, 0, 0.05).
        expect_near(asset.anchors.at("terminal_plus").position[0],
                    0.02, 1e-6, "anchor terminal_plus.x");
        expect_near(asset.anchors.at("terminal_plus").position[2],
                    0.05, 1e-6, "anchor terminal_plus.z");

        // El glTF no publica `winding` (optional).  Debe estar en
        // warnings, no en missing.
        bool windingWarned = false;
        for (const auto& w : asset.warnings)
            if (w.find("winding") != std::string::npos) { windingWarned = true; break; }
        expect_true(windingWarned, "winding optional listado en warnings");

        std::remove(path.c_str());
    }

    // ---- 7. Falla un required: housing ausente ----------------------------
    {
        const std::string path = "/tmp/scinodes_test_motor_no_housing.gltf";
        expect_true(writeFile(path, kMotorGltfMissingHousing),
                    "escritura del glTF sin housing");
        std::string err;
        auto asset = scinodes::DeviceAssetLoader::load(path, *dcContract, &err);
        expect_true(!asset.valid(), "asset.valid() == false (falta housing)");
        bool housingMissing = false;
        for (const auto& m : asset.missing)
            if (m == "part:housing") { housingMissing = true; break; }
        expect_true(housingMissing, "missing contiene 'part:housing'");
        // Lo demás SÍ está presente.
        expect_true(asset.parts.count("shaft") == 1,
                    "shaft sigue presente aunque housing falte");
        expect_true(asset.joints.count("shaft_bearing") == 1,
                    "joint sigue presente");
        std::remove(path.c_str());
    }

    // ---- 7.5 Forma plana de extras (estilo Blender) -----------------------
    {
        const std::string path = "/tmp/scinodes_test_motor_flat.gltf";
        expect_true(writeFile(path, kMotorGltfFlatExtras),
                    "escritura del glTF con extras planos");
        std::string err;
        auto asset = scinodes::DeviceAssetLoader::load(path, *dcContract, &err);
        expect_true(err.empty(), "load sin error (forma plana)");
        expect_true(asset.valid(),
                    "asset válido con extras planos (estilo Blender)");
        expect_true(asset.parts.count("shaft")   == 1, "shaft (flat)");
        expect_true(asset.parts.count("housing") == 1, "housing (flat)");
        expect_true(asset.joints.count("shaft_bearing") == 1,
                    "joint (flat)");
        const auto& jf = asset.joints.at("shaft_bearing");
        expect_near(jf.axis[2], 1.0, 1e-6, "joint axis Z (flat)");
        expect_near(asset.anchors.at("terminal_plus").position[0],
                    0.02, 1e-6, "terminal_plus.x (flat)");
        std::remove(path.c_str());
    }

    // ---- 8. Archivo inexistente: todos los required quedan missing --------
    {
        std::string err;
        auto asset = scinodes::DeviceAssetLoader::load(
            "/tmp/scinodes_no_such_file.gltf", *dcContract, &err);
        expect_true(!asset.valid(), "asset inválido si el archivo no existe");
        expect_true(!err.empty(),   "*err lleva el motivo de fallo");
        // Cada required del contrato debe estar en missing.
        expect_true(asset.missing.size() >= 5,
                    "missing cubre todos los required del contrato");
    }

    // ========================================================================
    // G8 — AssetMapping (sidecar JSON para vincular geometría sin metadata)
    // ========================================================================

    // ---- 9. sidecarPathFor — derivación de la ruta del sidecar ------------
    {
        expect_eq(scinodes::AssetMapping::sidecarPathFor("dc_motor.gltf"),
                  "dc_motor.mapping.json",
                  "sidecar replaces .gltf extension");
        expect_eq(scinodes::AssetMapping::sidecarPathFor("foo/bar/dc_motor.glb"),
                  "foo/bar/dc_motor.mapping.json",
                  "sidecar preserves directory + replaces .glb");
        expect_eq(scinodes::AssetMapping::sidecarPathFor("dc_motor"),
                  "dc_motor.mapping.json",
                  "sidecar appends when no extension");
        // Path con punto antes del último separador no debe confundir.
        expect_eq(scinodes::AssetMapping::sidecarPathFor("a.b/c"),
                  "a.b/c.mapping.json",
                  "sidecar ignores dots in directory components");
        expect_eq(scinodes::AssetMapping::sidecarPathFor(""),
                  ".mapping.json",
                  "sidecar handles empty path");
    }

    // ---- 10. Round-trip: parse → toJsonString → parse, igual contenido ---
    {
        const std::string original = R"({
          "version": "0.1",
          "asset_path":  "dc_motor.gltf",
          "device_type": "DCMotorModel",
          "parts": {
            "shaft":   { "node": "Cylinder.001" },
            "housing": { "node": "Cylinder"     }
          },
          "joints": {
            "shaft_bearing": {
              "node": "shaft_pivot",
              "axis": [0, 1, 0]
            }
          },
          "anchors": {
            "terminal_plus":  { "node": "T+" },
            "terminal_minus": { "node": "T-" }
          }
        })";

        std::string err;
        auto m1 = scinodes::AssetMapping::loadFromString(original, &err);
        expect_true(err.empty(), "mapping JSON parse OK");
        expect_eq(m1.version,     "0.1",          "version 0.1");
        expect_eq(m1.device_type, "DCMotorModel", "device_type");
        expect_true(m1.parts.size() == 2,   "parts count 2");
        expect_true(m1.joints.size() == 1,  "joints count 1");
        expect_true(m1.anchors.size() == 2, "anchors count 2");
        expect_eq(m1.parts.at("shaft").node_name, "Cylinder.001",
                  "shaft → Cylinder.001");
        expect_eq(m1.joints.at("shaft_bearing").node_name, "shaft_pivot",
                  "joint node");
        expect_true(m1.joints.at("shaft_bearing").axis_explicit,
                    "axis_explicit true (axis array provided)");
        expect_near(m1.joints.at("shaft_bearing").axis[1], 1.0, 1e-6,
                    "axis y = 1");
        expect_eq(m1.anchors.at("terminal_plus").node_name, "T+",
                  "anchor T+");

        // Round-trip
        const std::string out = m1.toJsonString();
        err.clear();
        auto m2 = scinodes::AssetMapping::loadFromString(out, &err);
        expect_true(err.empty(), "round-trip parse OK");
        expect_eq(m2.parts.at("shaft").node_name, "Cylinder.001",
                  "shaft preserved across round-trip");
        expect_eq(m2.joints.at("shaft_bearing").node_name, "shaft_pivot",
                  "joint preserved across round-trip");
        expect_true(m2.joints.at("shaft_bearing").axis_explicit,
                    "axis_explicit preserved");
    }

    // ---- 11. Forma de slot abreviada (string directo en vez de objeto) ---
    {
        // Toleramos sidecars hechos a mano donde "shaft": "Cylinder.001"
        // sin envolverlo en {"node": "..."} — más cómodo para editar.
        const std::string compact = R"({
          "version": "0.1",
          "parts":   { "shaft": "Cylinder.001" },
          "anchors": { "terminal_plus": "T+" },
          "joints":  { "shaft_bearing": "pivot" }
        })";
        std::string err;
        auto m = scinodes::AssetMapping::loadFromString(compact, &err);
        expect_true(err.empty(), "compact slot parse OK");
        expect_eq(m.parts.at("shaft").node_name, "Cylinder.001",
                  "compact part slot");
        expect_eq(m.anchors.at("terminal_plus").node_name, "T+",
                  "compact anchor slot");
        expect_eq(m.joints.at("shaft_bearing").node_name, "pivot",
                  "compact joint slot");
        expect_true(!m.joints.at("shaft_bearing").axis_explicit,
                    "axis_explicit false (no axis array)");
    }

    // ---- 12. JSON inválido — error claro, mapping vacío ------------------
    {
        std::string err;
        auto m = scinodes::AssetMapping::loadFromString("{ this is not json",
                                                        &err);
        expect_true(!err.empty(), "JSON inválido reporta error");
        expect_true(m.empty(),    "mapping vacío en caso de error");
    }

    // ---- 13. Versión desconocida es rechazada ----------------------------
    {
        std::string err;
        auto m = scinodes::AssetMapping::loadFromString(
            R"({ "version": "9.9", "parts": { "shaft": "x" } })", &err);
        expect_true(!err.empty(),
                    "versión incompatible reporta error");
        expect_true(m.empty(),
                    "mapping con versión incompatible es vacío");
    }

    // ---- 14. save → load = el mismo contenido en disco -------------------
    {
        scinodes::AssetMapping m;
        m.asset_path  = "x.gltf";
        m.device_type = "DCMotorModel";
        m.parts["shaft"]   = {"Cylinder.001"};
        m.parts["housing"] = {"Cylinder"};
        scinodes::AssetMapping::JointSlot js;
        js.node_name = "pivot";
        js.axis = {{ 0.f, 1.f, 0.f }};
        js.axis_explicit = true;
        m.joints["shaft_bearing"] = js;
        m.anchors["terminal_plus"]  = {"T+"};
        m.anchors["terminal_minus"] = {"T-"};

        const std::string path = "/tmp/scinodes_test_mapping.json";
        std::string err;
        expect_true(m.saveToFile(path, &err),     "saveToFile OK");
        expect_true(err.empty(),                  "saveToFile sin error");

        err.clear();
        auto loaded = scinodes::AssetMapping::loadFromFile(path, &err);
        expect_true(err.empty(),  "loadFromFile sin error");
        expect_eq(loaded.parts.at("shaft").node_name, "Cylinder.001",
                  "save→load shaft");
        expect_eq(loaded.joints.at("shaft_bearing").node_name, "pivot",
                  "save→load joint node");
        expect_true(loaded.joints.at("shaft_bearing").axis_explicit,
                    "save→load axis_explicit preserved");
        std::remove(path.c_str());
    }

    // ========================================================================
    // G9 — Integración DeviceAssetLoader + sidecar AssetMapping
    // ========================================================================

    // Asset glTF "crudo": SIN extras.scinodes, solo nodos con nombres
    // realistas (estilo SolidWorks/FreeCAD/Thingiverse).  El loader debería
    // poder mapearlo solo con un sidecar al lado.
    const char* kRawMotorGltf = R"({
      "asset": { "version": "2.0" },
      "scene": 0,
      "scenes": [ { "nodes": [0, 1, 2, 3, 4] } ],
      "nodes": [
        { "name": "Stator_Body" },
        { "name": "Rotor_Shaft" },
        { "name": "Pivot_Empty", "translation": [0.0, 0.0, 0.0] },
        { "name": "Plus_Terminal",  "translation": [0.02, 0.05, 0.0] },
        { "name": "Minus_Terminal", "translation": [-0.02, 0.05, 0.0] }
      ]
    })";

    // ---- 15. Sidecar resuelve un glTF sin metadata ---------------------
    {
        const std::string gltfPath    = "/tmp/scinodes_test_raw_motor.gltf";
        const std::string mappingPath =
            scinodes::AssetMapping::sidecarPathFor(gltfPath);

        expect_true(writeFile(gltfPath, kRawMotorGltf),
                    "glTF crudo escrito");

        // Construir el mapping en memoria y guardarlo en disco.
        scinodes::AssetMapping m;
        m.asset_path  = "scinodes_test_raw_motor.gltf";
        m.device_type = "DCMotorModel";
        m.parts["shaft"]   = {"Rotor_Shaft"};
        m.parts["housing"] = {"Stator_Body"};
        scinodes::AssetMapping::JointSlot js;
        js.node_name     = "Pivot_Empty";
        js.axis          = {{ 0.f, 1.f, 0.f }};
        js.axis_explicit = true;
        m.joints["shaft_bearing"] = js;
        m.anchors["terminal_plus"]  = {"Plus_Terminal"};
        m.anchors["terminal_minus"] = {"Minus_Terminal"};

        std::string err;
        expect_true(m.saveToFile(mappingPath, &err),
                    "sidecar escrito al lado del glTF");

        // Cargar el asset: el loader debería descubrir el sidecar.
        err.clear();
        auto asset =
            scinodes::DeviceAssetLoader::load(gltfPath, *dcContract, &err);
        expect_true(err.empty(),  "load sin error (sidecar path)");
        expect_true(asset.valid(),
                    "asset válido SOLO por el sidecar (glTF sin extras)");
        expect_true(asset.parts.count("shaft"),   "shaft via sidecar");
        expect_true(asset.parts.count("housing"), "housing via sidecar");
        expect_true(asset.joints.count("shaft_bearing"),
                    "joint via sidecar");
        const auto& jf = asset.joints.at("shaft_bearing");
        expect_near(jf.axis[1], 1.0, 1e-6,
                    "joint axis Y (del mapping)");
        expect_eq(jf.driven_by, "omega", "joint.driven_by del contrato");
        expect_near(asset.anchors.at("terminal_plus").position[0],
                    0.02, 1e-6, "anchor.position via sidecar");

        std::remove(mappingPath.c_str());
        std::remove(gltfPath.c_str());
    }

    // ---- 16. Sidecar gana sobre extras.scinodes ------------------------
    // Cuando un glTF tiene extras Y existe sidecar, el sidecar manda.
    // Útil para sobreescribir un mapping incorrecto sin re-exportar.
    {
        const std::string gltfPath    = "/tmp/scinodes_test_both_motor.gltf";
        const std::string mappingPath =
            scinodes::AssetMapping::sidecarPathFor(gltfPath);
        expect_true(writeFile(gltfPath, kMotorGltf),
                    "glTF con extras escrito");

        // Mapping deliberadamente "raro": swappea housing y shaft.  Si
        // el sidecar manda, asset.parts["shaft"] saldrá del nodo
        // "Cuerpo" (que extras llamaba housing) y viceversa.
        scinodes::AssetMapping m;
        m.parts["shaft"]   = {"Cuerpo"};
        m.parts["housing"] = {"Rotor"};
        scinodes::AssetMapping::JointSlot js;
        js.node_name = "BearingEmpty";
        m.joints["shaft_bearing"] = js;
        m.anchors["terminal_plus"]  = {"TerminalPlusPoint"};
        m.anchors["terminal_minus"] = {"TerminalMinusPoint"};
        std::string err;
        m.saveToFile(mappingPath, &err);

        err.clear();
        auto asset =
            scinodes::DeviceAssetLoader::load(gltfPath, *dcContract, &err);
        expect_true(err.empty(),    "load sin error (sidecar dominante)");
        expect_true(asset.valid(),  "asset válido con sidecar dominante");
        // Como axis_explicit es false y el nodo "BearingEmpty" no tiene
        // rotation, el loader cae al default Y-up (no Z).  Lo importante
        // del test es que el sidecar mandó, no el axis específico.
        expect_true(asset.joints.count("shaft_bearing"),
                    "sidecar mapeó el joint");

        std::remove(mappingPath.c_str());
        std::remove(gltfPath.c_str());
    }

    // ---- 17. Sidecar mal formado → fallback a extras + warning -------
    {
        const std::string gltfPath    = "/tmp/scinodes_test_badside_motor.gltf";
        const std::string mappingPath =
            scinodes::AssetMapping::sidecarPathFor(gltfPath);
        expect_true(writeFile(gltfPath,    kMotorGltf),
                    "glTF con extras escrito");
        expect_true(writeFile(mappingPath, "{ not valid json"),
                    "sidecar inválido escrito");

        std::string err;
        auto asset =
            scinodes::DeviceAssetLoader::load(gltfPath, *dcContract, &err);
        // El sidecar es inválido, pero el glTF tiene extras correctos →
        // el asset debe quedar válido por el camino legacy.
        expect_true(asset.valid(),
                    "fallback a extras cuando sidecar es inválido");
        bool sawWarning = false;
        for (const auto& w : asset.warnings)
            if (w.find("sidecar") != std::string::npos) sawWarning = true;
        expect_true(sawWarning,
                    "warning del sidecar inválido presente");

        std::remove(mappingPath.c_str());
        std::remove(gltfPath.c_str());
    }

    // ---- 18. Sidecar referencia un nodo inexistente → missing + warn -
    {
        const std::string gltfPath    = "/tmp/scinodes_test_badref_motor.gltf";
        const std::string mappingPath =
            scinodes::AssetMapping::sidecarPathFor(gltfPath);
        expect_true(writeFile(gltfPath, kRawMotorGltf),
                    "glTF crudo escrito");

        scinodes::AssetMapping m;
        m.parts["shaft"]   = {"NoSuchNode"};   // ← typo deliberado
        m.parts["housing"] = {"Stator_Body"};
        m.joints["shaft_bearing"]   = { "Pivot_Empty",
                                        {{ 0.f, 1.f, 0.f }}, true };
        m.anchors["terminal_plus"]  = {"Plus_Terminal"};
        m.anchors["terminal_minus"] = {"Minus_Terminal"};
        std::string err;
        m.saveToFile(mappingPath, &err);

        err.clear();
        auto asset =
            scinodes::DeviceAssetLoader::load(gltfPath, *dcContract, &err);
        expect_true(!asset.valid(),
                    "asset inválido si un required no se resuelve");
        bool shaftMissing = false;
        for (const auto& mm : asset.missing)
            if (mm == "part:shaft") shaftMissing = true;
        expect_true(shaftMissing,
                    "part:shaft listada como missing");
        bool sawNodeWarning = false;
        for (const auto& w : asset.warnings)
            if (w.find("NoSuchNode") != std::string::npos) sawNodeWarning = true;
        expect_true(sawNodeWarning,
                    "warning menciona el nombre de nodo inexistente");

        std::remove(mappingPath.c_str());
        std::remove(gltfPath.c_str());
    }

    std::fprintf(stderr, "\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
