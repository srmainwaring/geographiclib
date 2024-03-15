/**
 * \file Constants.hpp
 * \brief Header for GeographicLib::Constants class
 *
 * Copyright (c) Charles Karney (2008-2022) <karney@alum.mit.edu> and licensed
 * under the MIT/X11 License.  For more information, see
 * https://geographiclib.sourceforge.io/
 **********************************************************************/

#if !defined(GEOGRAPHICLIB_CONSTANTS_HPP)
#define GEOGRAPHICLIB_CONSTANTS_HPP 1

#include <AP_Geoid/Config.h>

/**
 * @relates GeographicLib::Constants
 * Pack the version components into a single integer.  Users should not rely on
 * this particular packing of the components of the version number; see the
 * documentation for GEOGRAPHICLIB_VERSION, below.
 **********************************************************************/
#define GEOGRAPHICLIB_VERSION_NUM(a,b,c) ((((a) * 10000 + (b)) * 100) + (c))

/**
 * @relates GeographicLib::Constants
 * The version of GeographicLib as a single integer, packed as MMmmmmpp where
 * MM is the major version, mmmm is the minor version, and pp is the patch
 * level.  Users should not rely on this particular packing of the components
 * of the version number.  Instead they should use a test such as \code
   #if GEOGRAPHICLIB_VERSION >= GEOGRAPHICLIB_VERSION_NUM(1,37,0)
   ...
   #endif
 * \endcode
 **********************************************************************/
#define GEOGRAPHICLIB_VERSION \
 GEOGRAPHICLIB_VERSION_NUM(GEOGRAPHICLIB_VERSION_MAJOR, \
                           GEOGRAPHICLIB_VERSION_MINOR, \
                           GEOGRAPHICLIB_VERSION_PATCH)

#define GEOGRAPHICLIB_EXPORT

// Use GEOGRAPHICLIB_DEPRECATED to mark functions, types or variables as
// deprecated.  Code inspired by Apache Subversion's svn_types.h file (via
// MPFR).
#if defined(__GNUC__)
#  if __GNUC__ > 4
#    define GEOGRAPHICLIB_DEPRECATED(msg) __attribute__((deprecated(msg)))
#  else
#    define GEOGRAPHICLIB_DEPRECATED(msg) __attribute__((deprecated))
#  endif
#else
#  define GEOGRAPHICLIB_DEPRECATED(msg)
#endif

#include <stdexcept>
#include <string>
#include <AP_Geoid/Math.hpp>

/**
 * \brief Namespace for %GeographicLib
 *
 * All of %GeographicLib is defined within the GeographicLib namespace.  In
 * addition all the header files are included via %GeographicLib/Class.hpp.
 * This minimizes the likelihood of conflicts with other packages.
 **********************************************************************/
namespace GeographicLib {

  /**
   * \brief %Constants needed by %GeographicLib
   *
   * Define constants specifying the WGS84 ellipsoid, the UTM and UPS
   * projections, and various unit conversions.
   *
   * Example of use:
   * \include example-Constants.cpp
   **********************************************************************/
  class GEOGRAPHICLIB_EXPORT Constants {
  private:
    typedef Math::real real;
    Constants() = delete;       // Disable constructor

  public:
    /**
     * A synonym for Math::degree<real>().
     **********************************************************************/
    static Math::real degree() { return Math::degree(); }

    /** \name Ellipsoid parameters
     **********************************************************************/
    ///@{
    /**
     * @tparam T the type of the returned value.
     * @return the equatorial radius of WGS84 ellipsoid (6378137 m).
     **********************************************************************/
    template<typename T = real> static T WGS84_a()
    { return 6378137 * meter<T>(); }
    /**
     * @tparam T the type of the returned value.
     * @return the flattening of WGS84 ellipsoid (1/298.257223563).
     **********************************************************************/
    template<typename T = real> static T WGS84_f() {
      // Evaluating this as 1000000000 / T(298257223563LL) reduces the
      // round-off error by about 10%.  However, expressing the flattening as
      // 1/298.257223563 is well ingrained.
      return 1 / ( T(298257223563LL) / 1000000000 );
    }
    ///@}

    /** \name SI units
     **********************************************************************/
    ///@{
    /**
     * @tparam T the type of the returned value.
     * @return the number of meters in a meter.
     *
     * This is unity, but this lets the internal system of units be changed if
     * necessary.
     **********************************************************************/
    template<typename T = real> static T meter() { return T(1); }
    ///@}
  };

  /**
   * \brief Exception handling for %GeographicLib
   *
   * A class to handle exceptions.  It's derived from std::runtime_error so it
   * can be caught by the usual catch clauses.
   *
   * Example of use:
   * \include example-GeographicErr.cpp
   **********************************************************************/
  class GeographicErr : public std::runtime_error {
  public:

    /**
     * Constructor
     *
     * @param[in] msg a string message, which is accessible in the catch
     *   clause via what().
     **********************************************************************/
    GeographicErr(const std::string& msg) : std::runtime_error(msg) {}
  };

} // namespace GeographicLib

#endif  // GEOGRAPHICLIB_CONSTANTS_HPP
