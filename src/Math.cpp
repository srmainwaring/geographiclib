/**
 * \file Math.cpp
 * \brief Implementation for GeographicLib::Math class
 *
 * Copyright (c) Charles Karney (2015-2022) <karney@alum.mit.edu> and licensed
 * under the MIT/X11 License.  For more information, see
 * https://geographiclib.sourceforge.io/
 **********************************************************************/

#include <GeographicLib/Math.hpp>

#if defined(_MSC_VER)
// Squelch warnings about constant conditional and enum-float expressions
#  pragma warning (disable: 4127 5055)
#endif

namespace GeographicLib {

  using namespace std;

  void Math::dummy() {
    static_assert(GEOGRAPHICLIB_PRECISION >= 1 && GEOGRAPHICLIB_PRECISION <= 5,
                  "Bad value of precision");
  }

  template<typename T> T Math::sum(T u, T v, T& t) {
    GEOGRAPHICLIB_VOLATILE T s = u + v;
    GEOGRAPHICLIB_VOLATILE T up = s - v;
    GEOGRAPHICLIB_VOLATILE T vpp = s - up;
    up -= u;
    vpp -= v;
    // if s = 0, then t = 0 and give t the same sign as s
    // mpreal needs T(0) here
    t = s != 0 ? T(0) - (up + vpp) : s;
    // u + v =       s      + t
    //       = round(u + v) + t
    return s;
  }

  template<typename T> T Math::AngNormalize(T x) {
    T y = remainder(x, T(td));
#if GEOGRAPHICLIB_PRECISION == 4
    // boost-quadmath doesn't set the sign of 0 correctly, see
    // https://github.com/boostorg/multiprecision/issues/426
    // Fixed by https://github.com/boostorg/multiprecision/pull/428
    if (y == 0) y = copysign(y, x);
#endif
    return fabs(y) == T(hd) ? copysign(T(hd), x) : y;
  }

  template<typename T> T Math::NaN() {
#if defined(_MSC_VER)
    return numeric_limits<T>::has_quiet_NaN ?
      numeric_limits<T>::quiet_NaN() :
      (numeric_limits<T>::max)();
#else
    return numeric_limits<T>::has_quiet_NaN ?
      numeric_limits<T>::quiet_NaN() :
      numeric_limits<T>::max();
#endif
  }

  template<typename T> T Math::infinity() {
#if defined(_MSC_VER)
    return numeric_limits<T>::has_infinity ?
        numeric_limits<T>::infinity() :
        (numeric_limits<T>::max)();
#else
    return numeric_limits<T>::has_infinity ?
      numeric_limits<T>::infinity() :
      numeric_limits<T>::max();
#endif
    }

  /// \cond SKIP
  // Instantiate
#define GEOGRAPHICLIB_MATH_INSTANTIATE(T)                                  \
  template T    GEOGRAPHICLIB_EXPORT Math::sum          <T>(T, T, T&);     \
  template T    GEOGRAPHICLIB_EXPORT Math::AngNormalize <T>(T);            \
  template T    GEOGRAPHICLIB_EXPORT Math::NaN          <T>();             \
  template T    GEOGRAPHICLIB_EXPORT Math::infinity     <T>();
  // Instantiate with the standard floating type
  GEOGRAPHICLIB_MATH_INSTANTIATE(float)
  GEOGRAPHICLIB_MATH_INSTANTIATE(double)

#if GEOGRAPHICLIB_HAVE_LONG_DOUBLE
  // Instantiate if long double is distinct from double
  GEOGRAPHICLIB_MATH_INSTANTIATE(long double)
#endif
#if GEOGRAPHICLIB_PRECISION > 3
  // Instantiate with the high precision type
  GEOGRAPHICLIB_MATH_INSTANTIATE(Math::real)
#endif

#undef GEOGRAPHICLIB_MATH_INSTANTIATE

  // Also we need int versions for Utility::nummatch
  template int GEOGRAPHICLIB_EXPORT Math::NaN     <int>();
  template int GEOGRAPHICLIB_EXPORT Math::infinity<int>();
  /// \endcond

} // namespace GeographicLib
