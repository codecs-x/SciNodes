#!/usr/bin/env bash
# Compila los snippets TikZ de doc/diagrams/*.tex como standalone
# y emite SVG (para mdBook) + PDF (para reuso desde LaTeX externo).
# La fuente única son los .tex de doc/diagrams/. Esta carpeta se copia
# tal cual al repo de la tesis al cierre de cada tag.
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/doc/diagrams"
SVG_OUT="$ROOT/doc/manual/src/diagrams"
PDF_OUT="$ROOT/doc/diagrams/build"

mkdir -p "$SVG_OUT" "$PDF_OUT"

TMP="$(mktemp -d)"
trap "rm -rf '$TMP'" EXIT

for snippet in "$SRC"/*.tex; do
    name="$(basename "$snippet" .tex)"
    # _preamble.tex no se compila por sí solo, sólo se incluye.
    [[ "$name" == _* ]] && continue

    echo "==> $name"

    # Wrap en standalone class y compila a PDF.
    # \pagecolor{white} hace que el SVG (que llevará al mdBook con tema
    # oscuro) tenga fondo legible sin forzar al lector a un tema claro.
    cat > "$TMP/$name.tex" <<EOF
\\documentclass[border=4pt]{standalone}
\\usepackage{tikz}
\\usepackage{xcolor}
\\input{$SRC/_preamble}
\\pagecolor{white}
\\begin{document}
\\input{$snippet}
\\end{document}
EOF
    (cd "$TMP" && pdflatex -interaction=nonstopmode -halt-on-error "$name.tex" >/dev/null)

    cp "$TMP/$name.pdf" "$PDF_OUT/$name.pdf"

    # PDF → SVG via dvisvgm (acepta PDF con --pdf desde la 2.5).
    dvisvgm --pdf --no-fonts --output="$SVG_OUT/$name.svg" "$TMP/$name.pdf" >/dev/null 2>&1
    echo "    SVG: doc/manual/src/diagrams/$name.svg"
    echo "    PDF: doc/diagrams/build/$name.pdf"
done

echo
echo "Listo. $(ls "$SVG_OUT"/*.svg | wc -l) SVG + $(ls "$PDF_OUT"/*.pdf | wc -l) PDF generados."
