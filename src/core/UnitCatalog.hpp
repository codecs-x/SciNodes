#pragma once

#include "Unit.hpp"

// ---------------------------------------------------------------------------
// scinodes::units — catálogo canónico de unidades para el registry de
// nodos.  Cada constante es un `Unit` con sus exponentes SI exactos y
// magnitud 1.0 respecto a la base canónica.  Etapa 6D del análisis
// dimensional usará estas constantes para declarar
// `NodeDef::inputPortUnits` / `outputPortUnits` sin parsear strings en
// startup — coherente con "no magic numbers".
//
// Los valores corresponden a la tabla del parser textual (etapa 6B);
// `parseUnit("V").unit == units::kVolt`, etc. (verificado por tests).
//
// Convención SOLID: cuando un nodo se declara con una unidad fija, se
// referencia a esta constante.  Nunca se construye `Unit{{exps...}, ...}`
// inline en el registry — la centralización vive aquí.
// ---------------------------------------------------------------------------
namespace scinodes::units {

// Adimensional (todos los exponentes 0).
inline constexpr Unit kDimensionless = Unit{};

// Bases SI canónicas (magnitude = 1.0, exponente = 1 en la dimensión
// correspondiente, 0 en las demás).  La 8ª dim es la "phantom angle
// exponent" (etapa 6I.L) — distingue ángulos de adimensionales puros.
inline constexpr Unit kMeter        = Unit{ {1,0,0,0,0,0,0,0}, 1.0 };
inline constexpr Unit kKilogram     = Unit{ {0,1,0,0,0,0,0,0}, 1.0 };
inline constexpr Unit kSecond       = Unit{ {0,0,1,0,0,0,0,0}, 1.0 };
inline constexpr Unit kAmpere       = Unit{ {0,0,0,1,0,0,0,0}, 1.0 };
inline constexpr Unit kKelvin       = Unit{ {0,0,0,0,1,0,0,0}, 1.0 };
inline constexpr Unit kMole         = Unit{ {0,0,0,0,0,1,0,0}, 1.0 };
inline constexpr Unit kCandela      = Unit{ {0,0,0,0,0,0,1,0}, 1.0 };

// Derivadas SI con nombre propio.  Los exponentes derivan del álgebra
// (W = m²·kg·s⁻³, V = m²·kg·s⁻³·A⁻¹, etc.) — verificados por tests.
inline constexpr Unit kHertz        = Unit{ {0,0,-1,0,0,0,0,0}, 1.0 };
inline constexpr Unit kNewton       = Unit{ {1,1,-2,0,0,0,0,0}, 1.0 };
inline constexpr Unit kPascal       = Unit{ {-1,1,-2,0,0,0,0,0}, 1.0 };
inline constexpr Unit kJoule        = Unit{ {2,1,-2,0,0,0,0,0}, 1.0 };
inline constexpr Unit kWatt         = Unit{ {2,1,-3,0,0,0,0,0}, 1.0 };
inline constexpr Unit kCoulomb      = Unit{ {0,0,1,1,0,0,0,0}, 1.0 };
inline constexpr Unit kVolt         = Unit{ {2,1,-3,-1,0,0,0,0}, 1.0 };
inline constexpr Unit kFarad        = Unit{ {-2,-1,4,2,0,0,0,0}, 1.0 };
inline constexpr Unit kOhm          = Unit{ {2,1,-3,-2,0,0,0,0}, 1.0 };
inline constexpr Unit kTesla        = Unit{ {0,1,-2,-1,0,0,0,0}, 1.0 };
inline constexpr Unit kHenry        = Unit{ {2,1,-2,-2,0,0,0,0}, 1.0 };

// Ángulo (etapa 6I.L): rad tiene la 8ª dim = 1, distinguible de un
// escalar puro.  deg comparte dim con rad pero difiere en magnitud
// (π/180).  rev también es ángulo con magnitud 2π.
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr Unit kRadian       = Unit{ {0,0,0,0,0,0,0,1}, 1.0 };
inline constexpr Unit kDegree       = Unit{ {0,0,0,0,0,0,0,1}, kPi / 180.0 };

// Compuestos frecuentes en física e ingeniería de control.  rad/s
// AHORA es distinguible de Hz (sin 8ª dim) gracias al phantom angle
// exponent: rad/s = (s⁻¹, rad), Hz = (s⁻¹, sin rad).
inline constexpr Unit kRadianPerSec   = Unit{ {0,0,-1,0,0,0,0,1}, 1.0 };
inline constexpr Unit kNewtonMeter    = Unit{ {2,1,-2,0,0,0,0,0}, 1.0 };          // ≡ Joule dim

}  // namespace scinodes::units
