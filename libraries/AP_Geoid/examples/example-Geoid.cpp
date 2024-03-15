// Example of using the GeographicLib::Geoid class
// This requires that the egm96-5 geoid model be installed; see
// https://geographiclib.sourceforge.io/C++/doc/geoid.html#geoidinst

#include <exception>
#include <AP_Geoid/Geoid.hpp>

using namespace std;
using namespace GeographicLib;

int main() {
  try {
    Geoid egm96("egm96-5");
    // Convert height above egm96 to height above the ellipsoid
    double lat = 42, lon = -75, height_above_geoid = 20;
    double geoid_height;
    if (egm96(lat, lon, geoid_height)) {
      double height_above_ellipsoid = (height_above_geoid +
                                Geoid::GEOIDTOELLIPSOID * geoid_height);
      printf("%f\n", height_above_ellipsoid);
    }
  }
  catch (const exception& e) {
    printf("Caught exception: %s\n", e.what());
    return 1;
  }
}
