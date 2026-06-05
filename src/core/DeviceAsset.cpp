#include "DeviceAsset.hpp"
#include "AssetMapping.hpp"

// tinygltf es header-only.  TINYGLTF_IMPLEMENTATION debe definirse en
// EXACTAMENTE UNA unidad de traducción — ésta.  Las otras flags
// deshabilitan dependencias de stb_image que no necesitamos (no cargamos
// texturas).
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_USE_CPP14
#include <tiny_gltf.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace scinodes {

namespace {

// Lee un vector de 3 floats de un campo "key" en un objeto Value de
// tinygltf (que es la representación de los `extras` parseados).
bool readVec3(const tinygltf::Value& v, const std::string& key,
              std::array<float, 3>& out) {
    if (!v.IsObject()) return false;
    if (!v.Has(key))   return false;
    const auto val = v.Get(key);
    if (!val.IsArray() || val.ArrayLen() != 3) return false;
    for (size_t i = 0; i < 3; ++i) {
        const auto e = val.Get(int(i));
        if (e.IsNumber()) out[i] = static_cast<float>(e.GetNumberAsDouble());
    }
    return true;
}

std::string readString(const tinygltf::Value& v, const std::string& key) {
    if (!v.IsObject()) return "";
    if (!v.Has(key))   return "";
    const auto val = v.Get(key);
    return val.IsString() ? val.Get<std::string>() : std::string{};
}

// Extrae POSITION de una primitive en un vector flat de floats (x,y,z).
void appendPositions(const tinygltf::Model&     model,
                     const tinygltf::Primitive& prim,
                     std::vector<float>&        out) {
    auto it = prim.attributes.find("POSITION");
    if (it == prim.attributes.end()) return;
    const auto& acc = model.accessors[it->second];
    if (acc.bufferView < 0) return;
    const auto& bv  = model.bufferViews[acc.bufferView];
    const auto& buf = model.buffers[bv.buffer];

    const uint8_t* p = buf.data.data() + bv.byteOffset + acc.byteOffset;
    const size_t   stride = acc.ByteStride(bv) ? acc.ByteStride(bv)
                                               : sizeof(float) * 3;
    out.reserve(out.size() + acc.count * 3);
    for (size_t i = 0; i < acc.count; ++i) {
        const float* f = reinterpret_cast<const float*>(p + i * stride);
        out.push_back(f[0]);
        out.push_back(f[1]);
        out.push_back(f[2]);
    }
}

// Idem para NORMAL.
void appendNormals(const tinygltf::Model&     model,
                   const tinygltf::Primitive& prim,
                   std::vector<float>&        out) {
    auto it = prim.attributes.find("NORMAL");
    if (it == prim.attributes.end()) return;
    const auto& acc = model.accessors[it->second];
    if (acc.bufferView < 0) return;
    const auto& bv  = model.bufferViews[acc.bufferView];
    const auto& buf = model.buffers[bv.buffer];

    const uint8_t* p = buf.data.data() + bv.byteOffset + acc.byteOffset;
    const size_t   stride = acc.ByteStride(bv) ? acc.ByteStride(bv)
                                               : sizeof(float) * 3;
    out.reserve(out.size() + acc.count * 3);
    for (size_t i = 0; i < acc.count; ++i) {
        const float* f = reinterpret_cast<const float*>(p + i * stride);
        out.push_back(f[0]);
        out.push_back(f[1]);
        out.push_back(f[2]);
    }
}

// Extrae los índices de una primitive según su componentType.
void appendIndices(const tinygltf::Model&     model,
                   const tinygltf::Primitive& prim,
                   std::vector<uint32_t>&     out) {
    if (prim.indices < 0) return;
    const auto& acc = model.accessors[prim.indices];
    if (acc.bufferView < 0) return;
    const auto& bv  = model.bufferViews[acc.bufferView];
    const auto& buf = model.buffers[bv.buffer];
    const uint8_t* p = buf.data.data() + bv.byteOffset + acc.byteOffset;

    out.reserve(out.size() + acc.count);
    for (size_t i = 0; i < acc.count; ++i) {
        uint32_t idx = 0;
        switch (acc.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                idx = reinterpret_cast<const uint32_t*>(p)[i]; break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                idx = reinterpret_cast<const uint16_t*>(p)[i]; break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                idx = p[i]; break;
            default:
                continue;
        }
        out.push_back(idx);
    }
}

// Llena la lista `missing` con los required del contrato ausentes en el
// asset.  Llamado al final del load(), pero también al inicio cuando
// tinygltf falla — así el llamador obtiene una respuesta uniforme.
void fillMissing(const DeviceContract& c, DeviceAsset& a) {
    for (const auto& p : c.parts)
        if (p.required && a.parts.find(p.name) == a.parts.end())
            a.missing.push_back("part:" + p.name);
    for (const auto& j : c.joints)
        if (j.required && a.joints.find(j.name) == a.joints.end())
            a.missing.push_back("joint:" + j.name);
    for (const auto& an : c.anchors)
        if (an.required && a.anchors.find(an.name) == a.anchors.end())
            a.missing.push_back("anchor:" + an.name);
    // Warnings: optional ausentes.
    for (const auto& an : c.anchors)
        if (!an.required && a.anchors.find(an.name) == a.anchors.end())
            a.warnings.push_back("anchor:" + an.name + " (optional, ausente)");
}

bool endsWithCi(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    const size_t off = s.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        const char a = static_cast<char>(std::tolower(s[off + i]));
        const char b = static_cast<char>(std::tolower(suffix[i]));
        if (a != b) return false;
    }
    return true;
}

// Localiza un nodo glTF por nombre.  Devuelve el índice del primer
// nodo cuyo `name` coincida exactamente con `name`, o -1 si no existe.
int findNodeByName(const tinygltf::Model& model, const std::string& name) {
    if (name.empty()) return -1;
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        if (model.nodes[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

// Lee la malla del node.mesh (concatenando primitivas) y la rellena en
// `out`.  Si el nodo no tiene mesh asociada, out queda vacía.  Compartido
// entre el camino extras y el camino mapping.
void extractMesh(const tinygltf::Model& model,
                 const tinygltf::Node&  node,
                 AssetMesh&             out) {
    if (node.mesh < 0 || node.mesh >= (int)model.meshes.size()) return;
    const auto& mesh = model.meshes[node.mesh];

    // Transform del nodo glTF (T·R·S) horneado en los vértices del mesh
    // local.  Sin esto, partes con offset/orientación propios aparecen
    // colapsadas o mal orientadas.  El exporter +Y-up de Blender deja la
    // conversión Z-up→Y-up como rotación de nodo en algunas piezas (p. ej.
    // cilindros), así que IGNORARLA las desorienta — por eso aplicamos
    // también rotation y scale, no sólo translation.
    float tx = 0.f, ty = 0.f, tz = 0.f;
    if (node.translation.size() == 3) {
        tx = static_cast<float>(node.translation[0]);
        ty = static_cast<float>(node.translation[1]);
        tz = static_cast<float>(node.translation[2]);
    }
    float sx = 1.f, sy = 1.f, sz = 1.f;
    if (node.scale.size() == 3) {
        sx = static_cast<float>(node.scale[0]);
        sy = static_cast<float>(node.scale[1]);
        sz = static_cast<float>(node.scale[2]);
    }
    float qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;
    if (node.rotation.size() == 4) {
        qx = static_cast<float>(node.rotation[0]);
        qy = static_cast<float>(node.rotation[1]);
        qz = static_cast<float>(node.rotation[2]);
        qw = static_cast<float>(node.rotation[3]);
    }
    // Rota (x,y,z) por el cuaternión (qx,qy,qz,qw): v + 2qw(u×v) + 2u×(u×v).
    auto qrot = [qx, qy, qz, qw](float x, float y, float z, float o[3]) {
        const float ux = 2.f*(qy*z - qz*y);
        const float uy = 2.f*(qz*x - qx*z);
        const float uz = 2.f*(qx*y - qy*x);
        o[0] = x + qw*ux + (qy*uz - qz*uy);
        o[1] = y + qw*uy + (qz*ux - qx*uz);
        o[2] = z + qw*uz + (qx*uy - qy*ux);
    };

    for (const auto& prim : mesh.primitives) {
        const uint32_t indexBase =
            static_cast<uint32_t>(out.positions.size() / 3);
        const size_t posBefore = out.positions.size();
        appendPositions(model, prim, out.positions);
        // Hornear T·R·S del nodo en los vértices recién añadidos.
        for (size_t k = posBefore; k + 2 < out.positions.size(); k += 3) {
            float r[3];
            qrot(out.positions[k+0]*sx, out.positions[k+1]*sy,
                 out.positions[k+2]*sz, r);
            out.positions[k+0] = r[0] + tx;
            out.positions[k+1] = r[1] + ty;
            out.positions[k+2] = r[2] + tz;
        }
        const size_t nrmBefore = out.normals.size();
        appendNormals  (model, prim, out.normals);
        // Las normales sólo rotan (sin traslación ni escala).
        for (size_t k = nrmBefore; k + 2 < out.normals.size(); k += 3) {
            float r[3];
            qrot(out.normals[k+0], out.normals[k+1], out.normals[k+2], r);
            out.normals[k+0] = r[0];
            out.normals[k+1] = r[1];
            out.normals[k+2] = r[2];
        }
        const size_t indicesBefore = out.indices.size();
        appendIndices  (model, prim, out.indices);
        if (indexBase > 0) {
            for (size_t k = indicesBefore; k < out.indices.size(); ++k)
                out.indices[k] += indexBase;
        }
    }
}

// Deriva un eje en coordenadas globales a partir de la rotación del nodo
// glTF (cuaternión qx, qy, qz, qw).  Aplica q al vector local +Z (0,0,1):
//     x = 2(qx·qz + qw·qy)
//     y = 2(qy·qz - qw·qx)
//     z = 1 - 2(qx² + qy²)
// Si el nodo no trae rotation o ésta es identidad, devuelve +Y (default
// glTF Y-up; ver feature de geometry-contracts para por qué Y y no Z).
std::array<float, 3> axisFromNodeRotation(const tinygltf::Node& node) {
    if (node.rotation.size() != 4) {
        return {{ 0.f, 1.f, 0.f }};
    }
    const float qx = static_cast<float>(node.rotation[0]);
    const float qy = static_cast<float>(node.rotation[1]);
    const float qz = static_cast<float>(node.rotation[2]);
    const float qw = static_cast<float>(node.rotation[3]);
    return {{
        2.f * (qx * qz + qw * qy),
        2.f * (qy * qz - qw * qx),
        1.f - 2.f * (qx * qx + qy * qy)
    }};
}

// Aplica un AssetMapping ya cargado contra el modelo glTF, llenando
// asset.parts / asset.joints / asset.anchors.  No toca `missing` /
// `warnings` (eso lo hace fillMissing al final).  Si un slot del mapping
// referencia un nodo que no existe, se agrega un warning y el slot queda
// sin resolver (caerá en missing si era required).
void applyMapping(const tinygltf::Model& model,
                  const DeviceContract&  contract,
                  const AssetMapping&    mapping,
                  DeviceAsset&           asset) {
    // ---- parts ----
    for (const auto& [slotName, slot] : mapping.parts) {
        if (slot.node_name.empty()) continue;
        const int idx = findNodeByName(model, slot.node_name);
        if (idx < 0) {
            asset.warnings.push_back(
                "mapping.parts." + slotName + ": nodo '" +
                slot.node_name + "' no existe en el glTF");
            continue;
        }
        AssetMesh m;
        extractMesh(model, model.nodes[idx], m);
        asset.parts[slotName] = std::move(m);
    }

    // ---- joints ----
    for (const auto& [slotName, slot] : mapping.joints) {
        if (slot.node_name.empty()) continue;
        const int idx = findNodeByName(model, slot.node_name);
        if (idx < 0) {
            asset.warnings.push_back(
                "mapping.joints." + slotName + ": nodo '" +
                slot.node_name + "' no existe en el glTF");
            continue;
        }
        const auto& node = model.nodes[idx];

        AssetJointFrame jf;
        if (node.translation.size() == 3) {
            jf.origin = {{ float(node.translation[0]),
                           float(node.translation[1]),
                           float(node.translation[2]) }};
        }
        jf.axis = slot.axis_explicit
            ? slot.axis
            : axisFromNodeRotation(node);

        for (const auto& cj : contract.joints) {
            if (cj.name == slotName) {
                jf.type      = cj.type;
                jf.parent    = cj.parent;
                jf.child     = cj.child;
                jf.driven_by = cj.driven_by;
                break;
            }
        }
        asset.joints[slotName] = std::move(jf);
    }

    // ---- anchors ----
    for (const auto& [slotName, slot] : mapping.anchors) {
        if (slot.node_name.empty()) continue;
        const int idx = findNodeByName(model, slot.node_name);
        if (idx < 0) {
            asset.warnings.push_back(
                "mapping.anchors." + slotName + ": nodo '" +
                slot.node_name + "' no existe en el glTF");
            continue;
        }
        const auto& node = model.nodes[idx];

        AssetAnchor a;
        if (node.translation.size() == 3) {
            a.position = {{ float(node.translation[0]),
                            float(node.translation[1]),
                            float(node.translation[2]) }};
        }
        for (const auto& ca : contract.anchors) {
            if (ca.name == slotName) { a.kind = ca.kind; break; }
        }
        asset.anchors[slotName] = std::move(a);
    }
}

// ¿Existe el archivo en disco?  Predicado mínimo para evitar tirarse a
// loadFromFile si no hay sidecar (suprime un mensaje de "no se pudo
// abrir" que sería ruido en el flujo normal sin sidecar).
bool fileExists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

}  // namespace

DeviceAsset DeviceAssetLoader::load(const std::string&    path,
                                    const DeviceContract& contract,
                                    std::string*          err) {
    DeviceAsset asset;
    asset.path       = path;
    asset.deviceType = contract.device_type;

    tinygltf::TinyGLTF loader;
    tinygltf::Model    model;
    std::string        gltfWarn, gltfErr;

    const bool isBinary = endsWithCi(path, ".glb");
    const bool ok = isBinary
        ? loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, path)
        : loader.LoadASCIIFromFile (&model, &gltfErr, &gltfWarn, path);

    if (!ok) {
        if (err) *err = gltfErr.empty()
            ? ("tinygltf failed to open " + path)
            : ("tinygltf: " + gltfErr);
        fillMissing(contract, asset);   // toda la lista required → missing
        return asset;
    }
    if (!gltfWarn.empty()) asset.warnings.push_back("tinygltf: " + gltfWarn);

    // Camino A — sidecar AssetMapping (forma SciNodes-native, sin
    // metadata embebida en el glTF).  El sidecar siempre tiene
    // precedencia sobre los `extras` del glTF; cuando existe, los
    // extras se ignoran completamente.  Si existe pero el JSON es
    // inválido, caemos al camino B con un warning para que el usuario
    // sepa por qué.
    const std::string sidecarPath = AssetMapping::sidecarPathFor(path);
    if (fileExists(sidecarPath)) {
        std::string mErr;
        AssetMapping mapping = AssetMapping::loadFromFile(sidecarPath, &mErr);
        if (!mErr.empty()) {
            asset.warnings.push_back("sidecar: " + mErr +
                " (se ignora y se intenta con extras.scinodes)");
        } else {
            applyMapping(model, contract, mapping, asset);
            fillMissing(contract, asset);
            return asset;
        }
    }

    // Camino B — leer extras.scinodes embebidos en cada nodo del glTF.
    //
    // Aceptamos dos formas en extras:
    //   (a) Anidada — extras.scinodes = { role, name, axis }
    //       Forma "canónica", la que producen los tests, pygltflib o
    //       JSON escrito a mano.
    //   (b) Plana    — extras["scinodes.role"], ["scinodes.name"], ...
    //       Forma que produce el exportador glTF de Blender cuando
    //       las Custom Properties se llaman literalmente
    //       "scinodes.role", "scinodes.name", "scinodes.axis".
    //
    // Esto deja al usuario libre de elegir la herramienta sin tener
    // que post-procesar el archivo.
    for (const auto& node : model.nodes) {
        const auto& extras = node.extras;
        if (!extras.IsObject()) continue;

        std::string          role;
        std::string          name;
        std::array<float, 3> axisFromExtras = {{ 0.f, 0.f, 1.f }};
        bool                 axisExplicit   = false;

        if (extras.Has("scinodes") && extras.Get("scinodes").IsObject()) {
            // Forma anidada (tests, pygltflib, JSON a mano).
            const auto scn = extras.Get("scinodes");
            role = readString(scn, "role");
            name = readString(scn, "name");
            axisExplicit = readVec3(scn, "axis", axisFromExtras);
        } else if (extras.Has("scinodes.role")) {
            // Forma plana (export estándar de Blender).
            role = readString(extras, "scinodes.role");
            name = readString(extras, "scinodes.name");
            axisExplicit = readVec3(extras, "scinodes.axis", axisFromExtras);
        } else {
            continue;       // este nodo no tiene metadata SciNodes
        }

        if (role.empty() || name.empty()) {
            asset.warnings.push_back(
                "nodo glTF '" + node.name + "' con metadata SciNodes incompleta");
            continue;
        }

        if (role == "part") {
            if (asset.parts.count(name)) {
                asset.warnings.push_back("part:" + name + " duplicada");
                continue;
            }
            AssetMesh m;
            if (node.mesh >= 0 && node.mesh < (int)model.meshes.size()) {
                const auto& mesh = model.meshes[node.mesh];
                for (const auto& prim : mesh.primitives) {
                    // Cada primitive tiene su propio vertex array.  Al
                    // concatenar hay que desplazar los índices que
                    // siguen para que apunten a las posiciones recién
                    // añadidas.
                    const uint32_t indexBase =
                        static_cast<uint32_t>(m.positions.size() / 3);
                    appendPositions(model, prim, m.positions);
                    appendNormals  (model, prim, m.normals);
                    const size_t indicesBefore = m.indices.size();
                    appendIndices  (model, prim, m.indices);
                    if (indexBase > 0) {
                        for (size_t k = indicesBefore; k < m.indices.size(); ++k)
                            m.indices[k] += indexBase;
                    }
                }
            }
            asset.parts[name] = std::move(m);
        }
        else if (role == "joint") {
            if (asset.joints.count(name)) {
                asset.warnings.push_back("joint:" + name + " duplicada");
                continue;
            }
            AssetJointFrame jf;
            if (node.translation.size() == 3) {
                jf.origin = {{ float(node.translation[0]),
                               float(node.translation[1]),
                               float(node.translation[2]) }};
            }
            // Eje del joint: si scinodes.axis está explícito en extras
            // (caso JSON a mano / pygltflib) lo respetamos.  Si no, lo
            // derivamos de la rotación del propio nodo glTF: aplicamos
            // el cuaternión (qx, qy, qz, qw) al vector local +Z (0,0,1).
            //
            // Esto hace que el flujo Blender "just work": el usuario rota
            // el SINGLE_ARROW empty en Blender para apuntar al eje físico,
            // y al exportar con "+Y Up" la rotación se baka en
            // node.rotation, llevándose con ella la convención Z-up→Y-up
            // sin necesidad de que el usuario escriba la metadata en
            // coordenadas glTF post-export.
            if (axisExplicit) {
                jf.axis = axisFromExtras;
            } else if (node.rotation.size() == 4) {
                const float qx = float(node.rotation[0]);
                const float qy = float(node.rotation[1]);
                const float qz = float(node.rotation[2]);
                const float qw = float(node.rotation[3]);
                jf.axis = {{
                    2.f * (qx * qz + qw * qy),
                    2.f * (qy * qz - qw * qx),
                    1.f - 2.f * (qx * qx + qy * qy)
                }};
            } else {
                // Default cuando no hay metadata ni rotation: +Y.
                // glTF es Y-up por especificación, así que un cilindro
                // exportado desde cualquier herramienta queda
                // típicamente alineado a +Y.  El antiguo default +Z
                // producía rotación perpendicular ("hélice") en
                // exports de Blender que vienen con rotation=null y
                // sin scinodes.axis (el empty queda en identity tras
                // el bake del +Y-Up en la geometría).
                jf.axis = {{ 0.f, 1.f, 0.f }};
            }

            // type/parent/child/driven_by del contrato por nombre.
            for (const auto& cj : contract.joints) {
                if (cj.name == name) {
                    jf.type      = cj.type;
                    jf.parent    = cj.parent;
                    jf.child     = cj.child;
                    jf.driven_by = cj.driven_by;
                    break;
                }
            }
            asset.joints[name] = std::move(jf);
        }
        else if (role == "anchor") {
            if (asset.anchors.count(name)) {
                asset.warnings.push_back("anchor:" + name + " duplicada");
                continue;
            }
            AssetAnchor a;
            if (node.translation.size() == 3) {
                a.position = {{ float(node.translation[0]),
                                float(node.translation[1]),
                                float(node.translation[2]) }};
            }
            for (const auto& ca : contract.anchors) {
                if (ca.name == name) { a.kind = ca.kind; break; }
            }
            asset.anchors[name] = std::move(a);
        }
        else {
            asset.warnings.push_back(
                "extras.scinodes.role='" + role + "' no reconocido");
        }
    }

    // Camino C — fallback name-matching contra el contrato.
    //
    // Si ningún slot del contrato fue resuelto vía sidecar (A) ni vía
    // extras.scinodes (B), intentamos un match directo entre los nombres
    // declarados en el contrato y los `name` de los nodos del glTF.  Esto
    // hace que un .gltf "limpio" — sin metadata SciNodes embebida y sin
    // sidecar — funcione automáticamente si el modelador usó los mismos
    // identificadores del contrato como nombres de nodos.
    //
    // Caso de uso: assets exportados desde Blender con cada pieza
    // nombrada `shaft`, `housing`, `shaft_bearing`, `terminal_plus`,
    // etc.  Sin este fallback el usuario tenía que llenar manualmente
    // el panel "Editar mapping" para cada slot — fricción innecesaria.
    auto tryBindByName = [&](const std::string& slotName,
                             auto&& applyFn) {
        if (auto* existing = (decltype(applyFn(0,0))*)nullptr; false) {} // dummy
        const int idx = findNodeByName(model, slotName);
        if (idx >= 0) applyFn(idx, 0);
    };
    for (const auto& cp : contract.parts) {
        if (asset.parts.count(cp.name)) continue;
        const int idx = findNodeByName(model, cp.name);
        if (idx < 0) continue;
        AssetMesh m;
        extractMesh(model, model.nodes[idx], m);
        if (!m.positions.empty()) asset.parts[cp.name] = std::move(m);
    }
    for (const auto& cj : contract.joints) {
        if (asset.joints.count(cj.name)) continue;
        const int idx = findNodeByName(model, cj.name);
        if (idx < 0) continue;
        const auto& node = model.nodes[idx];
        AssetJointFrame jf;
        if (node.translation.size() == 3) {
            jf.origin = {{ float(node.translation[0]),
                           float(node.translation[1]),
                           float(node.translation[2]) }};
        }
        jf.axis      = axisFromNodeRotation(node);
        jf.type      = cj.type;
        jf.parent    = cj.parent;
        jf.child     = cj.child;
        jf.driven_by = cj.driven_by;
        asset.joints[cj.name] = std::move(jf);
    }
    for (const auto& ca : contract.anchors) {
        if (asset.anchors.count(ca.name)) continue;
        const int idx = findNodeByName(model, ca.name);
        if (idx < 0) continue;
        const auto& node = model.nodes[idx];
        AssetAnchor a;
        if (node.translation.size() == 3) {
            a.position = {{ float(node.translation[0]),
                            float(node.translation[1]),
                            float(node.translation[2]) }};
        }
        a.kind = ca.kind;
        asset.anchors[ca.name] = std::move(a);
    }

    // Validar required del contrato contra lo que llegó.
    fillMissing(contract, asset);
    return asset;
}

// ---------------------------------------------------------------------------
// loadCatalog — contract-less.  Para entradas del catálogo de objetos
// importados (Menú Archivo → Importar modelo 3D).  Recorre todos los
// nodos con mesh y los emite como `parts` del DeviceAsset; ignora
// extras.scinodes y sidecar AssetMapping.  Sin validación, sin joints,
// sin anchors.  Ver `doc/designs/3d_scene_graph_design.md` §8.
// ---------------------------------------------------------------------------
DeviceAsset DeviceAssetLoader::loadCatalog(const std::string& path,
                                           std::string*       err) {
    DeviceAsset asset;
    asset.path       = path;
    asset.deviceType = "Catalog";

    tinygltf::TinyGLTF loader;
    tinygltf::Model    model;
    std::string        gltfWarn, gltfErr;

    const bool isBinary = endsWithCi(path, ".glb");
    const bool ok = isBinary
        ? loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, path)
        : loader.LoadASCIIFromFile (&model, &gltfErr, &gltfWarn, path);

    if (!ok) {
        if (err) *err = gltfErr.empty()
            ? ("tinygltf failed to open " + path)
            : ("tinygltf: " + gltfErr);
        return asset;
    }
    if (!gltfWarn.empty()) asset.warnings.push_back("tinygltf: " + gltfWarn);

    // Recorre cada nodo del glTF con mesh.  El nombre del nodo identifica
    // la part en el catálogo (Object3D.objectRef = "<file>/<partName>").
    // Anónimos: fallback "part_N" para que el catálogo nunca quede con
    // claves vacías.
    int anonCounter = 0;
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& node = model.nodes[i];
        if (node.mesh < 0) continue;
        AssetMesh m;
        extractMesh(model, node, m);
        if (m.empty()) continue;
        std::string name = node.name;
        if (name.empty()) name = "part_" + std::to_string(anonCounter++);
        // Si ya existe una part con ese nombre, agrega sufijo para no
        // perder geometría (glTF permite nodos hermanos con el mismo
        // nombre; raro pero posible).
        std::string finalName = name;
        int dedupe = 1;
        while (asset.parts.count(finalName))
            finalName = name + "#" + std::to_string(dedupe++);
        asset.parts[std::move(finalName)] = std::move(m);
    }

    return asset;
}

}  // namespace scinodes
