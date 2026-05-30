# FEM Sidecar

This directory hosts the **optional FEM backend** from the v0.8 planner
roadmap: an external script that refines the lumped-parameter
electromagnetic model of a PMSM beyond the closed form baked into
SciNodes' `PMSMElectromagnetic` node, and emits a CSV the user re-imports
through the node's parameter panel.

The shipped script (`pmsm_lumped_corrections.py`) is **not** a finite-
element solver — it applies three higher-order analytical corrections
(Carter coefficient for slot openings, magnet leakage flux, end-winding
inductance) that are still tractable by hand but materially shift Ke and
L_phase away from the textbook closed form.

The point of shipping this rather than a real FEM is to **demonstrate the
IPC contract**: SciNodes → JSON in → external solver → CSV out → SciNodes.
Once that round-trip works with the analytical-correction script, the
body of `apply_corrections()` can be replaced with a call to FEMM
(via `pyfemm`), scikit-fem, OpenModelica, or any other backend without
touching SciNodes itself.

## Usage

```bash
# One-shot from a JSON spec to a CSV ready for Import CSV…
python3 pmsm_lumped_corrections.py < sample_input.json > params.csv

# Or with explicit flags:
python3 pmsm_lumped_corrections.py \
    --input  sample_input.json \
    --output params.csv
```

Then in SciNodes:

1. Double-click the `PMSMElectromagnetic` node.
2. Click **Import CSV…** in the parameter panel.
3. Pick `params.csv` — the seven parameters update at once, an undo
   snapshot is recorded, and the live Scilab solver picks up the new
   values at the next step boundary.

The Ke / L_phase / Tcog actually consumed by the simulation are still
computed inside Scilab from the imported parameters via the formulas in
[grammar_reference.md](../grammar_reference.md). The sidecar's `# diag`
comment header in the CSV reports the Ke / L_phase / Tcog values it
would compute itself, so you can sanity-check the round-trip against the
on-screen values from the Oscilloscope sinks wired to the node.

## Swapping in a real FEM solver

Replace the body of `apply_corrections(spec)` in
`pmsm_lumped_corrections.py`. The expected return value is a dict mapping
SciNodes parameter names to numeric values. The keys must match the
PMSMElectromagnetic param names exactly — `Turns per Phase`,
`Winding Factor`, `Pole Pairs`, `Airgap Flux Density`, `Mechanical Airgap`,
`Magnet Thickness`, `Slot Count`.

Unknown rows in the CSV are reported as warnings by the importer and
ignored, so extra diagnostics are safe to add — they just won't change
the node's behaviour.
