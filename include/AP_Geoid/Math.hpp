/**
 * \file Math.hpp
 * \brief Header for GeographicLib::Math class
 *
 * Copyright (c) Charles Karney (2008-2023) <karney@alum.mit.edu> and licensed
 * under the MIT/X11 License.  For more information, see
 * https://geographiclib.sourceforge.io/
 **********************************************************************/

// Constants.hpp includes Math.hpp.  Place this include outside Math.hpp's
// include guard to enforce this ordering.
#include <AP_Geoid/Constants.hpp>

#if !defined(GEOGRAPHICLIB_MATH_HPP)
#define GEOGRAPHICLIB_MATH_HPP 1

#if !defined(GEOGRAPHICLIB_WORDS_BIGENDIAN)
#  define GEOGRAPHICLIB_WORDS_BIGENDIAN 0
#endif

#if !defined(GEOGRAPHICLIB_HAVE_LONG_DOUBLE)
#  define GEOGRAPHICLIB_HAVE_LONG_DOUBLE 0
#endif

#if !defined(GEOGRAPHICLIB_PRECISION)
/**
 * The precision of floating point numbers used in %GeographicLib.  1 means
 * float (single precision); 2 (the default) means double; 3 means long double;
 * 4 is reserved for quadruple precision.  Nearly all the testing has been
 * carried out with doubles and that's the recommended configuration.  In order
 * for long double to be used, GEOGRAPHICLIB_HAVE_LONG_DOUBLE needs to be
 * defined.  Note that with Microsoft Visual Studio, long double is the same as
 * double.
 **********************************************************************/
#  define GEOGRAPHICLIB_PRECISION 2
#endif

#include <cmath>
#include <limits>

#if GEOGRAPHICLIB_PRECISION == 4
#include <memory>
#include <boost/version.hpp>
#include <boost/multiprecision/float128.hpp>
#include <boost/math/special_functions.hpp>
#elif GEOGRAPHICLIB_PRECISION == 5
#include <mpreal.h>
#endif

#if GEOGRAPHICLIB_PRECISION > 3
// volatile keyword makes no sense for multiprec types
#define GEOGRAPHICLIB_VOLATILE
// Signal a convergence failure with multiprec types by throwing an exception
// at loop exit.
#define GEOGRAPHICLIB_PANIC \
  (throw GeographicLib::GeographicErr("Convergence failure"), false)
#else
#define GEOGRAPHICLIB_VOLATILE volatile
// Ignore convergence failures with standard floating points types by allowing
// loop to exit cleanly.
#define GEOGRAPHICLIB_PANIC false
#endif

namespace GeographicLib {

  /**
   * \brief Mathematical functions needed by %GeographicLib
   *
   * Define mathematical functions in order to localize system dependencies and
   * to provide generic versions of the functions.  In addition define a real
   * type to be used by %GeographicLib.
   *
   * Example of use:
   * \include example-Math.cpp
   **********************************************************************/
  class GEOGRAPHICLIB_EXPORT Math {
  private:
    void dummy();               // Static check for GEOGRAPHICLIB_PRECISION
    Math() = delete;            // Disable constructor
  public:

#if GEOGRAPHICLIB_HAVE_LONG_DOUBLE
    /**
     * The extended precision type for real numbers, used for some testing.
     * This is long double on computers with this type; otherwise it is double.
     **********************************************************************/
    typedef long double extended;
#else
    typedef double extended;
#endif

#if GEOGRAPHICLIB_PRECISION == 2
    /**
     * The real type for %GeographicLib. Nearly all the testing has been done
     * with \e real = double.  However, the algorithms should also work with
     * float and long double (where available).  (<b>CAUTION</b>: reasonable
     * accuracy typically cannot be obtained using floats.)
     **********************************************************************/
    typedef double real;
#elif GEOGRAPHICLIB_PRECISION == 1
    typedef float real;
#elif GEOGRAPHICLIB_PRECISION == 3
    typedef extended real;
#elif GEOGRAPHICLIB_PRECISION == 4
    typedef boost::multiprecision::float128 real;
#elif GEOGRAPHICLIB_PRECISION == 5
    typedef mpfr::mpreal real;
#else
    typedef double real;
#endif

    /**
     * The constants defining the standard (Babylonian) meanings of degrees,
     * minutes, and seconds, for angles.  Read the constants as follows (for
     * example): \e ms = 60 is the ratio 1 minute / 1 second.  The
     * abbreviations are
     * - \e t a whole turn (360&deg;)
     * - \e h a half turn (180&deg;)
     * - \e q a quarter turn (a right angle = 90&deg;)
     * - \e d a degree
     * - \e m a minute
     * - \e s a second
     * .
     * Note that degree() is ratio 1 degree / 1 radian, thus, for example,
     * Math::degree() * Math::qd is the ratio 1 quarter turn / 1 radian =
     * &pi;/2.
     *
     * Defining all these in one place would mean that it's simple to convert
     * to the centesimal system for measuring angles.  The DMS class assumes
     * that Math::dm and Math::ms are less than or equal to 100 (so that two
     * digits suffice for the integer parts of the minutes and degrees
     * components of an angle).  Switching to the centesimal convention will
     * break most of the tests.  Also the normal definition of degree is baked
     * into some classes, e.g., UTMUPS, MGRS, Georef, Geohash, etc.
     **********************************************************************/
    enum dms {
      qd = 90,                  ///< degrees per quarter turn
      dm = 60,                  ///< minutes per degree
      ms = 60,                  ///< seconds per minute
      hd = 2 * qd,              ///< degrees per half turn
      td = 2 * hd,              ///< degrees per turn
      ds = dm * ms              ///< seconds per degree
    };

    /**
     * true if the machine is big-endian.
     **********************************************************************/
    static const bool bigendian = GEOGRAPHICLIB_WORDS_BIGENDIAN;

    /**
     * @tparam T the type of the returned value.
     * @return &pi;.
     **********************************************************************/
    template<typename T = real> static T pi() {
      using std::atan2;
      static const T pi = atan2(T(0), T(-1));
      return pi;
    }

    /**
     * @tparam T the type of the returned value.
     * @return the number of radians in a degree.
     **********************************************************************/
    template<typename T = real> static T degree() {
      static const T degree = pi<T>() / T(hd);
      return degree;
    }

    /**
     * The error-free sum of two numbers.
     *
     * @tparam T the type of the argument and the returned value.
     * @param[in] u
     * @param[in] v
     * @param[out] t the exact error given by (\e u + \e v) - \e s.
     * @return \e s = round(\e u + \e v).
     *
     * See D. E. Knuth, TAOCP, Vol 2, 4.2.2, Theorem B.
     *
     * \note \e t can be the same as one of the first two arguments.
     **********************************************************************/
    template<typename T> static T sum(T u, T v, T& t);

    /**
     * Normalize an angle.
     *
     * @tparam T the type of the argument and returned value.
     * @param[in] x the angle in degrees.
     * @return the angle reduced to the range [&minus;180&deg;, 180&deg;].
     *
     * The range of \e x is unrestricted.  If the result is &plusmn;0&deg; or
     * &plusmn;180&deg; then the sign is the sign of \e x.
     **********************************************************************/
    template<typename T> static T AngNormalize(T x);

    /**
     * Normalize a latitude.
     *
     * @tparam T the type of the argument and returned value.
     * @param[in] x the angle in degrees.
     * @return x if it is in the range [&minus;90&deg;, 90&deg;], otherwise
     *   return NaN.
     **********************************************************************/
    template<typename T> static T LatFix(T x)
    { using std::fabs; return fabs(x) > T(qd) ? NaN<T>() : x; }

    /**
     * The NaN (not a number)
     *
     * @tparam T the type of the returned value.
     * @return NaN if available, otherwise return the max real of type T.
     **********************************************************************/
    template<typename T = real> static T NaN();

    /**
     * Infinity
     *
     * @tparam T the type of the returned value.
     * @return infinity if available, otherwise return the max real.
     **********************************************************************/
    template<typename T = real> static T infinity();

    /**
     * Swap the bytes of a quantity
     *
     * @tparam T the type of the argument and the returned value.
     * @param[in] x
     * @return x with its bytes swapped.
     **********************************************************************/
    template<typename T> static T swab(T x) {
      union {
        T r;
        unsigned char c[sizeof(T)];
      } b;
      b.r = x;
      for (int i = sizeof(T)/2; i--; )
        std::swap(b.c[i], b.c[sizeof(T) - 1 - i]);
      return b.r;
    }

  };

} // namespace GeographicLib

#endif  // GEOGRAPHICLIB_MATH_HPP
