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
#include <GeographicLib/Constants.hpp>

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
#include <algorithm>
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
     * @return the number of bits of precision in a real number.
     **********************************************************************/
    static int digits();

    /**
     * Set the binary precision of a real number.
     *
     * @param[in] ndigits the number of bits of precision.
     * @return the resulting number of bits of precision.
     *
     * This only has an effect when GEOGRAPHICLIB_PRECISION = 5.  See also
     * Utility::set_digits for caveats about when this routine should be
     * called.
     **********************************************************************/
    static int set_digits(int ndigits);

    /**
     * @return the number of decimal digits of precision in a real number.
     **********************************************************************/
    static int digits10();

    /**
     * Number of additional decimal digits of precision for real relative to
     * double (0 for float).
     **********************************************************************/
    static int extra_digits();

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
     * The exact difference of two angles reduced to
     * [&minus;180&deg;, 180&deg;].
     *
     * @tparam T the type of the arguments and returned value.
     * @param[in] x the first angle in degrees.
     * @param[in] y the second angle in degrees.
     * @param[out] e the error term in degrees.
     * @return \e d, the truncated value of \e y &minus; \e x.
     *
     * This computes \e z = \e y &minus; \e x exactly, reduced to
     * [&minus;180&deg;, 180&deg;]; and then sets \e z = \e d + \e e where \e d
     * is the nearest representable number to \e z and \e e is the truncation
     * error.  If \e z = &plusmn;0&deg; or &plusmn;180&deg;, then the sign of
     * \e d is given by the sign of \e y &minus; \e x.  The maximum absolute
     * value of \e e is 2<sup>&minus;26</sup> (for doubles).
     **********************************************************************/
    template<typename T> static T AngDiff(T x, T y, T& e);

    /**
     * Difference of two angles reduced to [&minus;180&deg;, 180&deg;]
     *
     * @tparam T the type of the arguments and returned value.
     * @param[in] x the first angle in degrees.
     * @param[in] y the second angle in degrees.
     * @return \e y &minus; \e x, reduced to the range [&minus;180&deg;,
     *   180&deg;].
     *
     * The result is equivalent to computing the difference exactly, reducing
     * it to [&minus;180&deg;, 180&deg;] and rounding the result.
     **********************************************************************/
    template<typename T> static T AngDiff(T x, T y)
    { T e; return AngDiff(x, y, e); }

    /**
     * Coarsen a value close to zero.
     *
     * @tparam T the type of the argument and returned value.
     * @param[in] x
     * @return the coarsened value.
     *
     * The makes the smallest gap in \e x = 1/16 &minus; nextafter(1/16, 0) =
     * 1/2<sup>57</sup> for doubles = 0.8 pm on the earth if \e x is an angle
     * in degrees.  (This is about 2000 times more resolution than we get with
     * angles around 90&deg;.)  We use this to avoid having to deal with near
     * singular cases when \e x is non-zero but tiny (e.g.,
     * 10<sup>&minus;200</sup>).  This sign of &plusmn;0 is preserved.
     **********************************************************************/
    template<typename T> static T AngRound(T x);

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
