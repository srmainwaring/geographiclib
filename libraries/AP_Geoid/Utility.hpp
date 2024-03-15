/**
 * \file Utility.hpp
 * \brief Header for GeographicLib::Utility class
 *
 * Copyright (c) Charles Karney (2011-2022) <karney@alum.mit.edu> and licensed
 * under the MIT/X11 License.  For more information, see
 * https://geographiclib.sourceforge.io/
 **********************************************************************/

#if !defined(GEOGRAPHICLIB_UTILITY_HPP)
#define GEOGRAPHICLIB_UTILITY_HPP 1

#include <limits>
#include <vector>
#include <sstream>
#include <cctype>

#include <AP_Geoid/Math.hpp>

namespace GeographicLib {

  /**
   * \brief Some utility routines for %GeographicLib
   *
   * Example of use:
   * \include example-Utility.cpp
   **********************************************************************/
  class Utility {
  public:

    /**
     * Read data of type ExtT from a binary stream to an array of type IntT.
     * The data in the file is in (bigendp ? big : little)-endian format.
     *
     * @tparam ExtT the type of the objects in the binary stream (external).
     * @tparam IntT the type of the objects in the array (internal).
     * @tparam bigendp true if the external storage format is big-endian.
     * @param[in] str the input stream containing the data of type ExtT
     *   (external).
     * @param[out] array the output array of type IntT (internal).
     * @param[in] num the size of the array.
     **********************************************************************/
    template<typename ExtT, typename IntT, bool bigendp>
      static bool readarray(std::istream& str, IntT array[], size_t num) {
      if (sizeof(IntT) == sizeof(ExtT) &&
          std::numeric_limits<IntT>::is_integer ==
          std::numeric_limits<ExtT>::is_integer)
        {
          // Data is compatible (aside from the issue of endian-ness).
          str.read(reinterpret_cast<char*>(array), num * sizeof(ExtT));
          if (!str.good()) {
            return false;
          }
          if (bigendp != Math::bigendian) { // endian mismatch -> swap bytes
            for (size_t i = num; i--;) {
              array[i] = Math::swab<IntT>(array[i]);
            }
          }
        }
      else
        {
          const int bufsize = 1024; // read this many values at a time
          ExtT buffer[bufsize];     // temporary buffer
          int k = int(num);         // data values left to read
          int i = 0;                // index into output array
          while (k) {
            int n = (std::min)(k, bufsize);
            str.read(reinterpret_cast<char*>(buffer), n * sizeof(ExtT));
            if (!str.good()) {
              return false;
            }
            for (int j = 0; j < n; ++j) {
              // fix endian-ness and cast to IntT
              array[i++] = IntT(bigendp == Math::bigendian ? buffer[j] :
                                Math::swab<ExtT>(buffer[j]));
            }
            k -= n;
          }
        }
      return true;
    }

    /**
     * Read data of type ExtT from a binary stream to a vector array of type
     * IntT.  The data in the file is in (bigendp ? big : little)-endian
     * format.
     *
     * @tparam ExtT the type of the objects in the binary stream (external).
     * @tparam IntT the type of the objects in the array (internal).
     * @tparam bigendp true if the external storage format is big-endian.
     * @param[in] str the input stream containing the data of type ExtT
     *   (external).
     * @param[out] array the output vector of type IntT (internal).
     **********************************************************************/
    template<typename ExtT, typename IntT, bool bigendp>
      static bool readarray(std::istream& str, std::vector<IntT>& array) {
      if (array.size() > 0) {
        return readarray<ExtT, IntT, bigendp>(str, &array[0], array.size());
      }
      return false;
    }

  };

} // namespace GeographicLib

#endif  // GEOGRAPHICLIB_UTILITY_HPP
