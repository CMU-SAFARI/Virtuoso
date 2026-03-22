#pragma once

/* small helper to detect incomplete traits (include‑order contract) */
template <typename T> struct is_complete { static constexpr bool value = sizeof(T) > 0; };

