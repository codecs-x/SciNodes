#!/usr/bin/env python3
"""
pmsm_lumped_corrections.py

A self-contained Python sidecar that refines the lumped electromagnetic
parameters of a surface-mount PMSM beyond the closed-form baked into
SciNodes' PMSMElectromagnetic node. Three corrections are applied:

  1. Carter coefficient k_c — the effective airgap a flux line sees once
     the slot openings interrupt the stator's smooth bore. Always > 1,
     so it shrinks the magnetising inductance.

  2. Magnet leakage factor k_leak — the fraction of magnet flux that
     short-circuits through the rotor iron and never reaches the
     stator. Reduces the effective magnet flux and therefore Ke.

  3. End-winding inductance — the inductance of the half-turns that
     stick out the front and back faces. Adds to the airgap inductance
     to give the true per-phase synchronous inductance.

The script reads its inputs as JSON on stdin and writes a SciNodes
parameter CSV to stdout (or to --output). Re-import the result via the
"Import CSV…" button on the PMSMElectromagnetic node's parameter panel.

This is the planner's "Python sidecar" path for v0.8 — an honest
extension point. To swap in a real FEM (FEMM, scikit-fem, …) replace
the body of `apply_corrections()` with the call to your solver of
choice; the IPC contract on either side is just JSON-in / CSV-out.

Usage:
    pmsm_lumped_corrections.py < input.json
    pmsm_lumped_corrections.py --input input.json --output params.csv
"""

import argparse
import json
import math
import sys

MU0 = 4.0e-7 * math.pi


def carter_coefficient(slot_opening_m: float,
                       slot_pitch_m: float,
                       airgap_m: float) -> float:
    """Classical Carter coefficient (Pyrhonen 4.10)."""
    if slot_opening_m <= 0 or airgap_m <= 0 or slot_pitch_m <= 0:
        return 1.0
    # gamma = (4/pi) * ( (b0/2g) * atan(b0/2g) - ln(sqrt(1 + (b0/2g)^2)) )
    bg = slot_opening_m / (2.0 * airgap_m)
    gamma = (4.0 / math.pi) * (
        bg * math.atan(bg) - math.log(math.sqrt(1.0 + bg * bg))
    )
    return slot_pitch_m / max(1e-12, slot_pitch_m - gamma * airgap_m)


def apply_corrections(spec: dict) -> dict:
    """
    Take a machine spec (geometry + electrical) and return refined
    lumped parameters. All units SI.
    """
    D       = float(spec["bore_diameter_m"])
    L       = float(spec["stack_length_m"])
    g       = float(spec["airgap_m"])
    hm      = float(spec.get("magnet_thickness_m",         0.003))
    mu_r    = float(spec.get("magnet_mu_r",                1.05))
    Bg      = float(spec.get("airgap_flux_density_T",      0.85))
    Nph     = float(spec.get("turns_per_phase",            100))
    kw      = float(spec.get("winding_factor",             0.95))
    p       = float(spec.get("pole_pairs",                 4))
    Nslots  = float(spec.get("slot_count",                 24))
    b0      = float(spec.get("slot_opening_m",
                              math.pi * D / Nslots * 0.4))      # 40% of slot pitch
    leak    = float(spec.get("magnet_leakage_factor",      0.05))
    end_w   = float(spec.get("end_winding_factor",         0.30))  # 30% extra

    # Carter — slot pitch τ_s = π D / N_s
    tau_s = math.pi * D / Nslots
    k_c   = carter_coefficient(b0, tau_s, g)

    # Effective airgap including Carter and magnet relative permeability.
    g_eff = k_c * g + hm / mu_r

    # Magnet leakage reduces the effective flux density seen by the winding.
    Bg_eff = Bg * (1.0 - leak)

    # Refined per-phase back-EMF constant (peak / rad/s).
    Ke = (kw * Nph * p * Bg_eff * L * D) / 2.0

    # Airgap synchronous inductance, classic surface-PMSM formula.
    L_air = (MU0 * (Nph ** 2) * (kw ** 2) * math.pi * D * L) \
          / (8.0 * (p ** 2) * g_eff)

    # End-winding contribution (empirical scaling).
    L_ph = L_air * (1.0 + end_w)

    # Cogging amplitude — same simple scaling as PMSMElectromagnetic,
    # using the effective (post-leakage) airgap flux density.
    Tcog = (Bg_eff ** 2 * D * D * L) / (8.0 * MU0 * Nslots)

    return {
        "Turns per Phase":       Nph,
        "Winding Factor":        kw,
        "Pole Pairs":            p,
        "Airgap Flux Density":   Bg_eff,
        "Mechanical Airgap":     g,
        "Magnet Thickness":      hm,
        "Slot Count":            Nslots,
        # Diagnostic values returned in the trailing CSV comment.
        "_diag": {
            "k_carter":         k_c,
            "g_effective_m":    g_eff,
            "Bg_effective_T":   Bg_eff,
            "Ke_VsPerRad":      Ke,
            "L_phase_H":        L_ph,
            "Tcog_peak_Nm":     Tcog,
        },
    }


def emit_csv(refined: dict, fh) -> None:
    diag = refined.pop("_diag")
    fh.write("# SciNodes parameters from pmsm_lumped_corrections.py\n")
    for k, v in diag.items():
        fh.write(f"# {k}: {v:.6g}\n")
    fh.write("parameter,value,units\n")
    units = {
        "Turns per Phase":     "",
        "Winding Factor":      "",
        "Pole Pairs":          "",
        "Airgap Flux Density": "T",
        "Mechanical Airgap":   "m",
        "Magnet Thickness":    "m",
        "Slot Count":          "",
    }
    for name, value in refined.items():
        fh.write(f"{name},{value:.10g},{units.get(name, '')}\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("--input",  default="-", help="JSON spec ('-' for stdin)")
    ap.add_argument("--output", default="-", help="CSV file ('-' for stdout)")
    args = ap.parse_args()

    spec_src = sys.stdin if args.input == "-" else open(args.input)
    with spec_src as f:
        spec = json.load(f)

    refined = apply_corrections(spec)

    out = sys.stdout if args.output == "-" else open(args.output, "w")
    with out as f:
        emit_csv(refined, f)

    return 0


if __name__ == "__main__":
    sys.exit(main())
