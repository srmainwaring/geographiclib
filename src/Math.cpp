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

  int Math::digits() {
#if GEOGRAPHICLIB_PRECISION != 5
    return numeric_limits<real>::digits;
#else
    return numeric_limits<real>::digits();
#endif
  }

  int Math::set_digits(int ndigits) {
#if GEOGRAPHICLIB_PRECISION != 5
    (void)ndigits;
#else
    mpfr::mpreal::set_default_prec(ndigits >= 2 ? ndigits : 2);
#endif
    return digits();
  }

  int Math::digits10() {
#if GEOGRAPHICLIB_PRECISION != 5
    return numeric_limits<real>::digits10;
#else
    return numeric_limits<real>::digits10();
#endif
  }

  int Math::extra_digits() {
    return
      digits10() > numeric_limits<double>::digits10 ?
      digits10() - numeric_limits<double>::digits10 : 0;
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

  template<typename T> T Math::AngDiff(T x, T y, T& e) {
    // Use remainder instead of AngNormalize, since we treat boundary cases
    // later taking account of the error
    T d = sum(remainder(-x, T(td)), remainder( y, T(td)), e);
    // This second sum can only change d if abs(d) < 128, so don't need to
    // apply remainder yet again.
    d = sum(remainder(d, T(td)), e, e);
    // Fix the sign if d = -180, 0, 180.
    if (d == 0 || fabs(d) == hd)
      // If e == 0, take sign from y - x
      // else (e != 0, implies d = +/-180), d and e must have opposite signs
      d = copysign(d, e == 0 ? y - x : -e);
    return d;
  }

  template<typename T> T Math::AngRound(T x) {
    static const T z = T(1)/T(16);
    GEOGRAPHICLIB_VOLATILE T y = fabs(x);
    GEOGRAPHICLIB_VOLATILE T w = z - y;
    // The compiler mustn't "simplify" z - (z - y) to y
    y = w > 0 ? z - w : y;
    return copysign(y, x);
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
  template T    GEOGRAPHICLIB_EXPORT Math::AngDiff      <T>(T, T, T&);     \
  template T    GEOGRAPHICLIB_EXPORT Math::AngRound     <T>(T);            \
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
