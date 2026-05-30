#include "Field.hpp"

#include "NodeType.hpp"

#include <cstdio>

namespace scinodes {

namespace {

// Construye el FieldDef de un puerto a partir del NodeDef + índice.  El
// lado (input/output) se decide via `isInput`.  Centraliza el fallback
// de label ("in N" / "out N") y la lógica de polymorphism.
FieldDef synthesizePortField(const NodeDef& def, int idx, bool isInput) {
    FieldDef f;
    char buf[32];

    // Label desde el array correspondiente; fallback al genérico.
    const auto& labels = isInput ? def.inputPortLabels : def.outputPortLabels;
    if (idx < static_cast<int>(labels.size()) && !labels[idx].empty()) {
        f.label = labels[idx];
    } else {
        std::snprintf(buf, sizeof(buf), "%s %d",
                      isInput ? "in" : "out", idx + 1);
        f.label = buf;
    }

    // Nombre estable para serialización + lookup.  Distinto del label
    // (que puede ser traducido o estilizado): el `name` es ASCII fijo.
    std::snprintf(buf, sizeof(buf), "%s%d", isInput ? "in" : "out", idx);
    f.name = buf;

    f.kind = isInput ? FieldKind::Input : FieldKind::Output;
    f.type = isInput ? inputPortTypeOf(def, idx)
                     : outputPortTypeOf(def, idx);

    // Unidad declarada por el registry — el bool `polymorphic` distingue
    // "no declarada" de "declarada como adimensional".
    const auto& units = isInput ? def.inputPortUnits : def.outputPortUnits;
    if (idx < static_cast<int>(units.size())) {
        f.defaultQuantity = Quantity{ 0.0, units[idx] };
        f.polymorphic = false;
    } else {
        f.defaultQuantity = Quantity{ 0.0, Unit{} };
        f.polymorphic = true;
    }
    return f;
}

// Idem para param.  Los params son SIEMPRE escalares (TypeExpr scalar)
// y nunca polimórficos según el registry actual — su unidad está
// declarada en `ParamDef::unit` (string).  Si el string está vacío o
// parsea mal, queda adimensional + polymorphic=true (para que la UI lo
// trate como "el usuario puede tipear cualquier unidad").
FieldDef synthesizeParamField(const ParamDef& pd) {
    FieldDef f;
    f.name = pd.name;
    f.label = pd.name;        // params no tienen label canónico aparte
                              // del nombre; la i18n se aplica en la UI.
    f.kind = FieldKind::Parameter;
    f.type = exprScalar();

    if (pd.unit.empty()) {
        f.defaultQuantity = Quantity{ pd.defaultValue, Unit{} };
        f.polymorphic = true;
    } else {
        auto pr = parseUnit(pd.unit);
        if (pr.ok()) {
            f.defaultQuantity = Quantity{ pd.defaultValue, pr.unit };
            f.polymorphic = false;
        } else {
            // Unit string del registry no parsea — registry corrupto.
            // Fallback a adimensional polimórfico; el registry debería
            // arreglarse, pero no nos colgamos aquí.
            f.defaultQuantity = Quantity{ pd.defaultValue, Unit{} };
            f.polymorphic = true;
        }
    }
    return f;
}

}  // anon

std::vector<FieldDef> synthesizeFields(const NodeDef& def) {
    std::vector<FieldDef> result;
    result.reserve(static_cast<size_t>(def.inputPorts)
                 + def.params.size()
                 + static_cast<size_t>(def.outputPorts));

    // Orden: inputs → params → outputs.  Espeja el orden de drawNode en
    // NodeCanvas y de columnas en computeNodeDimensions.
    for (int i = 0; i < def.inputPorts; ++i)
        result.push_back(synthesizePortField(def, i, /*isInput=*/true));

    for (const auto& pd : def.params)
        result.push_back(synthesizeParamField(pd));

    for (int o = 0; o < def.outputPorts; ++o)
        result.push_back(synthesizePortField(def, o, /*isInput=*/false));

    return result;
}

}  // namespace scinodes
