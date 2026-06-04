#!/usr/bin/env bash
# audit_all.sh — corre las 13 capas del audit en secuencia y reporta
# un resumen unificado al final.
#
# Capas:
#   1-5 — triple_audit.py (catálogo nodos, tests, menús, atajos, paneles)
#   6   — changelog_coverage.py (features del CHANGELOG ↔ doc)
#   7   — node_doc_coverage.py (catálogo descriptivo)
#   8   — api_doc_coverage.py (APIs públicas runtime)
#   9   — scn_format_coverage.py (formato .scn)
#   10  — i18n_coverage.py (keys i18n ↔ código)
#   11  — grammar_coverage.py (reglas R* de la gramática ↔ código)
#   12  — dependencies_coverage.py (dependencias ↔ CMakeLists.txt)
#   13  — simulation_controls_coverage.py (controles ↔ enum SimAction)
#
# Exit code 0 si todas pasan, 1 si alguna falla.
set -u
cd "$(dirname "$0")/.."

PASS=0
FAIL=0
FAILED_AUDITS=()

run_audit() {
    local name="$1" script="$2"
    echo
    echo "════════════════════════════════════════════════════════════════"
    echo "  $name"
    echo "════════════════════════════════════════════════════════════════"
    if python3 "tools/$script"; then
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
        FAILED_AUDITS+=("$name")
    fi
}

run_audit "Capas 1-5: catálogo + tests + menús + atajos + paneles" triple_audit.py
run_audit "Capa 6: features del CHANGELOG ↔ doc"                    changelog_coverage.py
run_audit "Capa 7: catálogo descriptivo ↔ mdBook+tesis"             node_doc_coverage.py
run_audit "Capa 8: APIs públicas runtime ↔ dev guide"                api_doc_coverage.py
run_audit "Capa 9: formato .scn ↔ doc"                               scn_format_coverage.py
run_audit "Capa 10: i18n keys ↔ código"                              i18n_coverage.py
run_audit "Capa 11: reglas de gramática ↔ código"                    grammar_coverage.py
run_audit "Capa 12: dependencias ↔ CMakeLists.txt"                   dependencies_coverage.py
run_audit "Capa 13: controles de simulación ↔ SimAction"             simulation_controls_coverage.py

echo
echo "════════════════════════════════════════════════════════════════"
echo "  Resumen"
echo "════════════════════════════════════════════════════════════════"
echo "  Audits ejecutados : $((PASS + FAIL))"
echo "  Pasaron           : $PASS"
echo "  Fallaron          : $FAIL"
if [ "$FAIL" -gt 0 ]; then
    echo
    echo "  Fallaron:"
    for f in "${FAILED_AUDITS[@]}"; do
        echo "    - $f"
    done
    echo
    echo "  Re-correr individualmente para ver los diffs específicos."
    exit 1
fi
echo
echo "  ✓ Todas las capas de la documentación están sincronizadas con el código."
exit 0
