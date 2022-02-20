/**
 * \file GeodesicExact.cpp
 * \brief Implementation for GeographicLib::GeodesicExact class
 *
 * Copyright (c) Charles Karney (2012-2021) <charles@karney.com> and licensed
 * under the MIT/X11 License.  For more information, see
 * https://geographiclib.sourceforge.io/
 *
 * This is a reformulation of the geodesic problem.  The notation is as
 * follows:
 * - at a general point (no suffix or 1 or 2 as suffix)
 *   - phi = latitude
 *   - beta = latitude on auxiliary sphere
 *   - omega = longitude on auxiliary sphere
 *   - lambda = longitude
 *   - alpha = azimuth of great circle
 *   - sigma = arc length along great circle
 *   - s = distance
 *   - tau = scaled distance (= sigma at multiples of pi/2)
 * - at northwards equator crossing
 *   - beta = phi = 0
 *   - omega = lambda = 0
 *   - alpha = alpha0
 *   - sigma = s = 0
 * - a 12 suffix means a difference, e.g., s12 = s2 - s1.
 * - s and c prefixes mean sin and cos
 **********************************************************************/

#include <GeographicLib/GeodesicExact.hpp>
#include <GeographicLib/GeodesicLineExact.hpp>

#if defined(_MSC_VER)
// Squelch warnings about potentially uninitialized local variables and
// constant conditional expressions
#  pragma warning (disable: 4701 4127)
#endif

namespace GeographicLib {

  using namespace std;

  GeodesicExact::GeodesicExact(real a, real f)
    : maxit2_(maxit1_ + Math::digits() + 10)
      // Underflow guard.  We require
      //   tiny_ * epsilon() > 0
      //   tiny_ + epsilon() == epsilon()
    , tiny_(sqrt(numeric_limits<real>::min()))
    , tol0_(numeric_limits<real>::epsilon())
      // Increase multiplier in defn of tol1_ from 100 to 200 to fix inverse
      // case 52.784459512564 0 -52.784459512563990912 179.634407464943777557
      // which otherwise failed for Visual Studio 10 (Release and Debug)
    , tol1_(200 * tol0_)
    , tol2_(sqrt(tol0_))
    , tolb_(tol0_ * tol2_)      // Check on bisection interval
    , xthresh_(1000 * tol2_)
    , _a(a)
    , _f(f)
    , _f1(1 - _f)
    , _e2(_f * (2 - _f))
    , _ep2(_e2 / Math::sq(_f1)) // e2 / (1 - e2)
    , _n(_f / ( 2 - _f))
    , _b(_a * _f1)
      // The Geodesic class substitutes atanh(sqrt(e2)) for asinh(sqrt(ep2)) in
      // the definition of _c2.  The latter is more accurate for very oblate
      // ellipsoids (which the Geodesic class does not attempt to handle).  Of
      // course, the area calculation in GeodesicExact is still based on a
      // series and so only holds for moderately oblate (or prolate)
      // ellipsoids.
    , _c2((Math::sq(_a) + Math::sq(_b) *
           (_f == 0 ? 1 :
            (_f > 0 ? asinh(sqrt(_ep2)) : atan(sqrt(-_e2))) /
            sqrt(fabs(_e2))))/2) // authalic radius squared
      // The sig12 threshold for "really short".  Using the auxiliary sphere
      // solution with dnm computed at (bet1 + bet2) / 2, the relative error in
      // the azimuth consistency check is sig12^2 * abs(f) * min(1, 1-f/2) / 2.
      // (Error measured for 1/100 < b/a < 100 and abs(f) >= 1/1000.  For a
      // given f and sig12, the max error occurs for lines near the pole.  If
      // the old rule for computing dnm = (dn1 + dn2)/2 is used, then the error
      // increases by a factor of 2.)  Setting this equal to epsilon gives
      // sig12 = etol2.  Here 0.1 is a safety factor (error decreased by 100)
      // and max(0.001, abs(f)) stops etol2 getting too large in the nearly
      // spherical case.
    , _etol2(real(0.1) * tol2_ /
             sqrt( fmax(real(0.001), fabs(_f)) * fmin(real(1), 1 - _f/2) / 2 ))
  {
    if (!(isfinite(_a) && _a > 0))
      throw GeographicErr("Equatorial radius is not positive");
    if (!(isfinite(_b) && _b > 0))
      throw GeographicErr("Polar semi-axis is not positive");
    C4coeff();
  }

  const GeodesicExact& GeodesicExact::WGS84() {
    static const GeodesicExact wgs84(Constants::WGS84_a(),
                                     Constants::WGS84_f());
    return wgs84;
  }

  Math::real GeodesicExact::CosSeries(real sinx, real cosx,
                                      const real c[], int n) {
    // Evaluate
    // y = sum(c[i] * cos((2*i+1) * x), i, 0, n-1)
    // using Clenshaw summation.
    // Approx operation count = (n + 5) mult and (2 * n + 2) add
    c += n ;                    // Point to one beyond last element
    real
      ar = 2 * (cosx - sinx) * (cosx + sinx), // 2 * cos(2 * x)
      y0 = n & 1 ? *--c : 0, y1 = 0;          // accumulators for sum
    // Now n is even
    n /= 2;
    while (n--) {
      // Unroll loop x 2, so accumulators return to their original role
      y1 = ar * y0 - y1 + *--c;
      y0 = ar * y1 - y0 + *--c;
    }
    return cosx * (y0 - y1);    // cos(x) * (y0 - y1)
  }

  GeodesicLineExact GeodesicExact::Line(real lat1, real lon1, real azi1,
                                        unsigned caps) const {
    return GeodesicLineExact(*this, lat1, lon1, azi1, caps);
  }

  Math::real GeodesicExact::GenDirect(real lat1, real lon1, real azi1,
                                      bool arcmode, real s12_a12,
                                      unsigned outmask,
                                      real& lat2, real& lon2, real& azi2,
                                      real& s12, real& m12,
                                      real& M12, real& M21,
                                      real& S12) const {
    // Automatically supply DISTANCE_IN if necessary
    if (!arcmode) outmask |= DISTANCE_IN;
    return GeodesicLineExact(*this, lat1, lon1, azi1, outmask)
      .                         // Note the dot!
      GenPosition(arcmode, s12_a12, outmask,
                  lat2, lon2, azi2, s12, m12, M12, M21, S12);
  }

  GeodesicLineExact GeodesicExact::GenDirectLine(real lat1, real lon1,
                                                 real azi1,
                                                 bool arcmode, real s12_a12,
                                                 unsigned caps) const {
    azi1 = Math::AngNormalize(azi1);
    real salp1, calp1;
    // Guard against underflow in salp0.  Also -0 is converted to +0.
    Math::sincosd(Math::AngRound(azi1), salp1, calp1);
    // Automatically supply DISTANCE_IN if necessary
    if (!arcmode) caps |= DISTANCE_IN;
    return GeodesicLineExact(*this, lat1, lon1, azi1, salp1, calp1,
                             caps, arcmode, s12_a12);
  }

  GeodesicLineExact GeodesicExact::DirectLine(real lat1, real lon1,
                                              real azi1, real s12,
                                              unsigned caps) const {
    return GenDirectLine(lat1, lon1, azi1, false, s12, caps);
  }

  GeodesicLineExact GeodesicExact::ArcDirectLine(real lat1, real lon1,
                                                 real azi1, real a12,
                                                 unsigned caps) const {
    return GenDirectLine(lat1, lon1, azi1, true, a12, caps);
  }

  Math::real GeodesicExact::GenInverse(real lat1, real lon1,
                                       real lat2, real lon2,
                                       unsigned outmask, real& s12,
                                       real& salp1, real& calp1,
                                       real& salp2, real& calp2,
                                       real& m12, real& M12, real& M21,
                                       real& S12) const {
    // Compute longitude difference (AngDiff does this carefully).  Result is
    // in [-180, 180] but -180 is only for west-going geodesics.  180 is for
    // east-going and meridional geodesics.
    using std::isnan;           // Needed for Centos 7, ubuntu 14
    real lon12s, lon12 = Math::AngDiff(lon1, lon2, lon12s);
    // Make longitude difference positive.
    int lonsign = signbit(lon12) ? -1 : 1;
    // If very close to being on the same half-meridian, then make it so.
    lon12 = lonsign * Math::AngRound(lon12);
    lon12s = Math::AngRound((180 - lon12) - lonsign * lon12s);
    real
      lam12 = lon12 * Math::degree(),
      slam12, clam12;
    if (lon12 > 90) {
      Math::sincosd(lon12s, slam12, clam12);
      clam12 = -clam12;
    } else
      Math::sincosd(lon12, slam12, clam12);

    // If really close to the equator, treat as on equator.
    lat1 = Math::AngRound(Math::LatFix(lat1));
    lat2 = Math::AngRound(Math::LatFix(lat2));
    // Swap points so that point with higher (abs) latitude is point 1
    // If one latitude is a nan, then it becomes lat1.
    int swapp = fabs(lat1) < fabs(lat2) || isnan(lat2) ? -1 : 1;
    if (swapp < 0) {
      lonsign *= -1;
      swap(lat1, lat2);
    }
    // Make lat1 <= 0
    int latsign = signbit(lat1) ? 1 : -1;
    lat1 *= latsign;
    lat2 *= latsign;
    // Now we have
    //
    //     0 <= lon12 <= 180
    //     -90 <= lat1 <= 0
    //     lat1 <= lat2 <= -lat1
    //
    // longsign, swapp, latsign register the transformation to bring the
    // coordinates to this canonical form.  In all cases, 1 means no change was
    // made.  We make these transformations so that there are few cases to
    // check, e.g., on verifying quadrants in atan2.  In addition, this
    // enforces some symmetries in the results returned.

    real sbet1, cbet1, sbet2, cbet2, s12x, m12x;
    // Initialize for the meridian.  No longitude calculation is done in this
    // case to let the parameter default to 0.
    EllipticFunction E(-_ep2);

    Math::sincosd(lat1, sbet1, cbet1); sbet1 *= _f1;
    // Ensure cbet1 = +epsilon at poles; doing the fix on beta means that sig12
    // will be <= 2*tiny for two points at the same pole.
    Math::norm(sbet1, cbet1); cbet1 = fmax(tiny_, cbet1);

    Math::sincosd(lat2, sbet2, cbet2); sbet2 *= _f1;
    // Ensure cbet2 = +epsilon at poles
    Math::norm(sbet2, cbet2); cbet2 = fmax(tiny_, cbet2);

    // If cbet1 < -sbet1, then cbet2 - cbet1 is a sensitive measure of the
    // |bet1| - |bet2|.  Alternatively (cbet1 >= -sbet1), abs(sbet2) + sbet1 is
    // a better measure.  This logic is used in assigning calp2 in Lambda12.
    // Sometimes these quantities vanish and in that case we force bet2 = +/-
    // bet1 exactly.  An example where is is necessary is the inverse problem
    // 48.522876735459 0 -48.52287673545898293 179.599720456223079643
    // which failed with Visual Studio 10 (Release and Debug)

    if (cbet1 < -sbet1) {
      if (cbet2 == cbet1)
        sbet2 = sbet2 < 0 ? sbet1 : -sbet1;
    } else {
      if (fabs(sbet2) == -sbet1)
        cbet2 = cbet1;
    }

    real
      dn1 = (_f >= 0 ? sqrt(1 + _ep2 * Math::sq(sbet1)) :
             sqrt(1 - _e2 * Math::sq(cbet1)) / _f1),
      dn2 = (_f >= 0 ? sqrt(1 + _ep2 * Math::sq(sbet2)) :
             sqrt(1 - _e2 * Math::sq(cbet2)) / _f1);

    real a12, sig12;

    bool meridian = lat1 == -90 || slam12 == 0;

    if (meridian) {

      // Endpoints are on a single full meridian, so the geodesic might lie on
      // a meridian.

      calp1 = clam12; salp1 = slam12; // Head to the target longitude
      calp2 = 1; salp2 = 0;           // At the target we're heading north

      real
        // tan(bet) = tan(sig) * cos(alp)
        ssig1 = sbet1, csig1 = calp1 * cbet1,
        ssig2 = sbet2, csig2 = calp2 * cbet2;

      // sig12 = sig2 - sig1
      sig12 = atan2(fmax(real(0), csig1 * ssig2 - ssig1 * csig2),
                                  csig1 * csig2 + ssig1 * ssig2);
      {
        real dummy;
        Lengths(E, sig12, ssig1, csig1, dn1, ssig2, csig2, dn2,
                cbet1, cbet2, outmask | REDUCEDLENGTH,
                s12x, m12x, dummy, M12, M21);
      }
      // Add the check for sig12 since zero length geodesics might yield m12 <
      // 0.  Test case was
      //
      //    echo 20.001 0 20.001 0 | GeodSolve -i
      //
      // In fact, we will have sig12 > pi/2 for meridional geodesic which is
      // not a shortest path.
      if (sig12 < 1 || m12x >= 0) {
        // Need at least 2, to handle 90 0 90 180
        if (sig12 < 3 * tiny_ ||
            // Prevent negative s12 or m12 for short lines
            (sig12 < tol0_ && (s12x < 0 || m12x < 0)))
          sig12 = m12x = s12x = 0;
        m12x *= _b;
        s12x *= _b;
        a12 = sig12 / Math::degree();
      } else
        // m12 < 0, i.e., prolate and too close to anti-podal
        meridian = false;
    }

    // somg12 > 1 marks that it needs to be calculated
    real omg12 = 0, somg12 = 2, comg12 = 0;
    if (!meridian &&
        sbet1 == 0 &&   // and sbet2 == 0
        (_f <= 0 || lon12s >= _f * 180)) {

      // Geodesic runs along equator
      calp1 = calp2 = 0; salp1 = salp2 = 1;
      s12x = _a * lam12;
      sig12 = omg12 = lam12 / _f1;
      m12x = _b * sin(sig12);
      if (outmask & GEODESICSCALE)
        M12 = M21 = cos(sig12);
      a12 = lon12 / _f1;

    } else if (!meridian) {

      // Now point1 and point2 belong within a hemisphere bounded by a
      // meridian and geodesic is neither meridional or equatorial.

      // Figure a starting point for Newton's method
      real dnm;
      sig12 = InverseStart(E, sbet1, cbet1, dn1, sbet2, cbet2, dn2,
                           lam12, slam12, clam12,
                           salp1, calp1, salp2, calp2, dnm);

      if (sig12 >= 0) {
        // Short lines (InverseStart sets salp2, calp2, dnm)
        s12x = sig12 * _b * dnm;
        m12x = Math::sq(dnm) * _b * sin(sig12 / dnm);
        if (outmask & GEODESICSCALE)
          M12 = M21 = cos(sig12 / dnm);
        a12 = sig12 / Math::degree();
        omg12 = lam12 / (_f1 * dnm);
      } else {

        // Newton's method.  This is a straightforward solution of f(alp1) =
        // lambda12(alp1) - lam12 = 0 with one wrinkle.  f(alp) has exactly one
        // root in the interval (0, pi) and its derivative is positive at the
        // root.  Thus f(alp) is positive for alp > alp1 and negative for alp <
        // alp1.  During the course of the iteration, a range (alp1a, alp1b) is
        // maintained which brackets the root and with each evaluation of
        // f(alp) the range is shrunk, if possible.  Newton's method is
        // restarted whenever the derivative of f is negative (because the new
        // value of alp1 is then further from the solution) or if the new
        // estimate of alp1 lies outside (0,pi); in this case, the new starting
        // guess is taken to be (alp1a + alp1b) / 2.
        //
        // initial values to suppress warnings (if loop is executed 0 times)
        real ssig1 = 0, csig1 = 0, ssig2 = 0, csig2 = 0, domg12 = 0;
        unsigned numit = 0;
        // Bracketing range
        real salp1a = tiny_, calp1a = 1, salp1b = tiny_, calp1b = -1;
        for (bool tripn = false, tripb = false;
             numit < maxit2_ || GEOGRAPHICLIB_PANIC;
             ++numit) {
          // 1/4 meridian = 10e6 m and random input.  max err is estimated max
          // error in nm (checking solution of inverse problem by direct
          // solution).  iter is mean and sd of number of iterations
          //
          //           max   iter
          // log2(b/a) err mean  sd
          //    -7     387 5.33 3.68
          //    -6     345 5.19 3.43
          //    -5     269 5.00 3.05
          //    -4     210 4.76 2.44
          //    -3     115 4.55 1.87
          //    -2      69 4.35 1.38
          //    -1      36 4.05 1.03
          //     0      15 0.01 0.13
          //     1      25 5.10 1.53
          //     2      96 5.61 2.09
          //     3     318 6.02 2.74
          //     4     985 6.24 3.22
          //     5    2352 6.32 3.44
          //     6    6008 6.30 3.45
          //     7   19024 6.19 3.30
          real dv;
          real v = Lambda12(sbet1, cbet1, dn1, sbet2, cbet2, dn2, salp1, calp1,
                            slam12, clam12,
                            salp2, calp2, sig12, ssig1, csig1, ssig2, csig2,
                            E, domg12, numit < maxit1_, dv);
          // Reversed test to allow escape with NaNs
          if (tripb || !(fabs(v) >= (tripn ? 8 : 1) * tol0_)) break;
          // Update bracketing values
          if (v > 0 && (numit > maxit1_ || calp1/salp1 > calp1b/salp1b))
            { salp1b = salp1; calp1b = calp1; }
          else if (v < 0 && (numit > maxit1_ || calp1/salp1 < calp1a/salp1a))
            { salp1a = salp1; calp1a = calp1; }
          if (numit < maxit1_ && dv > 0) {
            real
              dalp1 = -v/dv;
            real
              sdalp1 = sin(dalp1), cdalp1 = cos(dalp1),
              nsalp1 = salp1 * cdalp1 + calp1 * sdalp1;
            if (nsalp1 > 0 && fabs(dalp1) < Math::pi()) {
              calp1 = calp1 * cdalp1 - salp1 * sdalp1;
              salp1 = nsalp1;
              Math::norm(salp1, calp1);
              // In some regimes we don't get quadratic convergence because
              // slope -> 0.  So use convergence conditions based on epsilon
              // instead of sqrt(epsilon).
              tripn = fabs(v) <= 16 * tol0_;
              continue;
            }
          }
          // Either dv was not positive or updated value was outside legal
          // range.  Use the midpoint of the bracket as the next estimate.
          // This mechanism is not needed for the WGS84 ellipsoid, but it does
          // catch problems with more eccentric ellipsoids.  Its efficacy is
          // such for the WGS84 test set with the starting guess set to alp1 =
          // 90deg:
          // the WGS84 test set: mean = 5.21, sd = 3.93, max = 24
          // WGS84 and random input: mean = 4.74, sd = 0.99
          salp1 = (salp1a + salp1b)/2;
          calp1 = (calp1a + calp1b)/2;
          Math::norm(salp1, calp1);
          tripn = false;
          tripb = (fabs(salp1a - salp1) + (calp1a - calp1) < tolb_ ||
                   fabs(salp1 - salp1b) + (calp1 - calp1b) < tolb_);
        }
        {
          real dummy;
          Lengths(E, sig12, ssig1, csig1, dn1, ssig2, csig2, dn2,
                  cbet1, cbet2, outmask, s12x, m12x, dummy, M12, M21);
        }
        m12x *= _b;
        s12x *= _b;
        a12 = sig12 / Math::degree();
        if (outmask & AREA) {
          // omg12 = lam12 - domg12
          real sdomg12 = sin(domg12), cdomg12 = cos(domg12);
          somg12 = slam12 * cdomg12 - clam12 * sdomg12;
          comg12 = clam12 * cdomg12 + slam12 * sdomg12;
        }
      }
    }

    if (outmask & DISTANCE)
      s12 = real(0) + s12x;     // Convert -0 to 0

    if (outmask & REDUCEDLENGTH)
      m12 = real(0) + m12x;     // Convert -0 to 0

    if (outmask & AREA) {
      real
        // From Lambda12: sin(alp1) * cos(bet1) = sin(alp0)
        salp0 = salp1 * cbet1,
        calp0 = hypot(calp1, salp1 * sbet1); // calp0 > 0
      real alp12;
      if (calp0 != 0 && salp0 != 0) {
        real
          // From Lambda12: tan(bet) = tan(sig) * cos(alp)
          ssig1 = sbet1, csig1 = calp1 * cbet1,
          ssig2 = sbet2, csig2 = calp2 * cbet2,
          k2 = Math::sq(calp0) * _ep2,
          eps = k2 / (2 * (1 + sqrt(1 + k2)) + k2),
          // Multiplier = a^2 * e^2 * cos(alpha0) * sin(alpha0).
          A4 = Math::sq(_a) * calp0 * salp0 * _e2;
        Math::norm(ssig1, csig1);
        Math::norm(ssig2, csig2);
        real C4a[nC4_];
        C4f(eps, C4a);
        real
          B41 = CosSeries(ssig1, csig1, C4a, nC4_),
          B42 = CosSeries(ssig2, csig2, C4a, nC4_);
        S12 = A4 * (B42 - B41);
      } else
        // Avoid problems with indeterminate sig1, sig2 on equator
        S12 = 0;

      if (!meridian) {
        if (somg12 > 1) {
          somg12 = sin(omg12); comg12 = cos(omg12);
        }
      }

      if (!meridian &&
          // omg12 < 3/4 * pi
          comg12 > -real(0.7071) &&     // Long difference not too big
          sbet2 - sbet1 < real(1.75)) { // Lat difference not too big
        // Use tan(Gamma/2) = tan(omg12/2)
        // * (tan(bet1/2)+tan(bet2/2))/(1+tan(bet1/2)*tan(bet2/2))
        // with tan(x/2) = sin(x)/(1+cos(x))
        real domg12 = 1 + comg12, dbet1 = 1 + cbet1, dbet2 = 1 + cbet2;
        alp12 = 2 * atan2( somg12 * ( sbet1 * dbet2 + sbet2 * dbet1 ),
                           domg12 * ( sbet1 * sbet2 + dbet1 * dbet2 ) );
      } else {
        // alp12 = alp2 - alp1, used in atan2 so no need to normalize
        real
          salp12 = salp2 * calp1 - calp2 * salp1,
          calp12 = calp2 * calp1 + salp2 * salp1;
        // The right thing appears to happen if alp1 = +/-180 and alp2 = 0, viz
        // salp12 = -0 and alp12 = -180.  However this depends on the sign
        // being attached to 0 correctly.  The following ensures the correct
        // behavior.
        if (salp12 == 0 && calp12 < 0) {
          salp12 = tiny_ * calp1;
          calp12 = -1;
        }
        alp12 = atan2(salp12, calp12);
      }
      S12 += _c2 * alp12;
      S12 *= swapp * lonsign * latsign;
      // Convert -0 to 0
      S12 += 0;
    }

    // Convert calp, salp to azimuth accounting for lonsign, swapp, latsign.
    if (swapp < 0) {
      swap(salp1, salp2);
      swap(calp1, calp2);
      if (outmask & GEODESICSCALE)
        swap(M12, M21);
    }

    salp1 *= swapp * lonsign; calp1 *= swapp * latsign;
    salp2 *= swapp * lonsign; calp2 *= swapp * latsign;

    // Returned value in [0, 180]
    return a12;
  }

  Math::real GeodesicExact::GenInverse(real lat1, real lon1,
                                       real lat2, real lon2,
                                       unsigned outmask,
                                       real& s12, real& azi1, real& azi2,
                                       real& m12, real& M12, real& M21,
                                       real& S12) const {
    outmask &= OUT_MASK;
    real salp1, calp1, salp2, calp2,
      a12 =  GenInverse(lat1, lon1, lat2, lon2,
                        outmask, s12, salp1, calp1, salp2, calp2,
                        m12, M12, M21, S12);
    if (outmask & AZIMUTH) {
      azi1 = Math::atan2d(salp1, calp1);
      azi2 = Math::atan2d(salp2, calp2);
    }
    return a12;
  }

  GeodesicLineExact GeodesicExact::InverseLine(real lat1, real lon1,
                                               real lat2, real lon2,
                                               unsigned caps) const {
    real t, salp1, calp1, salp2, calp2,
      a12 = GenInverse(lat1, lon1, lat2, lon2,
                       // No need to specify AZIMUTH here
                       0u, t, salp1, calp1, salp2, calp2,
                       t, t, t, t),
      azi1 = Math::atan2d(salp1, calp1);
    // Ensure that a12 can be converted to a distance
    if (caps & (OUT_MASK & DISTANCE_IN)) caps |= DISTANCE;
    return GeodesicLineExact(*this, lat1, lon1, azi1, salp1, calp1, caps,
                             true, a12);
  }

  void GeodesicExact::Lengths(const EllipticFunction& E,
                              real sig12,
                              real ssig1, real csig1, real dn1,
                              real ssig2, real csig2, real dn2,
                              real cbet1, real cbet2, unsigned outmask,
                              real& s12b, real& m12b, real& m0,
                              real& M12, real& M21) const {
    // Return m12b = (reduced length)/_b; also calculate s12b = distance/_b,
    // and m0 = coefficient of secular term in expression for reduced length.

    outmask &= OUT_ALL;
    // outmask & DISTANCE: set s12b
    // outmask & REDUCEDLENGTH: set m12b & m0
    // outmask & GEODESICSCALE: set M12 & M21

    // It's OK to have repeated dummy arguments,
    // e.g., s12b = m0 = M12 = M21 = dummy

    if (outmask & DISTANCE)
      // Missing a factor of _b
      s12b = E.E() / (Math::pi() / 2) *
        (sig12 + (E.deltaE(ssig2, csig2, dn2) - E.deltaE(ssig1, csig1, dn1)));
    if (outmask & (REDUCEDLENGTH | GEODESICSCALE)) {
      real
        m0x = - E.k2() * E.D() / (Math::pi() / 2),
        J12 = m0x *
        (sig12 + (E.deltaD(ssig2, csig2, dn2) - E.deltaD(ssig1, csig1, dn1)));
      if (outmask & REDUCEDLENGTH) {
        m0 = m0x;
        // Missing a factor of _b.  Add parens around (csig1 * ssig2) and
        // (ssig1 * csig2) to ensure accurate cancellation in the case of
        // coincident points.
        m12b = dn2 * (csig1 * ssig2) - dn1 * (ssig1 * csig2) -
          csig1 * csig2 * J12;
      }
      if (outmask & GEODESICSCALE) {
        real csig12 = csig1 * csig2 + ssig1 * ssig2;
        real t = _ep2 * (cbet1 - cbet2) * (cbet1 + cbet2) / (dn1 + dn2);
        M12 = csig12 + (t * ssig2 - csig2 * J12) * ssig1 / dn1;
        M21 = csig12 - (t * ssig1 - csig1 * J12) * ssig2 / dn2;
      }
    }
  }

  Math::real GeodesicExact::Astroid(real x, real y) {
    // Solve k^4+2*k^3-(x^2+y^2-1)*k^2-2*y^2*k-y^2 = 0 for positive root k.
    // This solution is adapted from Geocentric::Reverse.
    real k;
    real
      p = Math::sq(x),
      q = Math::sq(y),
      r = (p + q - 1) / 6;
    if ( !(q == 0 && r <= 0) ) {
      real
        // Avoid possible division by zero when r = 0 by multiplying equations
        // for s and t by r^3 and r, resp.
        S = p * q / 4,            // S = r^3 * s
        r2 = Math::sq(r),
        r3 = r * r2,
        // The discriminant of the quadratic equation for T3.  This is zero on
        // the evolute curve p^(1/3)+q^(1/3) = 1
        disc = S * (S + 2 * r3);
      real u = r;
      if (disc >= 0) {
        real T3 = S + r3;
        // Pick the sign on the sqrt to maximize abs(T3).  This minimizes loss
        // of precision due to cancellation.  The result is unchanged because
        // of the way the T is used in definition of u.
        T3 += T3 < 0 ? -sqrt(disc) : sqrt(disc); // T3 = (r * t)^3
        // N.B. cbrt always returns the real root.  cbrt(-8) = -2.
        real T = cbrt(T3); // T = r * t
        // T can be zero; but then r2 / T -> 0.
        u += T + (T != 0 ? r2 / T : 0);
      } else {
        // T is complex, but the way u is defined the result is real.
        real ang = atan2(sqrt(-disc), -(S + r3));
        // There are three possible cube roots.  We choose the root which
        // avoids cancellation.  Note that disc < 0 implies that r < 0.
        u += 2 * r * cos(ang / 3);
      }
      real
        v = sqrt(Math::sq(u) + q),    // guaranteed positive
        // Avoid loss of accuracy when u < 0.
        uv = u < 0 ? q / (v - u) : u + v, // u+v, guaranteed positive
        w = (uv - q) / (2 * v);           // positive?
      // Rearrange expression for k to avoid loss of accuracy due to
      // subtraction.  Division by 0 not possible because uv > 0, w >= 0.
      k = uv / (sqrt(uv + Math::sq(w)) + w);   // guaranteed positive
    } else {               // q == 0 && r <= 0
      // y = 0 with |x| <= 1.  Handle this case directly.
      // for y small, positive root is k = abs(y)/sqrt(1-x^2)
      k = 0;
    }
    return k;
  }

  Math::real GeodesicExact::InverseStart(EllipticFunction& E,
                                         real sbet1, real cbet1, real dn1,
                                         real sbet2, real cbet2, real dn2,
                                         real lam12, real slam12, real clam12,
                                         real& salp1, real& calp1,
                                         // Only updated if return val >= 0
                                         real& salp2, real& calp2,
                                         // Only updated for short lines
                                         real& dnm) const {
    // Return a starting point for Newton's method in salp1 and calp1 (function
    // value is -1).  If Newton's method doesn't need to be used, return also
    // salp2 and calp2 and function value is sig12.
    real
      sig12 = -1,               // Return value
      // bet12 = bet2 - bet1 in [0, pi); bet12a = bet2 + bet1 in (-pi, 0]
      sbet12 = sbet2 * cbet1 - cbet2 * sbet1,
      cbet12 = cbet2 * cbet1 + sbet2 * sbet1;
    real sbet12a = sbet2 * cbet1 + cbet2 * sbet1;
    bool shortline = cbet12 >= 0 && sbet12 < real(0.5) &&
      cbet2 * lam12 < real(0.5);
    real somg12, comg12;
    if (shortline) {
      real sbetm2 = Math::sq(sbet1 + sbet2);
      // sin((bet1+bet2)/2)^2
      // =  (sbet1 + sbet2)^2 / ((sbet1 + sbet2)^2 + (cbet1 + cbet2)^2)
      sbetm2 /= sbetm2 + Math::sq(cbet1 + cbet2);
      dnm = sqrt(1 + _ep2 * sbetm2);
      real omg12 = lam12 / (_f1 * dnm);
      somg12 = sin(omg12); comg12 = cos(omg12);
    } else {
      somg12 = slam12; comg12 = clam12;
    }

    salp1 = cbet2 * somg12;
    calp1 = comg12 >= 0 ?
      sbet12 + cbet2 * sbet1 * Math::sq(somg12) / (1 + comg12) :
      sbet12a - cbet2 * sbet1 * Math::sq(somg12) / (1 - comg12);

    real
      ssig12 = hypot(salp1, calp1),
      csig12 = sbet1 * sbet2 + cbet1 * cbet2 * comg12;

    if (shortline && ssig12 < _etol2) {
      // really short lines
      salp2 = cbet1 * somg12;
      calp2 = sbet12 - cbet1 * sbet2 *
        (comg12 >= 0 ? Math::sq(somg12) / (1 + comg12) : 1 - comg12);
      Math::norm(salp2, calp2);
      // Set return value
      sig12 = atan2(ssig12, csig12);
    } else if (fabs(_n) > real(0.1) || // Skip astroid calc if too eccentric
               csig12 >= 0 ||
               ssig12 >= 6 * fabs(_n) * Math::pi() * Math::sq(cbet1)) {
      // Nothing to do, zeroth order spherical approximation is OK
    } else {
      // Scale lam12 and bet2 to x, y coordinate system where antipodal point
      // is at origin and singular point is at y = 0, x = -1.
      real y, lamscale, betscale;
      // Volatile declaration needed to fix inverse case
      // 56.320923501171 0 -56.320923501171 179.664747671772880215
      // which otherwise fails with g++ 4.4.4 x86 -O3
      GEOGRAPHICLIB_VOLATILE real x;
      real lam12x = atan2(-slam12, -clam12); // lam12 - pi
      if (_f >= 0) {            // In fact f == 0 does not get here
        // x = dlong, y = dlat
        {
          real k2 = Math::sq(sbet1) * _ep2;
          E.Reset(-k2, -_ep2, 1 + k2, 1 + _ep2);
          lamscale = _e2/_f1 * cbet1 * 2 * E.H();
        }
        betscale = lamscale * cbet1;

        x = lam12x / lamscale;
        y = sbet12a / betscale;
      } else {                  // _f < 0
        // x = dlat, y = dlong
        real
          cbet12a = cbet2 * cbet1 - sbet2 * sbet1,
          bet12a = atan2(sbet12a, cbet12a);
        real m12b, m0, dummy;
        // In the case of lon12 = 180, this repeats a calculation made in
        // Inverse.
        Lengths(E, Math::pi() + bet12a,
                sbet1, -cbet1, dn1, sbet2, cbet2, dn2,
                cbet1, cbet2, REDUCEDLENGTH, dummy, m12b, m0, dummy, dummy);
        x = -1 + m12b / (cbet1 * cbet2 * m0 * Math::pi());
        betscale = x < -real(0.01) ? sbet12a / x :
          -_f * Math::sq(cbet1) * Math::pi();
        lamscale = betscale / cbet1;
        y = lam12x / lamscale;
      }

      if (y > -tol1_ && x > -1 - xthresh_) {
        // strip near cut
        // Need real(x) here to cast away the volatility of x for min/max
        if (_f >= 0) {
          salp1 = fmin(real(1), -real(x)); calp1 = - sqrt(1 - Math::sq(salp1));
        } else {
          calp1 = fmax(real(x > -tol1_ ? 0 : -1), real(x));
          salp1 = sqrt(1 - Math::sq(calp1));
        }
      } else {
        // Estimate alp1, by solving the astroid problem.
        //
        // Could estimate alpha1 = theta + pi/2, directly, i.e.,
        //   calp1 = y/k; salp1 = -x/(1+k);  for _f >= 0
        //   calp1 = x/(1+k); salp1 = -y/k;  for _f < 0 (need to check)
        //
        // However, it's better to estimate omg12 from astroid and use
        // spherical formula to compute alp1.  This reduces the mean number of
        // Newton iterations for astroid cases from 2.24 (min 0, max 6) to 2.12
        // (min 0 max 5).  The changes in the number of iterations are as
        // follows:
        //
        // change percent
        //    1       5
        //    0      78
        //   -1      16
        //   -2       0.6
        //   -3       0.04
        //   -4       0.002
        //
        // The histogram of iterations is (m = number of iterations estimating
        // alp1 directly, n = number of iterations estimating via omg12, total
        // number of trials = 148605):
        //
        //  iter    m      n
        //    0   148    186
        //    1 13046  13845
        //    2 93315 102225
        //    3 36189  32341
        //    4  5396      7
        //    5   455      1
        //    6    56      0
        //
        // Because omg12 is near pi, estimate work with omg12a = pi - omg12
        real k = Astroid(x, y);
        real
          omg12a = lamscale * ( _f >= 0 ? -x * k/(1 + k) : -y * (1 + k)/k );
        somg12 = sin(omg12a); comg12 = -cos(omg12a);
        // Update spherical estimate of alp1 using omg12 instead of lam12
        salp1 = cbet2 * somg12;
        calp1 = sbet12a - cbet2 * sbet1 * Math::sq(somg12) / (1 - comg12);
      }
    }
    // Sanity check on starting guess.  Backwards check allows NaN through.
    if (!(salp1 <= 0))
      Math::norm(salp1, calp1);
    else {
      salp1 = 1; calp1 = 0;
    }
    return sig12;
  }

  Math::real GeodesicExact::Lambda12(real sbet1, real cbet1, real dn1,
                                     real sbet2, real cbet2, real dn2,
                                     real salp1, real calp1,
                                     real slam120, real clam120,
                                     real& salp2, real& calp2,
                                     real& sig12,
                                     real& ssig1, real& csig1,
                                     real& ssig2, real& csig2,
                                     EllipticFunction& E,
                                     real& domg12,
                                     bool diffp, real& dlam12) const
    {

    if (sbet1 == 0 && calp1 == 0)
      // Break degeneracy of equatorial line.  This case has already been
      // handled.
      calp1 = -tiny_;

    real
      // sin(alp1) * cos(bet1) = sin(alp0)
      salp0 = salp1 * cbet1,
      calp0 = hypot(calp1, salp1 * sbet1); // calp0 > 0

    real somg1, comg1, somg2, comg2, somg12, comg12, cchi1, cchi2, lam12;
    // tan(bet1) = tan(sig1) * cos(alp1)
    // tan(omg1) = sin(alp0) * tan(sig1) = tan(omg1)=tan(alp1)*sin(bet1)
    ssig1 = sbet1; somg1 = salp0 * sbet1;
    csig1 = comg1 = calp1 * cbet1;
    // Without normalization we have schi1 = somg1.
    cchi1 = _f1 * dn1 * comg1;
    Math::norm(ssig1, csig1);
    // Math::norm(somg1, comg1); -- don't need to normalize!
    // Math::norm(schi1, cchi1); -- don't need to normalize!

    // Enforce symmetries in the case abs(bet2) = -bet1.  Need to be careful
    // about this case, since this can yield singularities in the Newton
    // iteration.
    // sin(alp2) * cos(bet2) = sin(alp0)
    salp2 = cbet2 != cbet1 ? salp0 / cbet2 : salp1;
    // calp2 = sqrt(1 - sq(salp2))
    //       = sqrt(sq(calp0) - sq(sbet2)) / cbet2
    // and subst for calp0 and rearrange to give (choose positive sqrt
    // to give alp2 in [0, pi/2]).
    calp2 = cbet2 != cbet1 || fabs(sbet2) != -sbet1 ?
      sqrt(Math::sq(calp1 * cbet1) +
           (cbet1 < -sbet1 ?
            (cbet2 - cbet1) * (cbet1 + cbet2) :
            (sbet1 - sbet2) * (sbet1 + sbet2))) / cbet2 :
      fabs(calp1);
    // tan(bet2) = tan(sig2) * cos(alp2)
    // tan(omg2) = sin(alp0) * tan(sig2).
    ssig2 = sbet2; somg2 = salp0 * sbet2;
    csig2 = comg2 = calp2 * cbet2;
    // Without normalization we have schi2 = somg2.
    cchi2 = _f1 * dn2 * comg2;
    Math::norm(ssig2, csig2);
    // Math::norm(somg2, comg2); -- don't need to normalize!
    // Math::norm(schi2, cchi2); -- don't need to normalize!

    // sig12 = sig2 - sig1, limit to [0, pi]
    sig12 = atan2(fmax(real(0), csig1 * ssig2 - ssig1 * csig2),
                                csig1 * csig2 + ssig1 * ssig2);

    // omg12 = omg2 - omg1, limit to [0, pi]
    somg12 = fmax(real(0), comg1 * somg2 - somg1 * comg2);
    comg12 =               comg1 * comg2 + somg1 * somg2;
    real k2 = Math::sq(calp0) * _ep2;
    E.Reset(-k2, -_ep2, 1 + k2, 1 + _ep2);
    // chi12 = chi2 - chi1, limit to [0, pi]
    real
      schi12 = fmax(real(0), cchi1 * somg2 - somg1 * cchi2),
      cchi12 =               cchi1 * cchi2 + somg1 * somg2;
    // eta = chi12 - lam120
    real eta = atan2(schi12 * clam120 - cchi12 * slam120,
                     cchi12 * clam120 + schi12 * slam120);
    real deta12 = -_e2/_f1 * salp0 * E.H() / (Math::pi() / 2) *
      (sig12 + (E.deltaH(ssig2, csig2, dn2) - E.deltaH(ssig1, csig1, dn1)));
    lam12 = eta + deta12;
    // domg12 = deta12 + chi12 - omg12
    domg12 = deta12 + atan2(schi12 * comg12 - cchi12 * somg12,
                            cchi12 * comg12 + schi12 * somg12);
    if (diffp) {
      if (calp2 == 0)
        dlam12 = - 2 * _f1 * dn1 / sbet1;
      else {
        real dummy;
        Lengths(E, sig12, ssig1, csig1, dn1, ssig2, csig2, dn2,
                cbet1, cbet2, REDUCEDLENGTH,
                dummy, dlam12, dummy, dummy, dummy);
        dlam12 *= _f1 / (calp2 * cbet2);
      }
    }

    return lam12;
  }

  void GeodesicExact::C4f(real eps, real c[]) const {
    // Evaluate C4 coeffs
    // Elements c[0] thru c[nC4_ - 1] are set
    real mult = 1;
    int o = 0;
    for (int l = 0; l < nC4_; ++l) { // l is index of C4[l]
      int m = nC4_ - l - 1;          // order of polynomial in eps
      c[l] = mult * Math::polyval(m, _cC4x + o, eps);
      o += m + 1;
      mult *= eps;
    }
    // Post condition: o == nC4x_
    if  (!(o == nC4x_))
      throw GeographicErr("C4 misalignment");
  }

  // If the coefficient is greater or equal to 2^63, express it as a pair [a,
  // b] which is combined with a*2^52 + b.  The largest coefficient is
  // 831281402884796906843926125 = 0x2af9eaf25d149c52a73ee6d
  // = 184581550685 * 2^52 + 0x149c52a73ee6d which is less than 2^90.  Both a
  // and b are less that 2^52 and so are exactly representable by doubles; then
  // the computation of the full double coefficient involves only a single
  // rounding operation.  (Actually integers up to and including 2^53 can be
  // represented exactly as doubles.  Limiting b to 52 bits allows it to be
  // represented in 13 digits in hex.)

  // If the coefficient is less than 2^63, cast it to real if it isn't exactly
  // representable as a float.  Thus 121722048 = 1901907*2^6 and 1901907 < 2^24
  // so the cast is not needed; 21708121824 = 678378807*2^5 and 678378807 >=
  // 2^24 so the cast is needed.

  void GeodesicExact::C4coeff() {
    // Generated by Maxima on 2017-05-27 10:17:57-04:00
#if GEOGRAPHICLIB_GEODESICEXACT_ORDER == 24
    static const real coeff[] = {
      // C4[0], coeff of eps^23, polynomial in n of order 0
      2113,real(34165005),
      // C4[0], coeff of eps^22, polynomial in n of order 1
      5189536,1279278,real(54629842995LL),
      // C4[0], coeff of eps^21, polynomial in n of order 2
      real(19420000),-9609488,7145551,real(87882790905LL),
      // C4[0], coeff of eps^20, polynomial in n of order 3
      real(223285780800LL),-real(146003016320LL),real(72167144896LL),
      real(17737080900LL),real(0x205dc0bcbd6d7LL),
      // C4[0], coeff of eps^19, polynomial in n of order 4
      real(0x4114538e4c0LL),-real(0x2f55bac3db0LL),real(0x1ee26e63c60LL),
      -real(0xf3f108c690LL),real(777582423783LL),real(0x19244124e56e27LL),
      // C4[0], coeff of eps^18, polynomial in n of order 5
      real(0x303f35e1bc93a0LL),-real(0x24e1f056b1d580LL),
      real(0x1ab9fe0d1d4d60LL),-real(0x1164c583e996c0LL),
      real(0x892da1e80cb20LL),real(0x2194519fdb596LL),
      reale(3071,0xfdd7cc41833d5LL),
      // C4[0], coeff of eps^17, polynomial in n of order 6
      real(0x4aad22c875ed20LL),-real(0x3a4801a1c6bad0LL),
      real(0x2c487fb318d4c0LL),-real(0x1ff24d7cfd75b0LL),
      real(0x14ba39245f1460LL),-real(0xa32e190328e90LL),
      real(0x78c93074dfcffLL),reale(3071,0xfdd7cc41833d5LL),
      // C4[0], coeff of eps^16, polynomial in n of order 7
      real(0x33d84b92096e100LL),-real(0x286d35d824ffe00LL),
      real(0x1f3d33e2e951300LL),-real(0x178f58435181400LL),
      real(0x10e7992a3756500LL),-real(0xaed7fa8609aa00LL),
      real(0x55d8ac87b09700LL),real(0x14e51e43945a10LL),
      reale(21503,0xf0e695ca96ad3LL),
      // C4[0], coeff of eps^15, polynomial in n of order 8
      real(0x577cdb6aaee0d80LL),-real(0x4283c1e96325470LL),
      real(0x32feef20b794020LL),-real(0x26ea2e388de1a50LL),
      real(0x1d13f6131e5d6c0LL),-real(0x14b9aa66e270230LL),
      real(0xd5657196ac0560LL),-real(0x6880b0118a9810LL),
      real(0x4d0f1755168ee7LL),reale(21503,0xf0e695ca96ad3LL),
      // C4[0], coeff of eps^14, polynomial in n of order 9
      real(0xa82410caed14920LL),-real(0x774e0539d2de300LL),
      real(0x57ddc01c62bc8e0LL),-real(0x41de50dfff43e40LL),
      real(0x31742450a1bdca0LL),-real(0x248524531975180LL),
      real(0x19d013c6e35ec60LL),-real(0x1084c003a0434c0LL),
      real(0x8103758ad86020LL),real(0x1f2409edf5e286LL),
      reale(21503,0xf0e695ca96ad3LL),
      // C4[0], coeff of eps^13, polynomial in n of order 10
      real(0x1c6d2d6120015ca0LL),-real(0x104cedef383403b0LL),
      real(0xab9dd58c3e3d880LL),-real(0x78a4e83e5604750LL),
      real(0x57aa7cf5406e460LL),-real(0x4067a93ceeb2cf0LL),
      real(0x2ed62190d975c40LL),-real(0x20c076adcb21890LL),
      real(0x14cfa9cb9e01c20LL),-real(0xa1e25734956e30LL),
      real(0x76afbfe4ae6c4dLL),reale(21503,0xf0e695ca96ad3LL),
      // C4[0], coeff of eps^12, polynomial in n of order 11
      real(0x500e39e18e75c40LL),-real(0xb866fe4aaa63680LL),
      real(0x4337db32e526ac0LL),-real(0x264cce8c21af200LL),
      real(0x18fb7ba247a4140LL),-real(0x115709558576d80LL),
      real(0xc5be96cd3dcfc0LL),-real(0x8cdca1395db900LL),
      real(0x611fe1a7e00640LL),-real(0x3d26e46827e480LL),
      real(0x1d93970a8fd4c0LL),real(0x70bf87cc17354LL),
      reale(3071,0xfdd7cc41833d5LL),
      // C4[0], coeff of eps^11, polynomial in n of order 12
      -real(0x158a522ca96a9f40LL),real(0x14d4e49882e048f0LL),
      real(0x51a6258bc6026a0LL),-real(0xc07af3677bdc6b0LL),
      real(0x45ac09bc3b66080LL),-real(0x275e4ef59a8b450LL),
      real(0x195f928e5402a60LL),-real(0x114aa7eeb31a3f0LL),
      real(0xbf706c784da040LL),-real(0x817ec7d97ab990LL),
      real(0x508b8ca80cde20LL),-real(0x26b120ea091930LL),
      real(0x1c1ab3faf18ecdLL),reale(3071,0xfdd7cc41833d5LL),
      // C4[0], coeff of eps^10, polynomial in n of order 13
      real(0x85cd94c7a43620LL),real(0x41534458719f180LL),
      -real(0x1688b497e3eabf20LL),real(0x15fa3ad6bcd8bd40LL),
      real(0x531c27984875fa0LL),-real(0xc9b33381ee39f00LL),
      real(0x485a2b8a7ad1a60LL),-real(0x286be979df41b40LL),
      real(0x199b6e19072f920LL),-real(0x10f769bc7a1af80LL),
      real(0xb2b30e0b2b83e0LL),-real(0x6d4c30bc0953c0LL),
      real(0x3405b9397b42a0LL),real(0xc1ffd0ada51beLL),
      reale(3071,0xfdd7cc41833d5LL),
      // C4[0], coeff of eps^9, polynomial in n of order 14
      real(0x77c3b2fb788360LL),real(0x12370e8b6ebba50LL),
      real(0x3ce89570a2d35c0LL),real(0x1ddd463aa5801f30LL),
      -reale(2652,0xb61760f09fe0LL),reale(2613,0x24df88b461210LL),
      real(0x24dea39341926e80LL),-real(0x5ce704fae2f44110LL),
      real(0x20ecef343dc3cce0LL),-real(0x121947a4ab4bae30LL),
      real(0xb2a76f84c78e740LL),-real(0x70dd3a5c9a20950LL),
      real(0x43604f2667d29a0LL),-real(0x1fa7f2abdd82670LL),
      real(0x169d55eb03244c1LL),reale(21503,0xf0e695ca96ad3LL),
      // C4[0], coeff of eps^8, polynomial in n of order 15
      real(0x21331eec152c80LL),real(0x3c94fa87392d00LL),
      real(0x7bff534019c580LL),real(0x12eee208e5fe200LL),
      real(0x3f965ae4945ee80LL),real(0x1f56cb06e4e85700LL),
      -reale(2802,0x46e8e19f880LL),reale(2796,0xadb20bd4ec00LL),
      real(0x251d0efe774e7080LL),-real(0x625b74d58e27ff00LL),
      real(0x224674d7e8ab8980LL),-real(0x1260f3bdc69c0a00LL),
      real(0xad7256a98d1b280LL),-real(0x63bd65ce944d500LL),
      real(0x2df89c0cd0d4b80LL),real(0xa46618fc50ff08LL),
      reale(21503,0xf0e695ca96ad3LL),
      // C4[0], coeff of eps^7, polynomial in n of order 16
      real(0xcb641c2517300LL),real(0x1435342f6c1790LL),
      real(0x2223c168d902a0LL),real(0x3e90a70fac72b0LL),
      real(0x80a310c4f84640LL),real(0x13bcb7c20d40bd0LL),
      real(0x42a5540b0e391e0LL),real(0x210e40977bd376f0LL),
      -reale(2980,0x94d9def1cc680LL),reale(3022,0x503caf61c4810LL),
      real(0x24d397da2b859120LL),-real(0x68d822cc2f04ecd0LL),
      real(0x23a043b28810ecc0LL),-real(0x125159fafe6e93b0LL),
      real(0x9e1bc8a31f5a060LL),-real(0x46aed7b45d01890LL),
      real(0x30c71f0f146542fLL),reale(21503,0xf0e695ca96ad3LL),
      // C4[0], coeff of eps^6, polynomial in n of order 17
      real(0x5c9c64c833ea0LL),real(0x87cba49bc6200LL),real(0xcee016a8ff560LL),
      real(0x14a860e941a1c0LL),real(0x231567934bf020LL),
      real(0x40a648fc642980LL),real(0x85b2123b2c36e0LL),
      real(0x14a4159e5b98140LL),real(0x462d226dee7d1a0LL),
      real(0x2316888f6f2f3100LL),-reale(3198,0x3491a799c37a0LL),
      reale(3311,0xbf8f265e6c0c0LL),real(0x2372de10575f2320LL),
      -real(0x70af5543c56e4780LL),real(0x24bbd6e6395ee9e0LL),
      -real(0x116009bab4325fc0LL),real(0x75b7dfa9c5a24a0LL),
      real(0x17de90e4beab49eLL),reale(21503,0xf0e695ca96ad3LL),
      // C4[0], coeff of eps^5, polynomial in n of order 18
      real(0x6a525328e6e0LL),real(0x93f17033fb30LL),real(0xd36a04706f00LL),
      real(0x137db4aaadad0LL),real(0x1de17febed720LL),real(0x300ece09a4c70LL),
      real(0x5230537724340LL),real(0x98911a7bab410LL),real(0x13df6f0042d760LL),
      real(0x317f809c6f75b0LL),real(0xa9d28ba9acb780LL),
      real(0x55d121ad9d8f550LL),-real(0x1efee1555125f860LL),
      real(0x21073529064696f0LL),real(0x486394f46ccebc0LL),
      -real(0x11777145e6374170LL),real(0x54159fc268987e0LL),
      -real(0x1fa4dd5835d2fd0LL),real(0x13d87fc86cca643LL),
      reale(3071,0xfdd7cc41833d5LL),
      // C4[0], coeff of eps^4, polynomial in n of order 19
      real(0x3804d31f10c0LL),real(0x4b2ec20ad280LL),real(0x66f0ea418040LL),
      real(0x903f2204b400LL),real(0xcfad72d447c0LL),real(0x134cb9fa41580LL),
      real(0x1dd70e331b740LL),real(0x306dd8a084700LL),real(0x53a0a0b201ec0LL),
      real(0x9cd7c33c89880LL),real(0x14a7b599a9ce40LL),
      real(0x340e256f2c5a00LL),real(0xb4e7d2cf7515c0LL),
      real(0x5cc8e678862db80LL),-real(0x22304c48df63bac0LL),
      real(0x25f7d3a888bb6d00LL),real(0x3210c8a6905acc0LL),
      -real(0x131873ea3222a180LL),real(0x4a33217f63b9c40LL),
      real(0xaa39109cb79b1cLL),reale(3071,0xfdd7cc41833d5LL),
      // C4[0], coeff of eps^3, polynomial in n of order 20
      real(0x1d8a60744340LL),real(0x26a12f47d0f0LL),real(0x3353c9ffe420LL),
      real(0x4570fd193850LL),real(0x5fe8194aa900LL),real(0x87a7057de1b0LL),
      real(0xc54ab4558de0LL),real(0x12897a64b8910LL),real(0x1d013b7f18ec0LL),
      real(0x2fb033b96ea70LL),real(0x5384f3e45a7a0LL),real(0x9f10eb531c1d0LL),
      real(0x154d17c994d480LL),real(0x36ab828088cb30LL),
      real(0xc1d47f99841160LL),real(0x65b5717bb21c290LL),
      -real(0x269fd1ef6edfa5c0LL),real(0x2dc2d3f3f9f963f0LL),
      -real(0xf46c321c1b54e0LL),-real(0x14642b52c5fe94b0LL),
      real(0x6b46a122c3b5c05LL),reale(3071,0xfdd7cc41833d5LL),
      // C4[0], coeff of eps^2, polynomial in n of order 21
      real(0x65e46db33460LL),real(0x82b39a7b3380LL),real(0xa9e8c6cf36a0LL),
      real(0xe0317d0fa0c0LL),real(0x12cd0399df4e0LL),real(0x19b576ed17600LL),
      real(0x23ecb07d1c720LL),real(0x33785d3e48b40LL),real(0x4bedad56b0560LL),
      real(0x73f4d1eccb880LL),real(0xb8a5a1bdc07a0LL),real(0x1359aad161d5c0LL),
      real(0x22a518d96d25e0LL),real(0x43a50f3643bb00LL),
      real(0x95133a4d60b820LL),real(0x18b02de0f4e4040LL),
      real(0x5ac287501571660LL),real(0x31a5fa2db58d3d80LL),
      -reale(5087,0xbd2e8f8d6760LL),reale(6752,0x2ce8487308ac0LL),
      -reale(2184,0x86ffdb3446920LL),-real(0x199994ff919cd3b6LL),
      reale(21503,0xf0e695ca96ad3LL),
      // C4[0], coeff of eps^1, polynomial in n of order 22
      real(0xd0da1980ba0LL),real(0x10803fb20d70LL),real(0x151a70ced0c0LL),
      real(0x1b569dc61a10LL),real(0x23ecd2ce6de0LL),real(0x2ff80cba60b0LL),
      real(0x413672596700LL),real(0x5a7b8b75a550LL),real(0x8082f2984020LL),
      real(0xbb859b75abf0LL),real(0x11a6bf1637d40LL),real(0x1b9a143813890LL),
      real(0x2d2aacb8da260LL),real(0x4e2c5253a0f30LL),real(0x914a9e2ed3380LL),
      real(0x128a302f4ef3d0LL),real(0x2b2226f5e6b4a0LL),
      real(0x7a36190e0daa70LL),real(0x1e8d8643836a9c0LL),
      real(0x129e3dd12414f710LL),-reale(2184,0x86ffdb3446920LL),
      reale(3276,0xca7fc8ce69db0LL),-real(0x5999897e7da4e4fdLL),
      reale(7167,0xfaf78743878f1LL),
      // C4[0], coeff of eps^0, polynomial in n of order 23
      real(0x71a68037fdf14LL),real(0x81ebac5d53b48LL),real(0x957440e8ac5fcLL),
      real(0xad1ce56088670LL),real(0xca0c260c189e4LL),real(0xedd10e292f598LL),
      real(0x11a912af9e18ccLL),real(0x1534f4af92bec0LL),
      real(0x19c5b078ed00b4LL),real(0x1fc05a701dd7e8LL),
      real(0x27bd1031afaf9cLL),real(0x32a7dc61183710LL),
      real(0x41fc58560eb384LL),real(0x583759590a1238LL),
      real(0x79bd058a3bfa6cLL),real(0xaecdc650561f60LL),
      real(0x108312ea2251254LL),real(0x1abbd57b12fd488LL),
      real(0x2fbd21c97d5693cLL),real(0x634bf45b6b1a7b0LL),
      real(0x11110dffb6688d24LL),real(0x666653fe46734ed8LL),
      -reale(5734,0x625f9f69393f4LL),reale(14335,0xf5ef0e870f1e2LL),
      reale(21503,0xf0e695ca96ad3LL),
      // C4[1], coeff of eps^23, polynomial in n of order 0
      3401,real(512475075),
      // C4[1], coeff of eps^22, polynomial in n of order 1
      -5479232,3837834,real(163889528985LL),
      // C4[1], coeff of eps^21, polynomial in n of order 2
      -real(1286021216),real(571443856),real(142575393),real(0xef8343fb2e1LL),
      // C4[1], coeff of eps^20, polynomial in n of order 3
      -real(237999188352LL),real(138477414656LL),-real(77042430080LL),
      real(53211242700LL),real(0x6119423638485LL),
      // C4[1], coeff of eps^19, polynomial in n of order 4
      -real(0x2066cb6031fc0LL),real(0x14c85e7394470LL),-real(0xf6b8f35571e0LL),
      real(0x6ad3f08040d0LL),real(0x1aa3b2832565LL),real(0x230f8ed873f29c63LL),
      // C4[1], coeff of eps^18, polynomial in n of order 5
      -real(0x33e9644cad5b40LL),real(0x22b6849ca6a500LL),
      -real(0x1ce364ad2a4ec0LL),real(0x104aaed8cf4680LL),
      -real(0x949f0f8a89e40LL),real(0x64bcf4df920c2LL),
      reale(9215,0xf98764c489b7fLL),
      // C4[1], coeff of eps^17, polynomial in n of order 6
      -real(0x50a85b2e2e4060LL),real(0x36bb9aa442c6f0LL),
      -real(0x3029aafbbe0440LL),real(0x1dc29c0bd6ce90LL),
      -real(0x16a422844d9020LL),real(0x9763b8d8ca030LL),
      real(0x25b8d7edff7ebLL),reale(9215,0xf98764c489b7fLL),
      // C4[1], coeff of eps^16, polynomial in n of order 7
      -real(0x3822c174e5c7e00LL),real(0x25fbaf973d78c00LL),
      -real(0x222a860fbdb7a00LL),real(0x15dabd7a0984800LL),
      -real(0x129f00215535600LL),real(0xa0e9e0ae9b8400LL),
      -real(0x5ee97a6d2d5200LL),real(0x3eaf5acabd0e30LL),
      reale(64511,0xd2b3c15fc4079LL),
      // C4[1], coeff of eps^15, polynomial in n of order 8
      -real(0x5ec1dcd7666b480LL),real(0x3ed4935a3fd8cd0LL),
      -real(0x38014f5e5d79960LL),real(0x240af6a53256570LL),
      -real(0x2049d0fb0404a40LL),real(0x12efbc065d3f410LL),
      -real(0xee9d804d5d8320LL),real(0x5ed209adebbcb0LL),
      real(0x1798ea7fdd6773LL),reale(64511,0xd2b3c15fc4079LL),
      // C4[1], coeff of eps^14, polynomial in n of order 9
      -real(0x19f69929deb8bc0LL),real(0x1054723730b1600LL),
      -real(0xdce6aeb616e040LL),real(0x8c0069813d6480LL),
      -real(0x7e59f70027c8c0LL),real(0x4bea01551feb00LL),
      -real(0x42bb28790cad40LL),real(0x21dd61f97d4180LL),
      -real(0x14f93d4343f5c0LL),real(0xd58968a8df35eLL),
      reale(9215,0xf98764c489b7fLL),
      // C4[1], coeff of eps^13, polynomial in n of order 10
      -real(0x1ecd4a3794400de0LL),real(0x101df33ec1bb0110LL),
      -real(0xbc64ec7794b2980LL),real(0x71d5f4e2a637ff0LL),
      -real(0x625888ecafc7520LL),real(0x3aa6879742ff4d0LL),
      -real(0x3585f7f60d164c0LL),real(0x1d18174ef21abb0LL),
      -real(0x18117eb39416c60LL),real(0x8df7a42ab2f090LL),
      real(0x23413de9276581LL),reale(64511,0xd2b3c15fc4079LL),
      // C4[1], coeff of eps^12, polynomial in n of order 11
      -real(0x113775cb09582880LL),real(0x5790112bb17c4700LL),
      -real(0x204e01ed2b929d80LL),real(0x1063af9e8d99cc00LL),
      -real(0xc3ef805036ada80LL),real(0x701a56aa2d31100LL),
      -real(0x63910631abdcf80LL),real(0x368e0c562512600LL),
      -real(0x31ed34307286c80LL),real(0x170e89cb9dd1b00LL),
      -real(0xf5f0efdd07a180LL),real(0x93fb623bde75e4LL),
      reale(64511,0xd2b3c15fc4079LL),
      // C4[1], coeff of eps^11, polynomial in n of order 12
      real(0x13635f7860ae69c0LL),-real(0x169d904d9d4691d0LL),
      -real(0x2254277308cd9e0LL),real(0xd20446e8d8a9710LL),
      -real(0x4df2aedeefd1980LL),real(0x25e2aff2baec9f0LL),
      -real(0x1d3856fa2b08920LL),real(0xf7cadc640f92d0LL),
      -real(0xe3d2f6c9ad5cc0LL),real(0x6e412eaf297db0LL),
      -real(0x62000ef613c860LL),real(0x201266fb021690LL),
      real(0x7ee4c480c21e1LL),reale(9215,0xf98764c489b7fLL),
      // C4[1], coeff of eps^10, polynomial in n of order 13
      -real(0x5fe482817c4c40LL),-real(0x3373730b4b79d00LL),
      real(0x140f919171472640LL),-real(0x17f10e5417ef9980LL),
      -real(0x1b454cf244cf340LL),real(0xdd42319af5c0200LL),
      -real(0x530205145e450c0LL),real(0x25eec00584a7d80LL),
      -real(0x1e9e562555aaa40LL),real(0xe85806d73b2100LL),
      -real(0xde44387c5bb7c0LL),real(0x581f06023d3480LL),
      -real(0x421ccd71c33140LL),real(0x245ff7208ef53aLL),
      reale(9215,0xf98764c489b7fLL),
      // C4[1], coeff of eps^9, polynomial in n of order 14
      -real(0x47f3709eaa4320LL),-real(0xbb640bc2e1ae70LL),
      -real(0x2a7854a3ead7b40LL),-real(0x1701de8d91314210LL),
      reale(2329,0x5f8472b9624a0LL),-reale(2855,0xe7c1182872fb0LL),
      -real(0x785bf95be998780LL),real(0x66690260b30024b0LL),
      -real(0x272595745774a3a0LL),real(0x104f772bee315710LL),
      -real(0xe11ad02f34b53c0LL),real(0x5a192e055800370LL),
      -real(0x58d8bfb781fbbe0LL),real(0x17a156426e4c5d0LL),
      real(0x5c88907e67c575LL),reale(64511,0xd2b3c15fc4079LL),
      // C4[1], coeff of eps^8, polynomial in n of order 15
      -real(0x1138d3e7324700LL),-real(0x210a1008a4f200LL),
      -real(0x47b7d2285e8500LL),-real(0xbbe3dba17a1400LL),
      -real(0x2aeb63e9e4cb300LL),-real(0x1781d8a9c80b7600LL),
      reale(2419,0xe4212c9be8f00LL),-reale(3063,0xd7c230ad9b800LL),
      -real(0x116171a56015f00LL),real(0x6cc31b4079da8600LL),
      -real(0x2af22cc657d11d00LL),real(0xf75e4ec12d0a400LL),
      -real(0xeb60cc0dd754b00LL),real(0x472a49a74880200LL),
      -real(0x4174f343c328900LL),real(0x1ed324af4f2fd18LL),
      reale(64511,0xd2b3c15fc4079LL),
      // C4[1], coeff of eps^7, polynomial in n of order 16
      -real(0xd56426d4f700LL),-real(0x15fa65017d450LL),
      -real(0x26ba18ad11e20LL),-real(0x4a9605f1a58f0LL),
      -real(0xa2b494aee2940LL),-real(0x1ad07f38fd2390LL),
      -real(0x62deb836d71c60LL),-real(0x36d68c47bf27830LL),
      real(0x167d3fa4abc50480LL),-real(0x1d9b2fd161b99ad0LL),
      real(0x13a59aea9293560LL),real(0x10886ca52ccf3090LL),
      -real(0x6e8a4c27dbf8dc0LL),real(0x1f02cd8f1f8a5f0LL),
      -real(0x2216230a1ac48e0LL),real(0x5f13c815b08150LL),
      real(0x1666b06ca8f56dLL),reale(9215,0xf98764c489b7fLL),
      // C4[1], coeff of eps^6, polynomial in n of order 17
      -real(0x2678d0ed9f140LL),-real(0x39d0dbe263c00LL),
      -real(0x5aa623a5216c0LL),-real(0x95d2f30c44880LL),
      -real(0x108ea4db631840LL),-real(0x2005d27e0acd00LL),
      -real(0x463ad5e0e22dc0LL),-real(0xba80ab02c40180LL),
      -real(0x2b67c47d5d48f40LL),-real(0x186d6a49f7da1e00LL),
      reale(2625,0x9832921f08b40LL),-reale(3627,0xa72ee4675a80LL),
      real(0x17be252bac67e9c0LL),real(0x7a8f5366d9ba1100LL),
      -real(0x38a15d77b043abc0LL),real(0x9cd4e0bf35fec80LL),
      -real(0xceae5004f176d40LL),real(0x479bb2ae3c01ddaLL),
      reale(64511,0xd2b3c15fc4079LL),
      // C4[1], coeff of eps^5, polynomial in n of order 18
      -real(0x11dc9e54dea60LL),-real(0x193ec5647cdf0LL),
      -real(0x24bda460ceb00LL),-real(0x3760182d9a010LL),
      -real(0x5717ea0e54ba0LL),-real(0x907095ecddc30LL),
      -real(0x10063188dee040LL),-real(0x1f228e862f9650LL),
      -real(0x44adcde9a37ce0LL),-real(0xb7cbf8f2d0e270LL),
      -real(0x2b3f803c770f580LL),-real(0x18c05d008644d490LL),
      reale(2737,0x3ce4b1d74e1e0LL),-reale(4017,0xdf79eceb980b0LL),
      real(0x30ac41edd5123540LL),real(0x7e3ade121a8e0530LL),
      -real(0x45ec5d28a0fecf60LL),real(0x3577aaf625fa910LL),
      real(0x7292b77d2ccfc9LL),reale(64511,0xd2b3c15fc4079LL),
      // C4[1], coeff of eps^4, polynomial in n of order 19
      -real(0x14469ef39280LL),-real(0x1b74a6d65900LL),-real(0x25fc6724f380LL),
      -real(0x35e25bf6c800LL),-real(0x4eb76c6a3c80LL),-real(0x771a92ddb700LL),
      -real(0xbc1644489d80LL),-real(0x13946cde25600LL),
      -real(0x22eaf36054680LL),-real(0x44349dbbbd500LL),
      -real(0x976a625a56780LL),-real(0x1989ef99e16400LL),
      -real(0x6150e2c16e3080LL),-real(0x38c68feccea3300LL),
      real(0x1963a1a8e71b2e80LL),-real(0x2849f713f5ed7200LL),
      real(0xd30bac57bb18580LL),real(0x105e1a36741daf00LL),
      -real(0xc8c696e03b05b80LL),real(0x1feab31d626d154LL),
      reale(9215,0xf98764c489b7fLL),
      // C4[1], coeff of eps^3, polynomial in n of order 20
      -real(0xa4172dfa1c0LL),-real(0xd77fb109ed0LL),-real(0x11fc3eda7860LL),
      -real(0x1879b9235cf0LL),-real(0x2209eb95db00LL),-real(0x308bcfa5f110LL),
      -real(0x47510fa29da0LL),-real(0x6c88ffcf6f30LL),-real(0xac6dd3019440LL),
      -real(0x120fcca63eb50LL),-real(0x206b8121592e0LL),
      -real(0x3fc3a9ace7970LL),-real(0x8ea4f3b556d80LL),
      -real(0x18488ccc5b2d90LL),-real(0x5db9d9787df820LL),
      -real(0x37d6c7544511bb0LL),real(0x1a02f9f8abfbf940LL),
      -real(0x2d9fe91163ac57d0LL),real(0x18b01234447992a0LL),
      real(0x46ed1c414c80a10LL),-real(0x57c56c90ceabfa7LL),
      reale(9215,0xf98764c489b7fLL),
      // C4[1], coeff of eps^2, polynomial in n of order 21
      -real(0x2271f7278cc0LL),-real(0x2c3f5c6ec900LL),-real(0x399dc5a18140LL),
      -real(0x4c2bebb96280LL),-real(0x6670101499c0LL),-real(0x8c75450f5400LL),
      -real(0xc4e9f8733e40LL),-real(0x11b3ff75a0580LL),
      -real(0x1a3e7cf3fd6c0LL),-real(0x2853a9e02df00LL),
      -real(0x40b8bca6ccb40LL),-real(0x6da2a9d234880LL),
      -real(0xc6fc7477c83c0LL),-real(0x18bdddb834aa00LL),
      -real(0x37ff6cf7616840LL),-real(0x9a5f4811c06b80LL),
      -real(0x25bde21729de0c0LL),-real(0x16ea24b2a28ff500LL),
      reale(2841,0x69c686bdbaac0LL),-reale(5560,0x9d73ff6dcae80LL),
      reale(4369,0xdffb6688d240LL),-real(0x4cccbefeb4d67b22LL),
      reale(64511,0xd2b3c15fc4079LL),
      // C4[1], coeff of eps^1, polynomial in n of order 22
      -real(0xd0da1980ba0LL),-real(0x10803fb20d70LL),-real(0x151a70ced0c0LL),
      -real(0x1b569dc61a10LL),-real(0x23ecd2ce6de0LL),-real(0x2ff80cba60b0LL),
      -real(0x413672596700LL),-real(0x5a7b8b75a550LL),-real(0x8082f2984020LL),
      -real(0xbb859b75abf0LL),-real(0x11a6bf1637d40LL),
      -real(0x1b9a143813890LL),-real(0x2d2aacb8da260LL),
      -real(0x4e2c5253a0f30LL),-real(0x914a9e2ed3380LL),
      -real(0x128a302f4ef3d0LL),-real(0x2b2226f5e6b4a0LL),
      -real(0x7a36190e0daa70LL),-real(0x1e8d8643836a9c0LL),
      -real(0x129e3dd12414f710LL),reale(2184,0x86ffdb3446920LL),
      -reale(3276,0xca7fc8ce69db0LL),real(0x5999897e7da4e4fdLL),
      reale(64511,0xd2b3c15fc4079LL),
      // C4[2], coeff of eps^23, polynomial in n of order 0
      10384,real(854125125),
      // C4[2], coeff of eps^22, polynomial in n of order 1
      real(61416608),15713412,real(0x35f1be97217LL),
      // C4[2], coeff of eps^21, polynomial in n of order 2
      real(1053643008),-real(709188480),real(436906360),real(0x18f301bf7f77LL),
      // C4[2], coeff of eps^20, polynomial in n of order 3
      real(0x45823cb069c0LL),-real(0x3dc56cd10180LL),real(0x15b4532d4340LL),
      real(0x5946b207ad8LL),real(0xf72bf6e15a9abe5LL),
      // C4[2], coeff of eps^19, polynomial in n of order 4
      real(0x1b1b08a8c6e00LL),-real(0x1a1dea5249180LL),real(0xc1b857255700LL),
      -real(0x8a94db95d080LL),real(0x5209b9749ec8LL),
      real(0x3a6f4368c13f04a5LL),
      // C4[2], coeff of eps^18, polynomial in n of order 5
      real(0x13c972f90d64d60LL),-real(0x12d8369dbbbb080LL),
      real(0xa013fa80d7c1a0LL),-real(0x95d1a2bb4de840LL),
      real(0x30a495fb9aa5e0LL),real(0xc95efc891d64cLL),
      reale(107519,0xb480ecf4f161fLL),
      // C4[2], coeff of eps^17, polynomial in n of order 6
      real(0x4b31e4eff4bc00LL),-real(0x4190c8b5d5de00LL),
      real(0x27770ac0842800LL),-real(0x270a0d33995200LL),
      real(0x10c9f01b859400LL),-real(0xd056352974600LL),
      real(0x74f9dc1f6f260LL),reale(15359,0xf536fd4790329LL),
      // C4[2], coeff of eps^16, polynomial in n of order 7
      real(0x39908ef33285d00LL),-real(0x2a7d467835cbe00LL),
      real(0x1e0505551ade700LL),-real(0x1bf3204cf26d400LL),
      real(0xe195527d96f100LL),-real(0xe0af5ccd52ea00LL),
      real(0x41681113e87b00LL),real(0x1112b429bab2a0LL),
      reale(107519,0xb480ecf4f161fLL),
      // C4[2], coeff of eps^15, polynomial in n of order 8
      real(0xf8fa0142055000LL),-real(0x8f8aa7832e8a00LL),
      real(0x7d6f3ddfb47c00LL),-real(0x62d1e182b7be00LL),
      real(0x3bb149eddea800LL),-real(0x3be3b3e26a7200LL),
      real(0x175d0d17dad400LL),-real(0x14371cfc4fa600LL),
      real(0xa8f8f5855a060LL),reale(15359,0xf536fd4790329LL),
      // C4[2], coeff of eps^14, polynomial in n of order 9
      real(0x21490cd145715e0LL),-real(0xe087822f191900LL),
      real(0xf91f2bb3d29820LL),-real(0x949428c90dc2c0LL),
      real(0x7371ad50b34a60LL),-real(0x63c52e9a850c80LL),
      real(0x301579a22c8ca0LL),-real(0x33552a69ca1640LL),
      real(0xcc2c8c733bee0LL),real(0x35f5f30acfbecLL),
      reale(15359,0xf536fd4790329LL),
      // C4[2], coeff of eps^13, polynomial in n of order 10
      real(0x29bb6acaa073ef00LL),-real(0xc930d526d728e80LL),
      real(0xf55c2b3103d0c00LL),-real(0x63b9281a5449980LL),
      real(0x6acdfd5dbb92900LL),-real(0x441c8fce3be0480LL),
      real(0x2be797a45cb8600LL),-real(0x2aec3395f438f80LL),
      real(0xec70ff5d376300LL),-real(0xedc27143c9fa80LL),
      real(0x7039bcd0124e68LL),reale(107519,0xb480ecf4f161fLL),
      // C4[2], coeff of eps^12, polynomial in n of order 11
      -real(0x17ce935fc610ad40LL),-real(0x5d5bbde81a902580LL),
      real(0x2dcc12fb45c89240LL),-real(0xc1c61e98a479e00LL),
      real(0x10183633a5ddf1c0LL),-real(0x672de318faa1680LL),
      real(0x64ee85310393140LL),-real(0x481cf983db0cf00LL),
      real(0x2299f24f52810c0LL),-real(0x271fc56086d0780LL),
      real(0x79dac155045040LL),real(0x20c44d35dada38LL),
      reale(107519,0xb480ecf4f161fLL),
      // C4[2], coeff of eps^11, polynomial in n of order 12
      -real(0x6b8bdbaa2666e600LL),reale(2706,0x6d4e4332c7e80LL),
      -real(0x201eb2939ffc7500LL),-real(0x605f6d97c740b880LL),
      real(0x32fb1ca66ccebc00LL),-real(0xb85f2dd585e0f80LL),
      real(0x10b7dbe9dec0ed00LL),-real(0x6e454f6a0fd4680LL),
      real(0x594f6f139205e00LL),-real(0x4c204810d601d80LL),
      real(0x16a875347934f00LL),-real(0x1be72589c185480LL),
      real(0xb5a396e2ccd788LL),reale(107519,0xb480ecf4f161fLL),
      // C4[2], coeff of eps^10, polynomial in n of order 13
      real(0x332d666e095e20LL),real(0x205e97ebfb32780LL),
      -real(0xf80bf36cd359f20LL),real(0x19615ff8d71e0640LL),
      -real(0x61aef235a414c60LL),-real(0xe1fda0393083b00LL),
      real(0x83e2ad192fc7660LL),-real(0x18ece140ef0fc40LL),
      real(0x26bbb213037c920LL),-real(0x11a4c9418dd9d80LL),
      real(0x9ec708de66cbe0LL),-real(0xaee5994e9b7ec0LL),
      real(0x1626e135e59ea0LL),real(0x610ef2b6b35c4LL),
      reale(15359,0xf536fd4790329LL),
      // C4[2], coeff of eps^9, polynomial in n of order 14
      real(0x1b709db1871200LL),real(0x51a2a024c26b00LL),
      real(0x157c554050bb400LL),real(0xddb41f944653d00LL),
      -real(0x6d182f563006aa00LL),reale(2991,0xf7eb0ae304f00LL),
      -real(0x387b65599c618800LL),-real(0x64242336a83ddf00LL),
      real(0x4282c6eaa3899a00LL),-real(0xa8fc3afb1e6cd00LL),
      real(0x1040dddbf0493c00LL),-real(0x9184bc07b2bfb00LL),
      real(0x281ea22622bde00LL),-real(0x3dc59bc648ee900LL),
      real(0x13fb78815b4ca90LL),reale(107519,0xb480ecf4f161fLL),
      // C4[2], coeff of eps^8, polynomial in n of order 15
      real(0xacc0646b5180LL),real(0x1753663f74b00LL),real(0x3994d0061e480LL),
      real(0xadc1fbdd72e00LL),real(0x2e87a44adab780LL),
      real(0x1eaeb3451821100LL),-real(0xf937e414930b580LL),
      real(0x1c27d8b21df37400LL),-real(0xaa5908f76fee280LL),
      -real(0xe1c8d327ee92900LL),real(0xb2675f22d49b080LL),
      -real(0x19e66cd66684600LL),real(0x1f3a47aa5ea8380LL),
      -real(0x18da246c74e6300LL),real(0x10dd3b80dd1680LL),
      real(0x3f21f272d2a30LL),reale(15359,0xf536fd4790329LL),
      // C4[2], coeff of eps^7, polynomial in n of order 16
      real(0x2957d7da1000LL),real(0x4c28ba8a3700LL),real(0x9714a6610e00LL),
      real(0x14a5ff52a4500LL),real(0x33af2f78d8c00LL),real(0x9e87298409300LL),
      real(0x2b4e15dbd10a00LL),real(0x1d4c6da210ea100LL),
      -real(0xf6c4a6847e2f800LL),real(0x1da98c51a6b5ef00LL),
      -real(0xe1270d810dcfa00LL),-real(0xd23a021f3080300LL),
      real(0xd3b280b26948400LL),-real(0x22fd890d309b500LL),
      real(0x119ef453c630200LL),-real(0x1959af9980da700LL),
      real(0x5959078fa70870LL),reale(15359,0xf536fd4790329LL),
      // C4[2], coeff of eps^6, polynomial in n of order 17
      real(0x511612baa2a0LL),real(0x87a79de92a00LL),real(0xee2dd20af160LL),
      real(0x1bbcfaf32f4c0LL),real(0x37ba524fb5020LL),real(0x7b9b8f2a45f80LL),
      real(0x13a76fcf6fdee0LL),real(0x3d717a0fbe0a40LL),
      real(0x112dc752f02bda0LL),real(0xbfa002cc4689500LL),
      -real(0x694405622017f3a0LL),reale(3484,0x979f3cbb89fc0LL),
      -reale(2088,0x4fe2045ae14e0LL),-real(0x49f87439584d3580LL),
      real(0x6c3e90c1455479e0LL),-real(0x1afff07538f04ac0LL),
      -real(0x1a0f4cdf3b62760LL),-real(0x112f9b85f9ebf7cLL),
      reale(107519,0xb480ecf4f161fLL),
      // C4[2], coeff of eps^5, polynomial in n of order 18
      real(0x181437e05500LL),real(0x25c7b1fe6a80LL),real(0x3d5ebd606800LL),
      real(0x67dd27f0e580LL),real(0xb8ac7d2a7b00LL),real(0x15ce71e5cc080LL),
      real(0x2c7c6a3654e00LL),real(0x6460c05d0bb80LL),real(0x1046637cd7a100LL),
      real(0x340d46956b9680LL),real(0xef5f1bde883400LL),
      real(0xacec6aed73c1180LL),-real(0x63ea680d7ea23900LL),
      reale(3605,0xecc3861a0ec80LL),-reale(2759,0xc804a6c40e600LL),
      -real(0x212a787bd0571880LL),real(0x70c6a0884332ed00LL),
      -real(0x31a5fa2db58d3d80LL),real(0x5033807138f7d98LL),
      reale(107519,0xb480ecf4f161fLL),
      // C4[2], coeff of eps^4, polynomial in n of order 19
      real(0x6f3f0983c40LL),real(0xa6cf9192980LL),real(0x100e50e166c0LL),
      real(0x197f658cec00LL),real(0x29f706a6f140LL),real(0x480b7a0eae80LL),
      real(0x821ecd9c1bc0LL),real(0xfa1d1da0b100LL),real(0x2081a78802640LL),
      real(0x4aefd4add3380LL),real(0xc730805b650c0LL),real(0x28f491e04e7600LL),
      real(0xc2d07512dddb40LL),real(0x92e539684c6b880LL),
      -real(0x5a2096cfc695fa40LL),reale(3598,0x9cd1e91b83b00LL),
      -reale(3553,0x1d49601c5efc0LL),real(0x31a5fa2db58d3d80LL),
      real(0x3760835a5e313ac0LL),-real(0x1bed5cb9b61f7298LL),
      reale(107519,0xb480ecf4f161fLL),
      // C4[2], coeff of eps^3, polynomial in n of order 20
      real(273006835200LL),real(395945493120LL),real(586817304320LL),
      real(891220401024LL),real(0x1440886f800LL),real(0x20a73015480LL),
      real(0x36a4a027900LL),real(0x5f8b4acad80LL),real(0xb01798c3a00LL),
      real(0x15a2eb8a6680LL),real(0x2e235b147b00LL),real(0x6d6a30f2bf80LL),
      real(0x12c54474b7c00LL),real(0x40129870df880LL),real(0x13e41ecc817d00LL),
      real(0xfcf67c8cf45180LL),-real(0xa65f288fe794200LL),
      real(0x1cea83a477ce0a80LL),-real(0x240239aaff748100LL),
      real(0x1547221396f36380LL),-real(0x4e04d247d427178LL),
      reale(15359,0xf536fd4790329LL),
      // C4[2], coeff of eps^2, polynomial in n of order 21
      real(317370445920LL),real(448806691200LL),real(646426411680LL),
      real(950282020800LL),real(0x14ccaecc4e0LL),real(0x201acdf4e00LL),
      real(0x33093819720LL),real(0x53ed06eb440LL),real(0x8f8eb441960LL),
      real(0x1013bf0bfa80LL),real(0x1e750d7baba0LL),real(0x3dc4346800c0LL),
      real(0x88729901ade0LL),real(0x150e863aba700LL),real(0x3c89c1e8d8020LL),
      real(0xd9efed463cd40LL),real(0x47e39644808260LL),
      real(0x3d1b0c8706d5380LL),-real(0x2af704cef0cdeb60LL),
      real(0x7c1ef17245e119c0LL),-reale(2184,0x86ffdb3446920LL),
      real(0x333329ff2339a76cLL),reale(107519,0xb480ecf4f161fLL),
      // C4[3], coeff of eps^23, polynomial in n of order 0
      70576,real(29211079275LL),
      // C4[3], coeff of eps^22, polynomial in n of order 1
      -real(31178752),real(16812224),real(0x192c8c2464fLL),
      // C4[3], coeff of eps^21, polynomial in n of order 2
      -real(135977211392LL),real(37023086848LL),real(9903771944LL),
      real(0xb98f5d0044051LL),
      // C4[3], coeff of eps^20, polynomial in n of order 3
      -real(0x30f8b0f5c00LL),real(0x12d79f66800LL),-real(0x115c7023400LL),
      real(606224480400LL),real(0xa7c6f527b4f7c7LL),
      // C4[3], coeff of eps^19, polynomial in n of order 4
      -real(0x3317d68847dc00LL),real(0x19fc69dd236700LL),
      -real(0x1c6d14df7ace00LL),real(0x6cfe4fac52d00LL),
      real(0x1d99f24357808LL),reale(30105,0x847604e86c8c1LL),
      // C4[3], coeff of eps^18, polynomial in n of order 5
      -real(0x15b0eba45ef8000LL),real(0xf79bdd24a10000LL),
      -real(0xf32a8559288000LL),real(0x563281b24a8000LL),
      -real(0x5920796c2f8000LL),real(0x29f7b73471c480LL),
      reale(150527,0x964e188a1ebc5LL),
      // C4[3], coeff of eps^17, polynomial in n of order 6
      -real(0x1c02d0336ef1800LL),real(0x1d91ba24525dc00LL),
      -real(0x163d203e4811000LL),real(0xb8e8b252aa8400LL),
      -real(0xd2485de6110800LL),real(0x2a40e341b4ac00LL),
      real(0xbb70f2cbcf360LL),reale(150527,0x964e188a1ebc5LL),
      // C4[3], coeff of eps^16, polynomial in n of order 7
      -real(0x58b4aa16ae3000LL),real(0x7fa0a14380e000LL),
      -real(0x429ab6e3829000LL),real(0x383428ed0d4000LL),
      -real(0x32e93ebd99f000LL),real(0x108fe88bbda000LL),
      -real(0x13ba86ffa65000LL),real(0x868b4ab8e3340LL),
      reale(21503,0xf0e695ca96ad3LL),
      // C4[3], coeff of eps^15, polynomial in n of order 8
      -real(0xaedfc7febee000LL),real(0xe403ca9386ec00LL),
      -real(0x5568aa53f7a800LL),real(0x76f3d9af940400LL),
      -real(0x475f28b7bb7000LL),real(0x29018461d69c00LL),
      -real(0x2ed89591f13800LL),real(0x74380445fb400LL),
      real(0x21274712bcba0LL),reale(21503,0xf0e695ca96ad3LL),
      // C4[3], coeff of eps^14, polynomial in n of order 9
      -real(0x231ca125e5c8000LL),real(753027184687LL<<17),
      -real(0x97f88531f38000LL),real(0xee839ade908000LL),
      -real(0x572a9cdd748000LL),real(0x65a05d4f5f0000LL),
      -real(0x4ce11756538000LL),real(0x177f524c958000LL),
      -real(0x20e57338048000LL),real(0xc4518e260f380LL),
      reale(21503,0xf0e695ca96ad3LL),
      // C4[3], coeff of eps^13, polynomial in n of order 10
      -real(0x44ebd4477ad4f200LL),real(0x9a6a6024b320f00LL),
      -real(0xe915ce102d6a800LL),real(0xb28d5273bcee100LL),
      -real(0x37fa968ec235e00LL),real(0x68974b850671300LL),
      -real(0x2a735b9bf505400LL),real(0x20513dd7a7f6500LL),
      -real(0x220360a9be2ca00LL),real(0x36d1c1a3f49700LL),
      real(0x10369a2227fd98LL),reale(150527,0x964e188a1ebc5LL),
      // C4[3], coeff of eps^12, polynomial in n of order 11
      real(0x52462bb828351400LL),real(0x4a4d1c14e6172800LL),
      -real(0x4ced32c430d22400LL),real(0xb52b1b0c2492000LL),
      -real(0xd058359466b1c00LL),real(0xd07709dd3bd1800LL),
      -real(0x30072e56aae5400LL),real(0x605c027d5629000LL),
      -real(0x32e58b8ebb44c00LL),real(0x108221f23a90800LL),
      -real(0x1a7ac7295958400LL),real(0x836be4086f28d0LL),
      reale(150527,0x964e188a1ebc5LL),
      // C4[3], coeff of eps^11, polynomial in n of order 12
      real(0x48f7bc8748dd3400LL),-reale(2561,0x7f9f9673a4700LL),
      real(0x601d0ed1c7f2b600LL),real(0x449204e4f86d4300LL),
      -real(0x56194f80f81a8800LL),real(0xea108cfa6f6ed00LL),
      -real(0xa7ad46bd016c600LL),real(0xef32c344e507700LL),
      -real(0x30a1762ff0e4400LL),real(0x4a78ea25c4fa100LL),
      -real(0x3c3cca9d1bd4200LL),real(0x22cbd76a022b00LL),
      real(0x9df3abb037278LL),reale(150527,0x964e188a1ebc5LL),
      // C4[3], coeff of eps^10, polynomial in n of order 13
      -real(0x9607df2a17c000LL),-real(0x739371b7f3d8000LL),
      real(0x4688c366039fc000LL),-reale(2611,0x8a66cbfc04000LL),
      real(0x7056fbc7b1c24000LL),real(0x3af7506941670000LL),
      -real(0x601cadbaecf24000LL),real(0x14affbea17164000LL),
      -real(0x6daccbfd0bfc000LL),real(0x1036680bb42b8000LL),
      -real(0x42f04a7d6e84000LL),real(0x246d9b6ab84c000LL),
      -real(0x37cce3b53adc000LL),real(0xd43660c7def0c0LL),
      reale(150527,0x964e188a1ebc5LL),
      // C4[3], coeff of eps^9, polynomial in n of order 14
      -real(0x115a7e31ff400LL),-real(0x3c90c47c29600LL),
      -real(0x1311ab10640800LL),-real(0xf2246746703a00LL),
      real(0x99b5e8c5c68e400LL),-real(0x179a6d9c8ead9e00LL),
      real(0x12bd250608495000LL),real(0x63777cc9563be00LL),
      -real(0xf1ef7972c204400LL),real(0x47367775d725a00LL),
      -real(0x63378c7bb15800LL),real(0x22d63078c5cb600LL),
      -real(0xf8707c83e76c00LL),-real(0xb0e06786eae00LL),
      -real(0x5e4438ea922f0LL),reale(21503,0xf0e695ca96ad3LL),
      // C4[3], coeff of eps^8, polynomial in n of order 15
      -real(0x1fe011d85800LL),-real(0x4f422fb05000LL),-real(0xe40060fc8800LL),
      -real(0x32e664e9c2000LL),-real(0x1078ec0ef63800LL),
      -real(0xd864902b71f000LL),real(0x8fab71292d19800LL),
      -real(0x179bbec0170ac000LL),real(0x15c925f1e4f1e800LL),
      real(0x2c36e0d96c07000LL),-real(0x100d07856dfe4800LL),
      real(0x6d9c3efea16a000LL),-real(0x13ac4a3567f800LL),
      real(0x15b22a4de1ed000LL),-real(0x1452d18e2b42800LL),
      real(0x32eab893d697a0LL),reale(21503,0xf0e695ca96ad3LL),
      // C4[3], coeff of eps^7, polynomial in n of order 16
      -real(0x5003ad66000LL),-real(0xa79ae296200LL),-real(0x17d9e9f5d400LL),
      -real(0x3c8762ad2600LL),-real(0xb232a56ac800LL),-real(0x28dbf6ee52a00LL),
      -real(0xda6199e36bc00LL),-real(0xba74c6aa46ee00LL),
      real(0x825959cb764d000LL),-real(0x17232e4c4e57f200LL),
      real(0x190bf0598fc65c00LL),-real(0x27c51cb844db600LL),
      -real(0xf8735fc98339800LL),real(0xa28217eef524600LL),
      -real(0xfc87c9cb4a8c00LL),-real(0x3228ffc0ed7e00LL),
      -real(0x387bf611406670LL),reale(21503,0xf0e695ca96ad3LL),
      // C4[3], coeff of eps^6, polynomial in n of order 17
      -real(0x62d694dc000LL),-real(97716157LL<<17),-real(0x173b38f24000LL),
      -real(0x319b0ca1c000LL),-real(0x7361a893c000LL),-real(0x12be5bef38000LL),
      -real(0x38b3402cc4000LL),-real(0xd6a4403694000LL),
      -real(0x4a69cc1535c000LL),-real(0x42816c266fd0000LL),
      real(0x315cb6a39d95c000LL),-reale(2449,0xcf91c36a8c000LL),
      reale(3143,0x2391393fc4000LL),-real(0x466890d45f668000LL),
      -real(0x50368754849c4000LL),real(0x594b313771cfc000LL),
      -real(0x1cc16f4e99cdc000LL),real(0x1e8d8643836a9c0LL),
      reale(150527,0x964e188a1ebc5LL),
      // C4[3], coeff of eps^5, polynomial in n of order 18
      -real(0x1136c8f5600LL),-real(0x1e3b013df00LL),-real(0x37550c23000LL),
      -real(0x6a508e10100LL),-real(0xd872daf0a00LL),-real(0x1d8dd6618300LL),
      -real(0x468422b6a400LL),-real(0xbc9d06f02500LL),-real(0x24d784d09be00LL),
      -real(0x90d122dffa700LL),-real(0x347ca809f91800LL),
      -real(0x31861ec3b2ac900LL),real(0x276d051382ba8e00LL),
      -reale(2163,0x55347fa444b00LL),reale(3319,0x8d7da907400LL),
      -reale(2191,0xdbae56666ed00LL),-real(0x47e396448082600LL),
      real(0x3577aaf625fa9100LL),-real(0x1449fb28d544cb98LL),
      reale(150527,0x964e188a1ebc5LL),
      // C4[3], coeff of eps^4, polynomial in n of order 19
      -real(58538142720LL),-real(97662466048LL),-real(168340530176LL),
      -real(301206585344LL),-real(562729180160LL),-real(0x1017e988800LL),
      -real(0x21987b95400LL),-real(0x4b78a99d000LL),-real(0xb9ccd9f8c00LL),
      -real(0x202de3701800LL),-real(0x68b6655d0400LL),-real(0x1af3df037e000LL),
      -real(0xa515b5f563c00LL),-real(0xa65924698da800LL),
      real(0x8fc72c890104c00LL),-real(0x226e597c6e0df000LL),
      real(0x3ee7237bf0721400LL),-real(0x3d1b0c8706d53800LL),
      real(0x1e8d8643836a9c00LL),-real(0x634bf45b6b1a7b0LL),
      reale(50175,0xdcc4b2d8b4e97LL),
      // C4[3], coeff of eps^3, polynomial in n of order 20
      -real(16545868800LL),-real(26558972160LL),-real(43799006720LL),
      -real(74458311424LL),-real(131016159232LL),-real(239806362880LL),
      -real(459418505728LL),-real(928488660736LL),-real(0x1d19ea9f400LL),
      -real(0x43b761f2900LL),-real(0xad7cf6b5600LL),-real(0x1f71d9841300LL),
      -real(0x6bcf7c0df800LL),-real(0x1d7abbebd1d00LL),
      -real(0xc1b8d2e919a00LL),-real(0xd3e226aef40700LL),
      real(0xc94a0b2634a0400LL),-real(0x3577aaf625fa9100LL),
      real(0x6aef55ec4bf52200LL),-real(0x634bf45b6b1a7b00LL),
      real(0x22221bff6cd11a48LL),reale(150527,0x964e188a1ebc5LL),
      // C4[4], coeff of eps^23, polynomial in n of order 0
      567424,real(87633237825LL),
      // C4[4], coeff of eps^22, polynomial in n of order 1
      real(2135226368),real(598833664),real(0x1358168b64fd9LL),
      // C4[4], coeff of eps^21, polynomial in n of order 2
      real(23101878272LL),-real(26986989568LL),real(11760203136LL),
      real(0x4f869592664b5LL),
      // C4[4], coeff of eps^20, polynomial in n of order 3
      real(0xa4d4b674a00LL),-real(0xbdc38ed8400LL),real(0x20274dfee00LL),
      real(635330794560LL),real(0x436914c918b5d6dLL),
      // C4[4], coeff of eps^19, polynomial in n of order 4
      real(0x481bf9079c000LL),-real(0x3c015f7917000LL),real(0x133447522e000LL),
      -real(0x195b19983d000LL),real(0xa0f15f7a8700LL),
      reale(3518,0xd3a367a37a66dLL),
      // C4[4], coeff of eps^18, polynomial in n of order 5
      real(0x1e9f26efa689000LL),-real(0x100c94382c2c000LL),
      real(0xabead3c2e1f000LL),-real(0xc04c79a6f96000LL),
      real(0x18fb8548735000LL),real(0x76d40a3ef6c00LL),
      reale(193535,0x781b441f4c16bLL),
      // C4[4], coeff of eps^17, polynomial in n of order 6
      real(0x780536a0606000LL),-real(0x28779739e97000LL),
      real(0x3a9fdf130c4000LL),-real(0x2860390cb81000LL),
      real(0xcce73d3902000LL),-real(0x1322aa5844b000LL),
      real(0x6bd0a3ad69900LL),reale(27647,0xec962e4d9d27dLL),
      // C4[4], coeff of eps^16, polynomial in n of order 7
      real(0x45af61c2ad1f800LL),-real(0x1b140a5252fd000LL),
      real(0x348e789bd7f6800LL),-real(0x137ac7aed3be000LL),
      real(0x11da35dc2ded800LL),-real(0x12097ef153ff000LL),
      real(0x186b19645c4800LL),real(0x7935fe20ccb00LL),
      reale(193535,0x781b441f4c16bLL),
      // C4[4], coeff of eps^15, polynomial in n of order 8
      real(0x788485be348000LL),-real(0xbf417480965000LL),
      real(0xbdad05e3bd6000LL),-real(0x306dcc448df000LL),
      real(0x6c08266aea4000LL),-real(0x364dbd52879000LL),
      real(0x13468d692f2000LL),-real(0x1f6575294f3000LL),
      real(0x97982d7211100LL),reale(27647,0xec962e4d9d27dLL),
      // C4[4], coeff of eps^14, polynomial in n of order 9
      real(0x99754be5293000LL),-real(0x273b2ae73028000LL),
      real(0xa610233e31d000LL),-real(0x8ee7336f99e000LL),
      real(0xd7a1a110827000LL),-real(0x2f0d74b9c14000LL),
      real(0x4f375451ab1000LL),-real(0x4002b6db48a000LL),
      real(0x20d804cbbb000LL),real(0xa41d3b221400LL),
      reale(27647,0xec962e4d9d27dLL),
      // C4[4], coeff of eps^13, polynomial in n of order 10
      real(0x6016f6408271a000LL),-real(0x1e7546e7a0d1b000LL),
      real(0x18e4e98f72c8000LL),-real(0x113f96068e695000LL),
      real(0x6af41cd57176000LL),-real(0x2590480c1d6f000LL),
      real(0x61253410a664000LL),-real(0x1c92661c6269000LL),
      real(0xfa686d5b4d2000LL),-real(0x188238347643000LL),
      real(0x60544135abb900LL),reale(193535,0x781b441f4c16bLL),
      // C4[4], coeff of eps^12, polynomial in n of order 11
      -reale(2096,0xf9dac0e4d8600LL),-real(0xa96847f4d191400LL),
      real(0x644f115411ee9e00LL),-real(0x2912ee32dfa61000LL),
      -real(0x81eeabcb01be00LL),-real(0xfba8345c9670c00LL),
      real(0x9bbda8340726600LL),-real(0x11537009b3f0800LL),
      real(0x51c2ea8aa8c0a00LL),-real(0x2bb89caf7310400LL),
      -real(0x162bd9b163d200LL),-real(0xac0895744a3c0LL),
      reale(193535,0x781b441f4c16bLL),
      // C4[4], coeff of eps^11, polynomial in n of order 12
      -real(0x296aa6e320b86000LL),real(0x7d9f9f72af514800LL),
      -reale(2284,0xfefdd7e855000LL),real(0x8d22edc50949800LL),
      real(0x6581767b41ffc000LL),-real(0x371ad32683bb1800LL),
      -real(0x915b5d6cd33000LL),-real(0xbce7db3a027c800LL),
      real(0xd0ebaf65b57e000LL),-real(0x1274db255bb7800LL),
      real(0x2970a5137d6f000LL),-real(0x30b8535f9002800LL),
      real(0x8fa21d365c3780LL),reale(193535,0x781b441f4c16bLL),
      // C4[4], coeff of eps^10, polynomial in n of order 13
      real(0x73aaee373e800LL),real(0x6d942f05126000LL),
      -real(0x55d059f7fa72800LL),real(0x114ee97e0f335000LL),
      -real(0x16053fa9ce763800LL),real(0x4d23952dbcc4000LL),
      real(0xdda0de6f17eb800LL),-real(0xa56bf33e63ad000LL),
      real(0x90dadc83efa800LL),-real(0xbf52dd8df9e000LL),
      real(0x2172ab2d7549800LL),-real(0x85ae20f708f000LL),
      -real(0x10c904999a7800LL),-real(0xae78582fbfa00LL),
      reale(27647,0xec962e4d9d27dLL),
      // C4[4], coeff of eps^9, polynomial in n of order 14
      real(0x19fde85a2f000LL),real(0x6b4aa2bef4800LL),real(0x28c46a7eab6000LL),
      real(0x2827ed076a87800LL),-real(0x210a7394d5283000LL),
      real(0x72396f4bbfb2a800LL),-reale(2620,0x4dc0771ddc000LL),
      real(0x40dce91ee367d800LL),real(0x52592d2deb84b000LL),
      -real(0x5a9bf1fdd05df800LL),real(0x10e48562d1f92000LL),
      real(0x1d4b91258bb3800LL),real(0xaa81c5529799000LL),
      -real(0x6eadf18b1729800LL),real(0xd0db43634fa080LL),
      reale(193535,0x781b441f4c16bLL),
      // C4[4], coeff of eps^8, polynomial in n of order 15
      real(0x45bda664400LL),real(0xc8c97088800LL),real(0x2a5a46b84c00LL),
      real(0xb467fe915000LL),real(0x471c8a3c15400LL),real(0x49361b74ae1800LL),
      -real(0x3fb304ab7e4a400LL),real(0xedcc81cc3d0e000LL),
      -real(0x1834aac92fbf9c00LL),real(0xe864613c6aba800LL),
      real(0x759492ec34a6c00LL),-real(0xea1e49c1b0f9000LL),
      real(0x5db63d617b37400LL),real(0x31083890113800LL),
      -real(0xa60c227ea8400LL),-real(0x3b3da9a3dab180LL),
      reale(27647,0xec962e4d9d27dLL),
      // C4[4], coeff of eps^7, polynomial in n of order 16
      real(469241266176LL),real(0x10545cac800LL),real(0x2adf04bd000LL),
      real(0x7eec6985800LL),real(0x1ba16d402000LL),real(0x7a072d7ae800LL),
      real(0x322ca20e07000LL),real(0x3657aa17207800LL),
      -real(0x3263434d5c54000LL),real(0xcd0703e8db70800LL),
      -real(0x17ea571d4aa2f000LL),real(0x141161dbf7ec9800LL),
      -real(0x57d62fedaaa000LL),-real(0xce7cd449810d800LL),
      real(0x99132fccc31b000LL),-real(0x27598ad75934800LL),
      real(0x18a5cd1eccf980LL),reale(27647,0xec962e4d9d27dLL),
      // C4[4], coeff of eps^6, polynomial in n of order 17
      real(341540329472LL),real(727668064256LL),real(0x180da872800LL),
      real(0x3b0b3acd000LL),real(0x9f94c3e7800LL),real(0x1e8177ec2000LL),
      real(0x6e3ee471c800LL),real(0x1fbe99a5b7000LL),real(0xdb641b5c91800LL),
      real(0xfc08a38932c000LL),-real(0xfb6a7929bd39800LL),
      real(0x466e762d282a1000LL),-reale(2430,0x8d7c552bc4800LL),
      reale(2721,0xe81cb8f96000LL),-real(0x4dc0eea70f08f800LL),
      -real(0x1b9eda123c275000LL),real(0x2eba54dfb9ee5800LL),
      -real(0xf46c321c1b54e00LL),reale(193535,0x781b441f4c16bLL),
      // C4[4], coeff of eps^5, polynomial in n of order 18
      real(31160807424LL),real(61322082304LL),real(3864763LL<<15),
      real(276675840000LL),real(646157094912LL),real(0x17cd936d800LL),
      real(0x429614e2000LL),real(0xd3b41886800LL),real(0x31f7c0917000LL),
      real(0xf21fb6ecf800LL),real(0x6ee892beec000LL),real(0x889688d5b28800LL),
      -real(0x944ac482b6bf000LL),real(0x2e4469f00aa71800LL),
      -real(0x73c7760d5050a000LL),reale(2642,0x7d1cf3a18a800LL),
      -reale(2185,0x6d0b55a915000LL),real(0x3d1b0c8706d53800LL),
      -real(0xb7512595147fa80LL),reale(193535,0x781b441f4c16bLL),
      // C4[4], coeff of eps^4, polynomial in n of order 19
      real(1806732800),real(3354817536LL),real(6474635776LL),
      real(13058088960LL),real(27705484800LL),real(62364503040LL),
      real(150565728768LL),real(395569133568LL),real(0x10ca075be00LL),
      real(0x37f6c332400LL),real(0xdf0e61c4a00LL),real(0x47dfa8095000LL),
      real(0x236014b495600LL),real(0x2f60ae04237c00LL),
      -real(0x38c125ca4a81e00LL),real(0x13dd33a066e0a800LL),
      -real(0x389cd322becd1200LL),real(0x5ba892ca8a3fd400LL),
      -real(0x4c61cfa8c88a8600LL),real(0x18d2fd16dac69ec0LL),
      reale(193535,0x781b441f4c16bLL),
      // C4[5], coeff of eps^23, polynomial in n of order 0
      14777984,real(0xd190230980fLL),
      // C4[5], coeff of eps^22, polynomial in n of order 1
      -real(104833024),real(39440128),real(0x62c2748ec71LL),
      // C4[5], coeff of eps^21, polynomial in n of order 2
      -real(45133008896LL),real(5079242752LL),real(1557031040),
      real(0x4f869592664b5LL),
      // C4[5], coeff of eps^20, polynomial in n of order 3
      -real(0xecd417f0000LL),real(40869997LL<<17),-real(0x78cb3050000LL),
      real(0x28d58610800LL),real(0x5263fcf5c8de3f7LL),
      // C4[5], coeff of eps^19, polynomial in n of order 4
      -real(0xf4977948ac000LL),real(0xfebd5b2ac3000LL),
      -real(0xf90c852576000LL),real(0x1257a8b1e1000LL),real(0x5e1a6b95fb00LL),
      reale(21503,0xf0e695ca96ad3LL),
      // C4[5], coeff of eps^18, polynomial in n of order 5
      -real(0x25dd48c154000LL),real(0x596953f850000LL),
      -real(0x2b40cdd44c000LL),real(8741106765LL<<15),-real(0x1ab27f0a04000LL),
      real(0x7e701f145600LL),reale(3071,0xfdd7cc41833d5LL),
      // C4[5], coeff of eps^17, polynomial in n of order 6
      -real(0x4776cd8c606000LL),real(0x6d8a47bfe9f000LL),
      -real(0x187da0ea944000LL),real(0x2b758d37739000LL),
      -real(0x22fd5e6d302000LL),real(0x107133def3000LL),real(0x56ef801cd100LL),
      reale(33791,0xe845c6d0a3a27LL),
      // C4[5], coeff of eps^16, polynomial in n of order 7
      -real(0x6b41dfbb0208000LL),real(0x3281e67a9bd0000LL),
      -real(0x11e76a3ab618000LL),real(0x2fa8791e0ae0000LL),
      -real(0xef00faafea8000LL),real(0x82642584ff0000LL),
      -real(0xce6c8b206b8000LL),real(0x33a2c6e1f0cc00LL),
      reale(236543,0x59e86fb479711LL),
      // C4[5], coeff of eps^15, polynomial in n of order 8
      -real(0xd8a9f7e5e7f8000LL),real(0x75ff062faeb000LL),
      -real(0x57d41a79bb5a000LL),real(0x470a22b15ed1000LL),
      -real(0x941305430fc000LL),real(0x2571b5b524d7000LL),
      -real(0x15ee8622281e000LL),-real(0x810fd11a43000LL),
      -real(0x3b143f8fcc100LL),reale(236543,0x59e86fb479711LL),
      // C4[5], coeff of eps^14, polynomial in n of order 9
      -real(0x11e2c065bec000LL),real(597104820847LL<<17),
      -real(0x2505ead2add4000LL),real(0x375d7cf9da8000LL),
      -real(0x7d85d31b2fc000LL),real(0xc6e2597bcf0000LL),
      -real(0x1c3d1fca5e4000LL),real(0x26eff911138000LL),
      -real(0x32d040ac10c000LL),real(0xa3358a5620200LL),
      reale(33791,0xe845c6d0a3a27LL),
      // C4[5], coeff of eps^13, polynomial in n of order 10
      -real(0x4e0fa2600780a000LL),real(0x4e911c6aabd6b000LL),
      -real(0x693532675088000LL),real(0x218ccc46e845000LL),
      -real(0x117da33185e06000LL),real(0x4517905378bf000LL),
      -real(0x10ba1c1d3344000LL),real(0x5399b73b0419000LL),
      -real(0x1d57ddd62302000LL),-real(0x2b67cba006d000LL),
      -real(0x17851f6bed3f00LL),reale(236543,0x59e86fb479711LL),
      // C4[5], coeff of eps^12, polynomial in n of order 11
      reale(2256,0x5da9961330000LL),-real(0x4ad304d1312a0000LL),
      -real(0x4061e93f2b8f0000LL),real(0xb6157e3bfe7LL<<19),
      -real(0x11e106d1afa10000LL),-real(0x36aeeaeb6e60000LL),
      -real(0xfcdce3949630000LL),real(0x8af39fd661c0000LL),
      real(0x3d8b99e8cb0000LL),real(0x2f252d98fde0000LL),
      -real(0x29a890537770000LL),real(0x62af9738c95800LL),
      reale(236543,0x59e86fb479711LL),
      // C4[5], coeff of eps^11, polynomial in n of order 12
      real(0x2c14f5cef5da000LL),-real(0xb44f7f3a7637800LL),
      real(0x144dd8529649b000LL),-real(0xdf6b3f6a9dda800LL),
      -real(0x611b67a2b3c4000LL),real(0xe4e2f0fafbb2800LL),
      -real(0x51c03e2adea3000LL),-real(0xd7c7b9cb0f0800LL),
      -real(0x16096a592762000LL),real(0x1c9393e7a4dc800LL),
      -real(0x381de14f961000LL),-real(0xdc6f16ca46800LL),
      -real(0xd4311572ebf80LL),reale(33791,0xe845c6d0a3a27LL),
      // C4[5], coeff of eps^10, polynomial in n of order 13
      -real(0x1f7df788da000LL),-real(0x249f1260a08000LL),
      real(0x2485dbf6336a000LL),-real(0x9fd55d1961bc000LL),
      real(0x13ee6db114d4e000LL),-real(0x114ab28a688b0000LL),
      -real(0x1759d6f434ee000LL),real(0xe5435dae775c000LL),
      -real(0x883ae4654d0a000LL),real(0x6d085594a8000LL),
      -real(0x3b594ff4c6000LL),real(0x18b250a1c574000LL),
      -real(0xc2af3f725e2000LL),real(0x11b5d0e5824b00LL),
      reale(33791,0xe845c6d0a3a27LL),
      // C4[5], coeff of eps^9, polynomial in n of order 14
      -real(0x45be4df1f000LL),-real(0x154928d5d8800LL),
      -real(0x9c093f54d6000LL),-real(0xbe1dac855c3800LL),
      real(0xc8c35d9371b3000LL),-real(0x3b27b3be7f71e800LL),
      reale(2105,0xa27ce5e51c000LL),-reale(2266,0x2251e75549800LL),
      real(0x215c4ca42d605000LL),real(0x52b0fbc40a45b800LL),
      -real(0x52abb6acf6af2000LL),real(0x14cab8bdb5a70800LL),
      real(0x422bb90412d7000LL),real(0xaa8f3f42195800LL),
      -real(0x18c864fb5207380LL),reale(236543,0x59e86fb479711LL),
      // C4[5], coeff of eps^8, polynomial in n of order 15
      -real(0x323b5354000LL),-real(0xa77c1e58000LL),-real(0x297150a3c000LL),
      -real(0xd25b36ef0000LL),-real(0x64c6f9d464000LL),
      -real(0x816d981c288000LL),real(0x91bbe6aceeb4000LL),
      -real(0x2ea0d03ef98a0000LL),real(0x748c356a9df8c000LL),
      -reale(2463,0x44f7c770b8000LL),real(0x55038197b9ea4000LL),
      real(0x24c2f502435b0000LL),-real(0x557a28e333384000LL),
      real(0x319d6c472db18000LL),-real(0xa981b88bf66c000LL),
      real(0x2452a78bb4ce00LL),reale(236543,0x59e86fb479711LL),
      // C4[5], coeff of eps^7, polynomial in n of order 16
      -real(864347LL<<15),-real(77318326272LL),-real(233990443008LL),
      -real(807704598528LL),-real(0x306255a2000LL),-real(0x100b9fcf2800LL),
      -real(0x8171cf3d7000LL),-real(0xb08a440213800LL),
      real(0xd5be3a4ba94000LL),-real(0x4af12ff99ea4800LL),
      real(0xd4237986197f000LL),-real(0x15530c89262c5800LL),
      real(0x12c48ba350cca000LL),-real(0x590f07b7ee96800LL),
      -real(0x53e376c2a7ab000LL),real(0x5b3d559eedc8800LL),
      -real(0x1b37127cacfe280LL),reale(33791,0xe845c6d0a3a27LL),
      // C4[5], coeff of eps^6, polynomial in n of order 17
      -real(10859667456LL),-real(199353LL<<17),-real(67565166592LL),
      -real(190510645248LL),-real(597656199168LL),-real(65543051LL<<15),
      -real(0x869fe272000LL),-real(0x2f027b014000LL),-real(0x19275e39a6000LL),
      -real(0x24c57351390000LL),real(0x305c8c1f55c6000LL),
      -real(0x12c56d86cea0c000LL),real(0x3c958c9a69892000LL),
      -real(0x75427b7d716c8000LL),reale(2264,0x2021045b7e000LL),
      -real(0x686da1b1a7d04000LL),real(0x2b2226f5e6b4a000LL),
      -real(0x7a36190e0daa700LL),reale(236543,0x59e86fb479711LL),
      // C4[5], coeff of eps^5, polynomial in n of order 18
      -real(392933376),-real(865908736),-real(61523<<15),-real(5002905600LL),
      -real(13385551872LL),-real(39200544768LL),-real(128292691968LL),
      -real(483473385472LL),-real(0x1ffab8af000LL),-real(0xbdf5200f800LL),
      -real(0x6d0cb854c000LL),-real(0xacf22c5668800LL),
      real(0xfa276dd8697000LL),-real(0x6c92e41ed151800LL),
      real(0x18f8d3300c4da000LL),-real(0x382fdb2c1baea800LL),
      real(0x4f13f21826f5d000LL),-real(0x3d1b0c8706d53800LL),
      real(0x131873ea3222a180LL),reale(236543,0x59e86fb479711LL),
      // C4[6], coeff of eps^23, polynomial in n of order 0
      real(20016128),real(0x45dab658805LL),
      // C4[6], coeff of eps^22, polynomial in n of order 1
      real(12387831808LL),real(4069857792LL),real(0x1b45118f2c973bLL),
      // C4[6], coeff of eps^21, polynomial in n of order 2
      real(828267LL<<17),-real(2724645LL<<16),real(52104335360LL),
      real(0x22cae1700cc0f3LL),
      // C4[6], coeff of eps^20, polynomial in n of order 3
      real(0x94a2566a8000LL),-real(0x7736ce990000LL),real(0x345f5a38000LL),
      real(0x11f45dc9000LL),real(0x36c560e36413be89LL),
      // C4[6], coeff of eps^19, polynomial in n of order 4
      real(6043548407LL<<18),-real(7867012491LL<<16),real(0xfe56696e0000LL),
      -real(6798211929LL<<16),real(0x66855efe5000LL),
      reale(3630,0x89164e7bf8313LL),
      // C4[6], coeff of eps^18, polynomial in n of order 5
      real(0x588efe4c176000LL),-real(0xcc317e9b08000LL),
      real(0x2e65271667a000LL),-real(0x1cb46908f84000LL),
      -real(0x7bc8d2682000LL),-real(0x36524dd3a400LL),
      reale(39935,0xe3f55f53aa1d1LL),
      // C4[6], coeff of eps^17, polynomial in n of order 6
      real(0x2dbd6ef2050000LL),-real(0x356ee7ee5e8000LL),
      real(0x65e2c9482e0000LL),-real(0x1247a684858000LL),
      real(84899613015LL<<16),-real(0x1b548eba6c8000LL),
      real(0x5c900466be800LL),reale(39935,0xe3f55f53aa1d1LL),
      // C4[6], coeff of eps^16, polynomial in n of order 7
      -real(0x3fff5b5aa54000LL),-real(0x6a2cbaeaf348000LL),
      real(0x2b55e8782dc4000LL),-real(0x69f22faba30000LL),
      real(0x26e11f54b9dc000LL),-real(0x105d41b83118000LL),
      -real(0x12eb1ab4e0c000LL),-real(0x9530f9646a800LL),
      reale(279551,0x3bb59b49a6cb7LL),
      // C4[6], coeff of eps^15, polynomial in n of order 8
      real(0xf488f4012440000LL),-real(0xb16a4f02dfc8000LL),
      -real(0x103bba4a90d0000LL),-real(0x4da08c72a3d8000LL),
      real(0x45a11acaf220000LL),-real(0x25f21bc63e8000LL),
      real(0x12fccd9d4510000LL),-real(0x13e0eb3687f8000LL),
      real(0x356c2e9517d800LL),reale(279551,0x3bb59b49a6cb7LL),
      // C4[6], coeff of eps^14, polynomial in n of order 9
      real(0x28c5c3199aad2000LL),real(0x80d5fb17a810000LL),
      real(0x9c623a70694e000LL),-real(0xf23c0600f3f4000LL),
      real(0x6928769f1ca000LL),-real(0x1e8f96869bf8000LL),
      real(0x4f9253e0b846000LL),-real(0x11e4e806cbfc000LL),
      -real(0x2dad19c0f3e000LL),-real(0x1f2fac1e88dc00LL),
      reale(279551,0x3bb59b49a6cb7LL),
      // C4[6], coeff of eps^13, polynomial in n of order 10
      -real(0xdb139b99ca0000LL),-real(0x5dbaf74a92790000LL),
      real(0x76a096067dfLL<<19),real(0x39f346109690000LL),
      real(964470918621LL<<17),-real(0x10aa5a9917350000LL),
      real(0x49bc5039b7c0000LL),real(0x92ae304aad0000LL),
      real(0x32f3e8ddd3e0000LL),-real(0x233311e51f10000LL),
      real(0x4483a6a16dd000LL),reale(279551,0x3bb59b49a6cb7LL),
      // C4[6], coeff of eps^12, polynomial in n of order 11
      -real(0xfbf5c5edd078000LL),real(0x1202fde81d5f0000LL),
      -real(0x454a07e84fa8000LL),-real(0xbd470dafdb40000LL),
      real(0xb3ba7d182928000LL),-real(0x155dacd6cc70000LL),
      -real(0xdc21a82d608000LL),-real(0xe96f98256dLL<<17),
      real(0x167a9a9742c8000LL),-real(0x7d81f52ed0000LL),
      -real(0x7ffde3fc68000LL),-real(0xe287c62fa3000LL),
      reale(39935,0xe3f55f53aa1d1LL),
      // C4[6], coeff of eps^11, polynomial in n of order 12
      -real(283480971297LL<<18),real(0x5885fb25bf70000LL),
      -real(0xe5dec7019ee0000LL),real(0x13305b31e4ed0000LL),
      -real(0x9278e6008580000LL),-real(0x855a0cffe9d0000LL),
      real(0xd3d848f453e0000LL),-real(0x4a9f485fda70000LL),
      -real(0xfb7b0fc02c0000LL),-real(0x691c2e87310000LL),
      real(806997945397LL<<17),-real(0x9585db4a3b0000LL),
      real(0xa77dc54c8f000LL),reale(39935,0xe3f55f53aa1d1LL),
      // C4[6], coeff of eps^10, polynomial in n of order 13
      real(0x6d0001099000LL),real(0x9a74d7ec5c000LL),-real(0xc18676170e1000LL),
      real(0x45ad31c7f8a2000LL),-real(0xc7369375e55b000LL),
      real(0x1364b97f822e8000LL),-real(0xe19539447ad5000LL),
      -real(0x26bf9b041ad2000LL),real(0xce71cc8200b1000LL),
      -real(0x8c822446468c000LL),real(0x12e554ec5f37000LL),
      real(0xa6c4f3e59ba000LL),real(0x30bb36a52bd000LL),
      -real(0x34440d2d335600LL),reale(39935,0xe3f55f53aa1d1LL),
      // C4[6], coeff of eps^9, polynomial in n of order 14
      real(0x8fcb3bf8000LL),real(0x33bb5d994000LL),real(7630295323LL<<16),
      real(0x2a77da91fcc000LL),-real(0x38ac5a4a0098000LL),
      real(0x160f7571fbc04000LL),-real(0x45e92df7f7ee0000LL),
      real(0x7f01d3c372a3c000LL),-real(0x7edcf27daed28000LL),
      real(0x27dfe4585e674000LL),real(0x38a548f303090000LL),
      -real(0x4b87231069354000LL),real(0x24d2adef05648000LL),
      -real(0x6a5625dbc71c000LL),-real(0x18371a5d233400LL),
      reale(279551,0x3bb59b49a6cb7LL),
      // C4[6], coeff of eps^8, polynomial in n of order 15
      real(257397153792LL),real(991547604992LL),real(0x42cbc6ea000LL),
      real(843451707LL<<15),real(0xe8a206ec6000LL),real(0x170dd449e34000LL),
      -real(0x2102346c3b5e000LL),real(0xe0052eca6690000LL),
      -real(0x318a0eacb0b82000LL),real(0x690a1407d3eec000LL),
      -reale(2182,0xb601e615a6000LL),real(0x61bf435eea348000LL),
      -real(0xe133a8622dca000LL),-real(0x2748b26bf705c000LL),
      real(0x220d7d12f9812000LL),-real(0x98dbd66bee38400LL),
      reale(279551,0x3bb59b49a6cb7LL),
      // C4[6], coeff of eps^7, polynomial in n of order 16
      real(9867LL<<18),real(8045019136LL),real(854413LL<<15),
      real(6856031LL<<14),real(8304289LL<<16),real(0x3232f0a4000LL),
      real(0x1ec960fb8000LL),real(0x3439f07dcc000LL),-real(0x50f0148aea0000LL),
      real(0x25bf6de530f4000LL),-real(0x9635a567bcf8000LL),
      real(0x1735ee17e1e1c000LL),-real(0x25a38fef60750000LL),
      real(0x2834884b55944000LL),-real(0x1b3dfda8c79a8000LL),
      real(0xa981b88bf66c000LL),-real(0x1cc16f4e99cdc00LL),
      reale(93183,0xbe91de6de243dLL),
      // C4[6], coeff of eps^6, polynomial in n of order 17
      real(169275392),real(7007<<16),real(1348931584),real(4358086656LL),
      real(15819288576LL),real(66522136576LL),real(339738054656LL),
      real(0x214230b6000LL),real(0x15d36ff77000LL),real(0x2803a29af8000LL),
      -real(0x43d629aab87000LL),real(0x232131018d3a000LL),
      -real(0x9e155c86fb85000LL),real(0x1c3aabf38857c000LL),
      -real(0x361b1ee81aa83000LL),real(0x44dcb2f8dc1be000LL),
      -real(0x325282c98d281000LL),real(0xf46c321c1b54e00LL),
      reale(279551,0x3bb59b49a6cb7LL),
      // C4[7], coeff of eps^23, polynomial in n of order 0
      real(383798272),real(0x7ee24536c1115LL),
      // C4[7], coeff of eps^22, polynomial in n of order 1
      -real(127523LL<<20),real(34096398336LL),real(0x1f771442bd4c09LL),
      // C4[7], coeff of eps^21, polynomial in n of order 2
      -real(197998999LL<<19),-real(4877411LL<<18),-real(541336621056LL),
      real(0x3b1ebd1165abdce9LL),
      // C4[7], coeff of eps^20, polynomial in n of order 3
      -real(72076029LL<<20),real(33625235LL<<21),-real(96370351LL<<20),
      real(0x142b356fa000LL),real(0x3f32837c872a7963LL),
      // C4[7], coeff of eps^19, polynomial in n of order 4
      -real(2249063181LL<<20),real(51883720989LL<<18),-real(12233087197LL<<19),
      -real(1430728833LL<<18),-real(0x9e5c3c48b000LL),
      reale(46079,0xdfa4f7d6b097bLL),
      // C4[7], coeff of eps^18, polynomial in n of order 5
      -real(19747083035LL<<20),real(5938781185LL<<22),-real(1899464157LL<<20),
      real(2895955713LL<<21),-real(6730130079LL<<20),real(0x490d94cd2c000LL),
      reale(46079,0xdfa4f7d6b097bLL),
      // C4[7], coeff of eps^17, polynomial in n of order 6
      -real(0xf7ed31ddbc0000LL),real(90436020675LL<<17),
      -real(11671406741LL<<19),real(0x58222c9a6a0000LL),
      -real(28407954085LL<<18),-real(6936211449LL<<17),
      -real(0x1e088e877c800LL),reale(46079,0xdfa4f7d6b097bLL),
      // C4[7], coeff of eps^16, polynomial in n of order 7
      -real(688523975841LL<<19),-real(83606333811LL<<20),
      -real(805224840035LL<<19),real(106897379463LL<<21),
      real(22163836107LL<<19),real(88997602799LL<<20),
      -real(151227539575LL<<19),real(0x28435aa5d4b000LL),
      reale(322559,0x1d82c6ded425dLL),
      // C4[7], coeff of eps^15, polynomial in n of order 8
      real(557482450381LL<<20),real(0xfbb72a664ee0000LL),
      -real(0xa9b81eb4ea40000LL),-real(914196917515LL<<17),
      -real(409568792563LL<<19),real(0x4780d431da60000LL),
      -real(0x94b9eca98c0000LL),-real(82946761135LL<<17),
      -real(0x238b221440f800LL),reale(322559,0x1d82c6ded425dLL),
      // C4[7], coeff of eps^14, polynomial in n of order 9
      -real(0x59ec90b7ba5LL<<20),real(233491821731LL<<23),
      real(762388756437LL<<20),real(284558585577LL<<21),
      -real(0xf0573a4eb1LL<<20),real(25275836579LL<<22),
      real(22761999561LL<<20),real(112734627747LL<<21),
      -real(126941809085LL<<20),real(0x2fd680f7c84000LL),
      reale(322559,0x1d82c6ded425dLL),
      // C4[7], coeff of eps^13, polynomial in n of order 10
      real(0xaca84931355LL<<19),real(0x66fb36095adLL<<18),
      -real(0x2e7424117bfLL<<21),real(0xcac2488dd23LL<<18),
      real(762738574899LL<<19),-real(579380269895LL<<18),
      -real(968587667327LL<<20),real(0x73cbed27abc0000LL),
      real(75006191505LL<<19),-real(0xdb0f0aaec0000LL),
      -real(0x63c3eeba719000LL),reale(322559,0x1d82c6ded425dLL),
      // C4[7], coeff of eps^12, polynomial in n of order 11
      real(626455667783LL<<20),-real(567623567285LL<<21),
      real(0xf5d2e8872dLL<<20),-real(13896712169LL<<23),
      -real(798923144989LL<<20),real(364556664237LL<<21),
      -real(129034049335LL<<20),-real(20826366601LL<<22),
      -real(51607570881LL<<20),real(46156477135LL<<21),
      -real(30888509275LL<<20),real(0x6042659ec2000LL),
      reale(46079,0xdfa4f7d6b097bLL),
      // C4[7], coeff of eps^11, polynomial in n of order 12
      real(20777559885LL<<20),-real(569775860071LL<<18),
      real(0xe9ac41f6dbLL<<19),-real(0xef8ba34c8740000LL),
      real(598911876783LL<<21),-real(0x7cf99a74ecc0000LL),
      -real(957375911139LL<<19),real(0xc30e342965c0000LL),
      -real(423483761553LL<<20),real(35714168193LL<<18),
      real(79169625311LL<<19),real(68905136075LL<<18),
      -real(0x2f872ef9963000LL),reale(46079,0xdfa4f7d6b097bLL),
      // C4[7], coeff of eps^10, polynomial in n of order 13
      -real(18988489LL<<20),-real(129894471LL<<22),real(12886996881LL<<20),
      -real(47548938145LL<<21),real(367560238059LL<<20),
      -real(106884143981LL<<23),real(0x11c056e4d45LL<<20),
      -real(470740881351LL<<21),real(64061082015LL<<20),
      real(158992278163LL<<22),-real(634972709127LL<<20),
      real(135054066707LL<<21),-real(41343081645LL<<20),
      -real(0x7382e0581c000LL),reale(46079,0xdfa4f7d6b097bLL),
      // C4[7], coeff of eps^9, polynomial in n of order 14
      -real(7074089LL<<17),-real(95481295LL<<16),-real(249804765LL<<18),
      -real(0x6befb7d790000LL),real(0xb301172bea0000LL),
      -real(0x5978c2137030000LL),real(0x2fbc3e73e21LL<<19),
      -real(0x3f35c80b0f2d0000LL),real(0x6ce3ff0d91260000LL),
      -real(0x7761d1ce42b70000LL),real(0x468057c8ed840000LL),
      real(0x1bcb7dfb99f0000LL),-real(0x26d98474089e0000LL),
      real(0x1d375a3e49150000LL),-real(0x7d9dd8c3269dc00LL),
      reale(322559,0x1d82c6ded425dLL),
      // C4[7], coeff of eps^8, polynomial in n of order 15
      -real(47805LL<<18),-real(105987LL<<19),-real(1141959LL<<18),
      -real(2026311LL<<20),-real(89791009LL<<18),-real(1389164665LL<<19),
      real(79467759189LL<<18),-real(86766818957LL<<21),
      real(0xbfc5c91f6ec0000LL),-real(0x487b27f822fLL<<19),
      real(0x4a699e0854c40000LL),-real(0x69d85e75b6dLL<<20),
      real(0x66f7a9fb575c0000LL),-real(0x828d4038ea5LL<<19),
      real(0x60dc69748cdLL<<18),-real(0x3f90a5347c68800LL),
      reale(322559,0x1d82c6ded425dLL),
      // C4[7], coeff of eps^7, polynomial in n of order 16
      -real(143<<20),-real(8085<<16),-real(16121<<17),-real(9810411520LL),
      -real(212205LL<<18),-real(6380297LL<<16),-real(37701755LL<<17),
      -real(0x95a9db330000LL),real(9764754545LL<<19),-real(0xaf0fe765fd0000LL),
      real(0x3a2548493060000LL),-real(0xc8bdaa520270000LL),
      real(0x7871cc979b1LL<<18),-real(0x3353672f26710000LL),
      real(0x3c89c1e8d8020000LL),-real(0x2a606e22fd9b0000LL),
      real(0xc94a0b2634a0400LL),reale(322559,0x1d82c6ded425dLL),
      // C4[8], coeff of eps^23, polynomial in n of order 0
      real(7579<<15),real(0x4f56c0c24f87LL),
      // C4[8], coeff of eps^22, polynomial in n of order 1
      -real(1660549LL<<21),-real(23648625LL<<16),real(0x38232f25bccb5275LL),
      // C4[8], coeff of eps^21, polynomial in n of order 2
      real(9646043LL<<20),-real(24019457LL<<19),real(74048359LL<<15),
      real(0x99262e0aeeff091LL),
      // C4[8], coeff of eps^20, polynomial in n of order 3
      real(183351957435LL<<19),-real(32827160863LL<<20),
      -real(6509093591LL<<19),-real(0x6677b4e9b0000LL),
      reale(365566,0xff4ff27401803LL),
      // C4[8], coeff of eps^19, polynomial in n of order 4
      real(67207908275LL<<21),-real(201042891LL<<19),real(44011096899LL<<20),
      -real(85786308153LL<<19),real(0x195ba7c1ef8000LL),
      reale(365566,0xff4ff27401803LL),
      // C4[8], coeff of eps^18, polynomial in n of order 5
      -real(13677739LL<<21),-real(1155605701LL<<23),real(11263093395LL<<21),
      -real(1170886701LL<<22),-real(422863935LL<<21),-real(9609473031LL<<16),
      reale(52223,0xdb549059b7125LL),
      // C4[8], coeff of eps^17, polynomial in n of order 6
      -real(105328611LL<<20),-real(0xe3d4e1d7080000LL),real(9484526351LL<<21),
      real(4879307961LL<<19),real(13462873311LL<<20),-real(19014362253LL<<19),
      real(0x45bace6718000LL),reale(52223,0xdb549059b7125LL),
      // C4[8], coeff of eps^16, polynomial in n of order 7
      real(0x4802f7e045bLL<<18),-real(787109524929LL<<19),
      -real(616781829503LL<<18),-real(267630157067LL<<20),
      real(0xf57f439a67LL<<18),-real(26811748075LL<<19),
      -real(29646920051LL<<18),-real(0x25c0cef2988000LL),
      reale(365566,0xff4ff27401803LL),
      // C4[8], coeff of eps^15, polynomial in n of order 8
      real(61397460605LL<<22),real(0x9d011c37ef80000LL),
      real(907553463943LL<<20),-real(0xc0a473ee4980000LL),
      -real(21778698179LL<<21),-real(22179652453LL<<19),
      real(224024408237LL<<20),-real(212571195095LL<<19),
      real(0x216a7bfadc8000LL),reale(365566,0xff4ff27401803LL),
      // C4[8], coeff of eps^14, polynomial in n of order 9
      real(304663697949LL<<21),-real(51558232553LL<<24),
      real(126037118963LL<<21),real(28559389965LL<<22),real(12939195833LL<<21),
      -real(17167224841LL<<23),real(24466781775LL<<21),real(2302458607LL<<22),
      real(456812693LL<<21),-real(0xde9c5a4230000LL),
      reale(52223,0xdb549059b7125LL),
      // C4[8], coeff of eps^13, polynomial in n of order 10
      -real(0x71eca5b57e5LL<<20),real(0x8d98ab5c54bLL<<19),
      real(497026592783LL<<22),-real(0xacc7c9e1d9bLL<<19),
      real(0x35a7c7b51ddLL<<20),-real(81233361377LL<<19),
      -real(253988603057LL<<21),-real(954606696519LL<<19),
      real(577751554079LL<<20),-real(333997527437LL<<19),
      real(0x1689b847558000LL),reale(365566,0xff4ff27401803LL),
      // C4[8], coeff of eps^12, polynomial in n of order 11
      -real(0x367f7beda59LL<<19),real(0x45996b8ba21LL<<20),
      -real(0xdceb5493fc3LL<<19),real(0x18843cb160dLL<<22),
      -real(0x21789a51fedLL<<19),-real(0x41cde5aa8b9LL<<20),
      real(0x95638f58ea9LL<<19),-real(984566251123LL<<21),
      -real(435207598721LL<<19),real(219309948781LL<<20),
      real(274765170197LL<<19),-real(0x12cf88fa6ff0000LL),
      reale(365566,0xff4ff27401803LL),
      // C4[8], coeff of eps^11, polynomial in n of order 12
      -real(2296713447LL<<21),real(78660216877LL<<19),
      -real(180155131441LL<<20),real(0xeee01825bfLL<<19),
      -real(237440161933LL<<22),real(0x2042cbdcd31LL<<19),
      -real(652079196855LL<<20),-real(325903664957LL<<19),
      real(324695717299LL<<21),-real(0xf97e21ed4bLL<<19),
      real(203483994947LL<<20),-real(52367903417LL<<19),
      -real(0x8a9d0d3688000LL),reale(52223,0xdb549059b7125LL),
      // C4[8], coeff of eps^10, polynomial in n of order 13
      real(1140139LL<<21),real(9315711LL<<23),-real(1126319139LL<<21),
      real(5199009105LL<<22),-real(52132384161LL<<21),real(20770352565LL<<24),
      -real(357583911087LL<<21),real(262213551639LL<<22),
      -real(498523677485LL<<21),real(60302341333LL<<23),
      real(57310064901LL<<21),-real(90954779619LL<<22),
      real(124029244935LL<<21),-real(0xf0a5fe0ce50000LL),
      reale(52223,0xdb549059b7125LL),
      // C4[8], coeff of eps^9, polynomial in n of order 14
      real(54009LL<<20),real(849303LL<<19),real(2623117LL<<21),
      real(364892913LL<<19),-real(5919882885LL<<20),real(0xdd0128d3580000LL),
      -real(81910832913LL<<22),real(0x2229f5f9745LL<<19),
      -real(0x2a9587ee883LL<<20),real(0x982f47b44bfLL<<19),
      -real(0x30e1739ffd1LL<<21),real(0xb09887dee19LL<<19),
      -real(0x35101f0ee01LL<<20),real(0x25e6f19ce93LL<<19),
      -real(0x306e34ba4668000LL),reale(365566,0xff4ff27401803LL),
      // C4[8], coeff of eps^8, polynomial in n of order 15
      real(2295<<17),real(5831<<18),real(72709LL<<17),real(151011LL<<19),
      real(7936467LL<<17),real(147906885LL<<18),-real(0x4d5c1f23e0000LL),
      real(14228642337LL<<20),-real(697203474513LL<<17),
      real(0x51fe4e56b0c0000LL),-real(0xeb59f3d2e860000LL),
      real(0x3e0c14100a1LL<<19),-real(0x305340db42ea0000LL),
      real(0xd6c75923d41LL<<18),-real(0x2452a78bb4ce0000LL),
      real(0xa981b88bf66c000LL),reale(365566,0xff4ff27401803LL),
      // C4[9], coeff of eps^23, polynomial in n of order 0
      -real(45613<<15),real(0xa0b835899f381LL),
      // C4[9], coeff of eps^22, polynomial in n of order 1
      -real(4663637LL<<21),real(25498473LL<<16),real(0x8f68f0ea15ed989LL),
      // C4[9], coeff of eps^21, polynomial in n of order 2
      -real(313787291LL<<20),-real(89546863LL<<19),-real(880826107LL<<15),
      reale(5306,0x2ad1d52b570cdLL),
      // C4[9], coeff of eps^20, polynomial in n of order 3
      real(1691751267LL<<22),real(5868457511LL<<23),-real(9710518895LL<<22),
      real(43389881073LL<<17),reale(408574,0xe11d1e092eda9LL),
      // C4[9], coeff of eps^19, polynomial in n of order 4
      -real(45668361181LL<<21),real(290185772373LL<<19),
      -real(19310638221LL<<20),-real(10267037529LL<<19),
      -real(0x11435a10568000LL),reale(408574,0xe11d1e092eda9LL),
      // C4[9], coeff of eps^18, polynomial in n of order 5
      -real(206915608111LL<<21),real(8005795847LL<<23),real(6676372983LL<<21),
      real(24266221119LL<<22),-real(29173391667LL<<21),real(99595856143LL<<16),
      reale(408574,0xe11d1e092eda9LL),
      // C4[9], coeff of eps^17, polynomial in n of order 6
      -real(15515879355LL<<20),-real(36184750873LL<<19),
      -real(22177807609LL<<21),real(62194714929LL<<19),real(693176727LL<<20),
      -real(1189966821LL<<19),-real(0x5829503048000LL),
      reale(58367,0xd70428dcbd8cfLL),
      // C4[9], coeff of eps^16, polynomial in n of order 7
      real(38512528273LL<<23),real(67772681235LL<<24),-real(74410968653LL<<23),
      -real(3984568679LL<<25),-real(6152374683LL<<23),real(13551170801LL<<24),
      -real(11115057401LL<<23),real(24916219839LL<<18),
      reale(408574,0xe11d1e092eda9LL),
      // C4[9], coeff of eps^15, polynomial in n of order 8
      -real(162298412813LL<<22),real(0xff4317f5080000LL),
      real(119179074953LL<<20),real(0xf6d36e74980000LL),
      -real(63634032589LL<<21),real(61952932453LL<<19),real(10785104899LL<<20),
      real(4191026519LL<<19),-real(0xd59ae9d0e8000LL),
      reale(58367,0xd70428dcbd8cfLL),
      // C4[9], coeff of eps^14, polynomial in n of order 9
      real(162971496591LL<<21),real(33816350309LL<<24),
      -real(394783736543LL<<21),real(85862751303LL<<22),
      real(32462900611LL<<21),-real(6369607931LL<<23),-real(39152071083LL<<21),
      real(18189729581LL<<22),-real(9249690569LL<<21),real(6171570141LL<<16),
      reale(58367,0xd70428dcbd8cfLL),
      // C4[9], coeff of eps^13, polynomial in n of order 10
      real(0x52d38896f8bLL<<20),-real(0xd3acdf03195LL<<19),
      real(0x1195b2a1cffLL<<22),real(0xca9586e4a280000LL),
      -real(0x486f0b6e413LL<<20),real(0x7ca2ce8a83fLL<<19),
      -real(610236546241LL<<21),-real(717677267559LL<<19),
      real(159176229583LL<<20),real(291633515411LL<<19),
      -real(0x110150274e88000LL),reale(408574,0xe11d1e092eda9LL),
      // C4[9], coeff of eps^12, polynomial in n of order 11
      real(143956869023LL<<22),-real(243108013001LL<<23),
      real(0x101d5eb1615LL<<22),-real(213537904349LL<<25),
      real(0x183f300cffbLL<<22),-real(350529456991LL<<23),
      -real(545724783247LL<<22),real(274121340227LL<<24),
      -real(785966166377LL<<22),real(135225754699LL<<23),
      -real(28607511667LL<<22),-real(0x3ee3b308260000LL),
      reale(408574,0xe11d1e092eda9LL),
      // C4[9], coeff of eps^11, polynomial in n of order 12
      real(2520290511LL<<21),-real(0xc4ddd05ba80000LL),
      real(304931349961LL<<20),-real(0x21230116cd7LL<<19),
      real(735928623493LL<<22),-real(0x9d254a11d99LL<<19),
      real(0x6510e717cdfLL<<20),-real(0xa95d67804fbLL<<19),
      real(0x1055dd17e45LL<<21),real(0x239bcd685c3LL<<19),
      -real(0x22ba072788bLL<<20),real(0x2c142a0db61LL<<19),
      -real(0x59b3a2379f58000LL),reale(408574,0xe11d1e092eda9LL),
      // C4[9], coeff of eps^10, polynomial in n of order 13
      -real(29393LL<<21),-real(283917LL<<23),real(41246777LL<<21),
      -real(233407875LL<<22),real(2943398547LL<<21),-real(1525553871LL<<24),
      real(35837133917LL<<21),-real(38620600629LL<<22),
      real(123783976375LL<<21),-real(36640057007LL<<23),
      real(124599494337LL<<21),-real(35830670759LL<<22),
      real(24805848987LL<<21),-real(0x1ce0b816070000LL),
      reale(19455,0xf256b84994845LL),
      // C4[9], coeff of eps^9, polynomial in n of order 14
      -real(1615<<20),-real(29393LL<<19),-real(106267LL<<21),
      -real(17534055LL<<19),real(342711075LL<<20),-real(8430692445LL<<19),
      real(7306600119LL<<22),-real(270344204403LL<<19),
      real(450573674005LL<<20),-real(0x20c896b3e69LL<<19),
      real(0xfa29e850f7LL<<21),-real(0x5aaf3103bffLL<<19),
      real(0x3002653e387LL<<20),-real(0x3f2b92b02f5LL<<19),
      real(0x914a9e2ed338000LL),reale(408574,0xe11d1e092eda9LL),
      // C4[10], coeff of eps^23, polynomial in n of order 0
      real(137<<21),real(0x8757c14b789bLL),
      // C4[10], coeff of eps^22, polynomial in n of order 1
      -real(1152691LL<<20),-real(6743919LL<<17),real(0x9e817610332f06fLL),
      // C4[10], coeff of eps^21, polynomial in n of order 2
      real(79722199LL<<23),-real(113766289LL<<22),real(225212673LL<<18),
      reale(5864,0xb6105765cc00bLL),
      // C4[10], coeff of eps^20, polynomial in n of order 3
      real(64857768639LL<<21),-real(2220489243LL<<22),-real(2012833515LL<<21),
      -real(19551629405LL<<18),reale(451582,0xc2ea499e5c34fLL),
      // C4[10], coeff of eps^19, polynomial in n of order 4
      real(656353407LL<<24),real(1031809317LL<<22),real(12215335391LL<<23),
      -real(12759999497LL<<22),real(18944346729LL<<18),
      reale(451582,0xc2ea499e5c34fLL),
      // C4[10], coeff of eps^18, polynomial in n of order 5
      -real(62867132873LL<<20),-real(83127481829LL<<22),
      real(173460262689LL<<20),real(8415873627LL<<21),-real(1024568181LL<<20),
      -real(82657907689LL<<17),reale(451582,0xc2ea499e5c34fLL),
      // C4[10], coeff of eps^17, polynomial in n of order 6
      real(69839518785LL<<24),-real(46975322289LL<<23),-real(5175253237LL<<25),
      -real(10608265143LL<<23),real(12870275691LL<<24),-real(9303053053LL<<23),
      real(8528136981LL<<19),reale(451582,0xc2ea499e5c34fLL),
      // C4[10], coeff of eps^16, polynomial in n of order 7
      -real(12671764325LL<<22),real(11821938135LL<<23),real(23903917953LL<<22),
      -real(7023725731LL<<24),real(4254825447LL<<22),real(1372261021LL<<23),
      real(755775181LL<<22),-real(6809268397LL<<19),
      reale(64511,0xd2b3c15fc4079LL),
      // C4[10], coeff of eps^15, polynomial in n of order 8
      real(10583074157LL<<26),-real(84530118029LL<<23),real(12150058407LL<<24),
      real(12380362825LL<<23),-real(838454291LL<<25),-real(10410407457LL<<23),
      real(3974759309LL<<24),-real(1799658059LL<<23),real(156358707LL<<19),
      reale(64511,0xd2b3c15fc4079LL),
      // C4[10], coeff of eps^14, polynomial in n of order 9
      -real(922119298407LL<<20),real(52944024001LL<<23),
      real(329638564983LL<<20),-real(354979062141LL<<21),
      real(493120994773LL<<20),-real(24099541823LL<<22),
      -real(59503561293LL<<20),real(7459230081LL<<21),real(21243323153LL<<20),
      -real(75576440907LL<<17),reale(64511,0xd2b3c15fc4079LL),
      // C4[10], coeff of eps^13, polynomial in n of order 10
      -real(328595996641LL<<23),real(0x1245cb281e3LL<<22),
      -real(207527442829LL<<25),real(0x13d84cf39cdLL<<22),
      -real(169653271431LL<<23),-real(705690429577LL<<22),
      real(256163704307LL<<24),-real(657414782367LL<<22),
      real(103463476179LL<<23),-real(17233182197LL<<22),
      -real(65863805931LL<<18),reale(451582,0xc2ea499e5c34fLL),
      // C4[10], coeff of eps^12, polynomial in n of order 11
      -real(60530460661LL<<21),real(129708905557LL<<22),
      -real(783916037751LL<<21),real(215690023633LL<<24),
      -real(0x287cc397f79LL<<21),real(0x174d319d033LL<<22),
      -real(0x22bf2de15fbLL<<21),real(172524970961LL<<23),
      real(736992166659LL<<21),-real(554058611183LL<<22),
      real(665956259969LL<<21),-real(0x4d7d212a0a40000LL),
      reale(451582,0xc2ea499e5c34fLL),
      // C4[10], coeff of eps^11, polynomial in n of order 12
      -real(31220211LL<<24),real(1576100141LL<<22),-real(5588687797LL<<23),
      real(52675808031LL<<22),-real(22267080913LL<<25),
      real(449824279121LL<<22),-real(432213499347LL<<23),
      real(0x1275ac4a843LL<<22),-real(351080482641LL<<24),
      real(0x10853170e75LL<<22),-real(314682628337LL<<23),
      real(212227819111LL<<22),-real(520922828727LL<<18),
      reale(451582,0xc2ea499e5c34fLL),
      // C4[10], coeff of eps^10, polynomial in n of order 13
      real(46189LL<<20),real(522291LL<<22),-real(90008149LL<<20),
      real(613691925LL<<21),-real(9499950999LL<<20),real(6182507793LL<<23),
      -real(187536069721LL<<20),real(270344204403LL<<21),
      -real(0x11a7161219bLL<<20),real(533756506129LL<<22),
      -real(0x2a7db4d305dLL<<20),real(0x159e458acd1LL<<21),
      -real(0x1bcb7dfb99fLL<<20),real(0x7e5725605ea0000LL),
      reale(451582,0xc2ea499e5c34fLL),
      // C4[11], coeff of eps^23, polynomial in n of order 0
      -real(7309LL<<21),real(0x2c95e8ad321065LL),
      // C4[11], coeff of eps^22, polynomial in n of order 1
      -real(118877LL<<30),real(1675947LL<<23),real(0x7759dcb5574d50a7LL),
      // C4[11], coeff of eps^21, polynomial in n of order 2
      -real(9105745LL<<24),-real(49846181LL<<23),-real(2866583251LL<<18),
      reale(70655,0xce6359e2ca823LL),
      // C4[11], coeff of eps^20, polynomial in n of order 3
      -real(239228553LL<<25),real(1509768547LL<<26),-real(1393694995LL<<25),
      real(7195205325LL<<19),reale(494590,0xa4b77533898f5LL),
      // C4[11], coeff of eps^19, polynomial in n of order 4
      -real(10520646403LL<<25),real(16651704531LL<<23),real(1510969677LL<<24),
      real(227849937LL<<23),-real(40629886913LL<<18),
      reale(494590,0xa4b77533898f5LL),
      // C4[11], coeff of eps^18, polynomial in n of order 5
      -real(737236949LL<<28),-real(83959015LL<<31),-real(449296547LL<<28),
      real(188420603LL<<30),-real(243597193LL<<28),real(1420486123LL<<21),
      reale(494590,0xa4b77533898f5LL),
      // C4[11], coeff of eps^17, polynomial in n of order 6
      real(1797306345LL<<25),real(7110272827LL<<24),-real(1494242189LL<<26),
      real(407981949LL<<24),real(324085539LL<<25),real(232922271LL<<24),
      -real(6431919403LL<<19),reale(70655,0xce6359e2ca823LL),
      // C4[11], coeff of eps^16, polynomial in n of order 7
      -real(59422002475LL<<26),real(4462082415LL<<27),real(11958968063LL<<26),
      -real(116564371LL<<28),-real(9243946887LL<<26),real(3024840805LL<<27),
      -real(1229077213LL<<26),-real(836978961LL<<20),
      reale(494590,0xa4b77533898f5LL),
      // C4[11], coeff of eps^15, polynomial in n of order 8
      real(1450234755LL<<27),real(28955596425LL<<24),-real(20916501415LL<<25),
      real(24148276875LL<<24),-real(639979965LL<<26),-real(3796939603LL<<24),
      real(257117683LL<<25),real(1321384367LL<<24),-real(17153469915LL<<19),
      reale(70655,0xce6359e2ca823LL),
      // C4[11], coeff of eps^14, polynomial in n of order 9
      real(2991071409LL<<28),-real(215656441LL<<32),real(2375561279LL<<28),
      -real(29715609LL<<30),-real(1772722171LL<<28),real(262089343LL<<31),
      -real(1227751437LL<<28),real(88909853LL<<30),-real(21460999LL<<28),
      -real(1112906091LL<<21),reale(70655,0xce6359e2ca823LL),
      // C4[11], coeff of eps^13, polynomial in n of order 10
      real(48251719021LL<<24),-real(247802667483LL<<23),
      real(59903451769LL<<26),-real(693923403733LL<<23),
      real(362458490331LL<<24),-real(482970502063LL<<23),
      real(22585671353LL<<25),real(201583163607LL<<23),
      -real(128100703031LL<<24),real(147544368125LL<<23),
      -real(0x43bae67ca340000LL),reale(494590,0xa4b77533898f5LL),
      // C4[11], coeff of eps^12, polynomial in n of order 11
      real(488107587LL<<25),-real(1288790349LL<<26),real(9866997217LL<<25),
      -real(3570890001LL<<28),real(64004720367LL<<25),-real(56017267579LL<<26),
      real(152843494797LL<<25),-real(39981841137LL<<27),
      real(123894347227LL<<25),-real(33286009449LL<<26),
      real(21954601977LL<<25),-real(212227819111LL<<19),
      reale(494590,0xa4b77533898f5LL),
      // C4[11], coeff of eps^11, polynomial in n of order 12
      real(735471LL<<25),-real(44046541LL<<23),real(188198857LL<<24),
      -real(2177729631LL<<23),real(1156078693LL<<26),-real(30163144081LL<<23),
      real(38781185247LL<<24),-real(159433761571LL<<23),
      real(65649195941LL<<25),-real(342066863061LL<<23),
      real(168318615157LL<<24),-real(212227819111LL<<23),
      real(0x6f2df7ee67c0000LL),reale(494590,0xa4b77533898f5LL),
      // C4[12], coeff of eps^23, polynomial in n of order 0
      real(173LL<<24),real(0x88d5e64011771LL),
      // C4[12], coeff of eps^22, polynomial in n of order 1
      -real(163369LL<<28),-real(266903LL<<29),reale(14529,0xb09bccfe817bfLL),
      // C4[12], coeff of eps^21, polynomial in n of order 2
      real(26283479LL<<29),-real(21738605LL<<28),real(24285135LL<<24),
      reale(76799,0xca12f265d0fcdLL),
      // C4[12], coeff of eps^20, polynomial in n of order 3
      real(6122492151LL<<24),real(880448149LL<<25),real(269123645LL<<24),
      -real(4943792525LL<<21),reale(537598,0x8684a0c8b6e9bLL),
      // C4[12], coeff of eps^19, polynomial in n of order 4
      -real(616982441LL<<28),-real(2168310039LL<<26),real(1398586567LL<<27),
      -real(817632445LL<<26),real(450511215LL<<22),
      reale(537598,0x8684a0c8b6e9bLL),
      // C4[12], coeff of eps^18, polynomial in n of order 5
      real(1912616275LL<<26),-real(308159801LL<<28),-real(17594779LL<<26),
      real(72918855LL<<27),real(66311031LL<<26),-real(47313631LL<<26),
      reale(76799,0xca12f265d0fcdLL),
      // C4[12], coeff of eps^17, polynomial in n of order 6
      real(9134109LL<<27),real(1642561735LL<<26),real(58767343LL<<28),
      -real(1299624495LL<<26),real(374812639LL<<27),-real(137300677LL<<26),
      -real(61400001LL<<22),reale(76799,0xca12f265d0fcdLL),
      // C4[12], coeff of eps^16, polynomial in n of order 7
      real(118127909265LL<<25),-real(66457563795LL<<26),
      real(64469127555LL<<25),-real(134108625LL<<27),-real(12700511691LL<<25),
      real(295233743LL<<26),real(4531750951LL<<25),-real(13670656363LL<<22),
      reale(537598,0x8684a0c8b6e9bLL),
      // C4[12], coeff of eps^15, polynomial in n of order 8
      -real(10859744975LL<<29),real(49132517315LL<<26),real(5188275715LL<<27),
      -real(52074703975LL<<26),real(13295845745LL<<28),
      -real(28808201009LL<<26),real(3853119361LL<<27),-real(278992987LL<<26),
      -real(3626908831LL<<22),reale(537598,0x8684a0c8b6e9bLL),
      // C4[12], coeff of eps^14, polynomial in n of order 9
      -real(5262740745LL<<26),real(1142543055LL<<29),-real(12070462215LL<<26),
      real(5779723245LL<<27),-real(6878321925LL<<26),real(125534415LL<<28),
      real(3745400061LL<<26),-real(2112375473LL<<27),real(2351512319LL<<26),
      -real(573315259LL<<26),reale(76799,0xca12f265d0fcdLL),
      // C4[12], coeff of eps^13, polynomial in n of order 10
      -real(345262775LL<<27),real(2254590065LL<<26),-real(721021595LL<<29),
      real(11719656095LL<<26),-real(9489736865LL<<27),real(24346633325LL<<26),
      -real(6069982555LL<<28),real(18134544155LL<<26),-real(4742880779LL<<27),
      real(3068922857LL<<26),-real(7318200659LL<<22),
      reale(179199,0x822c35983cf89LL),
      // C4[12], coeff of eps^12, polynomial in n of order 11
      -real(58429085LL<<24),real(185910725LL<<25),-real(1747560815LL<<24),
      real(794345825LL<<27),-real(18392161025LL<<24),real(21545102915LL<<25),
      -real(82378334675LL<<24),real(32084193505LL<<26),
      -real(160420967525LL<<24),real(76723071425LL<<25),
      -real(95136608567LL<<24),real(212227819111LL<<21),
      reale(537598,0x8684a0c8b6e9bLL),
      // C4[13], coeff of eps^23, polynomial in n of order 0
      -real(34717LL<<24),real(0x4013d857859e5adLL),
      // C4[13], coeff of eps^22, polynomial in n of order 1
      -real(52837LL<<30),real(101283LL<<25),real(0x39b1009e5dec691dLL),
      // C4[13], coeff of eps^21, polynomial in n of order 2
      real(58223275LL<<29),real(25058159LL<<28),-real(597584743LL<<24),
      reale(580606,0x6851cc5de4441LL),
      // C4[13], coeff of eps^20, polynomial in n of order 3
      -real(38160201LL<<32),real(20133099LL<<33),-real(10736915LL<<32),
      real(8118075LL<<27),reale(580606,0x6851cc5de4441LL),
      // C4[13], coeff of eps^19, polynomial in n of order 4
      -real(246943573LL<<28),-real(102114339LL<<26),real(63266747LL<<27),
      real(72037887LL<<26),-real(711672919LL<<22),
      reale(82943,0xc5c28ae8d7777LL),
      // C4[13], coeff of eps^18, polynomial in n of order 5
      real(362438863LL<<28),real(29917105LL<<30),-real(313139991LL<<28),
      real(81176473LL<<29),-real(26857069LL<<28),-real(40519029LL<<23),
      reale(82943,0xc5c28ae8d7777LL),
      // C4[13], coeff of eps^17, polynomial in n of order 6
      -real(4194208665LL<<27),real(3411193933LL<<26),real(92059229LL<<28),
      -real(832792389LL<<26),-real(13821619LL<<27),real(313960329LL<<26),
      -real(1784908801LL<<22),reale(82943,0xc5c28ae8d7777LL),
      // C4[13], coeff of eps^16, polynomial in n of order 7
      real(4206195495LL<<29),real(1286394165LL<<30),-real(6553065099LL<<29),
      real(1494451903LL<<31),-real(3024727629LL<<29),real(374117415LL<<30),
      -real(7540351LL<<29),-real(836978961LL<<24),
      reale(580606,0x6851cc5de4441LL),
      // C4[13], coeff of eps^15, polynomial in n of order 8
      real(8293864515LL<<29),-real(80835230175LL<<26),real(35736027705LL<<27),
      -real(37780361325LL<<26),-real(587595645LL<<28),real(26485772901LL<<26),
      -real(13655575661LL<<27),real(14786628311LL<<26),
      -real(57193562335LL<<22),reale(580606,0x6851cc5de4441LL),
      // C4[13], coeff of eps^14, polynomial in n of order 9
      real(2173316805LL<<28),-real(627936225LL<<31),real(9404910795LL<<28),
      -real(7129362555LL<<29),real(17350941825LL<<28),-real(4150093185LL<<30),
      real(12011779143LL<<28),-real(3068922857LL<<29),real(1952950909LL<<28),
      -real(9206768571LL<<23),reale(580606,0x6851cc5de4441LL),
      // C4[13], coeff of eps^13, polynomial in n of order 10
      real(79676025LL<<27),-real(638856855LL<<26),real(256634805LL<<29),
      -real(5389330905LL<<26),real(5842215855LL<<27),-real(21011478075LL<<26),
      real(7804263285LL<<28),-real(37664053245LL<<26),real(17576558181LL<<27),
      -real(21482459999LL<<26),real(95136608567LL<<22),
      reale(580606,0x6851cc5de4441LL),
      // C4[14], coeff of eps^23, polynomial in n of order 0
      real(433LL<<27),real(0x16f0fb486be35c9LL),
      // C4[14], coeff of eps^22, polynomial in n of order 1
      real(938669LL<<29),-real(8460179LL<<26),reale(36683,0x318959e11f277LL),
      // C4[14], coeff of eps^21, polynomial in n of order 2
      real(1085551LL<<33),-real(531601LL<<32),real(109557LL<<28),
      reale(36683,0x318959e11f277LL),
      // C4[14], coeff of eps^20, polynomial in n of order 3
      -real(34899909LL<<31),real(11630633LL<<32),real(16602985LL<<31),
      -real(73138345LL<<28),reale(623614,0x4a1ef7f3119e7LL),
      // C4[14], coeff of eps^19, polynomial in n of order 4
      real(2603869LL<<34),-real(18588201LL<<32),real(4394077LL<<33),
      -real(1312099LL<<32),-real(1449057LL<<28),reale(89087,0xc172236bddf21LL),
      // C4[14], coeff of eps^18, polynomial in n of order 5
      real(1218191717LL<<27),real(79106081LL<<29),-real(371875421LL<<27),
      -real(20795103LL<<28),real(151229409LL<<27),-real(409250479LL<<24),
      reale(89087,0xc172236bddf21LL),
      // C4[14], coeff of eps^17, polynomial in n of order 6
      real(249532965LL<<30),-real(917899213LL<<29),real(191097911LL<<31),
      -real(363925371LL<<29),real(41606327LL<<30),real(1574359LL<<29),
      -real(54936843LL<<25),reale(89087,0xc172236bddf21LL),
      // C4[14], coeff of eps^16, polynomial in n of order 7
      -real(19067218845LL<<28),real(7820446095LL<<29),-real(7262714151LL<<28),
      -real(421931643LL<<30),real(6566089551LL<<28),-real(3155926907LL<<29),
      real(3340375493LL<<28),-real(6416838701LL<<25),
      reale(623614,0x4a1ef7f3119e7LL),
      // C4[14], coeff of eps^15, polynomial in n of order 8
      -real(353006415LL<<32),real(4931374455LL<<29),-real(3531935085LL<<30),
      real(8211223125LL<<29),-real(1894184271LL<<31),real(5332188211LL<<29),
      -real(1334642127LL<<30),real(836978961LL<<29),-real(1952950909LL<<25),
      reale(623614,0x4a1ef7f3119e7LL),
      // C4[14], coeff of eps^14, polynomial in n of order 9
      -real(436268025LL<<27),real(158349135LL<<30),-real(3064521495LL<<27),
      real(3110604525LL<<28),-real(10615555125LL<<27),real(3784676175LL<<29),
      -real(17712284499LL<<27),real(8090796623LL<<28),-real(9764754545LL<<27),
      real(21482459999LL<<24),reale(623614,0x4a1ef7f3119e7LL),
      // C4[15], coeff of eps^23, polynomial in n of order 0
      -real(11003LL<<27),real(0x6a44bb11ad2310dLL),
      // C4[15], coeff of eps^22, polynomial in n of order 1
      -real(28003LL<<36),real(3549LL<<30),reale(39213,0x11a47a8f8b3bdLL),
      // C4[15], coeff of eps^21, polynomial in n of order 2
      real(1243LL<<38),real(2249LL<<37),-real(577583LL<<28),
      reale(5601,0xddf2ecefef51bLL),
      // C4[15], coeff of eps^20, polynomial in n of order 3
      -real(28101LL<<40),real(24493LL<<39),-real(1645LL<<40),
      -real(318801LL<<29),reale(39213,0x11a47a8f8b3bdLL),
      // C4[15], coeff of eps^19, polynomial in n of order 4
      real(1359187LL<<38),-real(4447191LL<<36),-real(433293LL<<37),
      real(1982883LL<<36),-real(164770109LL<<28),
      reale(666622,0x2bec23883ef8dLL),
      // C4[15], coeff of eps^18, polynomial in n of order 5
      -real(6907451LL<<36),real(1332757LL<<38),-real(2401277LL<<36),
      real(253189LL<<37),real(26273LL<<36),-real(1574359LL<<30),
      reale(95231,0xbd21bbeee46cbLL),
      // C4[15], coeff of eps^17, polynomial in n of order 6
      real(60642045LL<<33),-real(48519929LL<<32),-real(5596337LL<<34),
      real(57431697LL<<32),-real(26089089LL<<33),real(27095547LL<<32),
      -real(828361417LL<<25),reale(95231,0xbd21bbeee46cbLL),
      // C4[15], coeff of eps^16, polynomial in n of order 7
      real(53036505LL<<34),-real(36153285LL<<35),real(80745483LL<<34),
      -real(18042031LL<<36),real(49556941LL<<34),-real(12180567LL<<35),
      real(7540351LL<<34),-real(278992987LL<<26),
      reale(222207,0x63f9612d6a52fLL),
      // C4[15], coeff of eps^15, polynomial in n of order 8
      real(5892945LL<<35),-real(106383165LL<<32),real(102040995LL<<33),
      -real(332742375LL<<32),real(114463377LL<<34),-real(521444273LL<<32),
      real(233750881LL<<33),-real(278992987LL<<32),real(9764754545LL<<25),
      reale(666622,0x2bec23883ef8dLL),
      // C4[16], coeff of eps^23, polynomial in n of order 0
      -real(1LL<<31),real(0x5f43434b6401e1LL),
      // C4[16], coeff of eps^22, polynomial in n of order 1
      real(4571LL<<36),-real(33945LL<<32),reale(5963,0x471b5f51fec25LL),
      // C4[16], coeff of eps^21, polynomial in n of order 2
      real(24269LL<<36),-real(5831LL<<35),-real(11703LL<<31),
      reale(5963,0x471b5f51fec25LL),
      // C4[16], coeff of eps^20, polynomial in n of order 3
      -real(224895LL<<36),-real(32277LL<<37),real(111531LL<<36),
      -real(139825LL<<34),reale(41742,0xf1bf9b3df7503LL),
      // C4[16], coeff of eps^19, polynomial in n of order 4
      real(978405LL<<37),-real(1674813LL<<35),real(162197LL<<36),
      real(29281LL<<35),-real(297087LL<<31),reale(41742,0xf1bf9b3df7503LL),
      // C4[16], coeff of eps^18, polynomial in n of order 5
      -real(15263501LL<<36),-real(3038189LL<<38),real(24413445LL<<36),
      -real(10587549LL<<37),real(10822455LL<<36),-real(41181917LL<<32),
      reale(709630,0xdb94f1d6c533LL),
      // C4[16], coeff of eps^17, polynomial in n of order 6
      -real(7565085LL<<36),real(16306961LL<<35),-real(3541967LL<<37),
      real(9518487LL<<35),-real(2301919LL<<36),real(1408637LL<<35),
      -real(3231579LL<<31),reale(101375,0xb8d15471eae75LL),
      // C4[16], coeff of eps^16, polynomial in n of order 7
      -real(57998985LL<<33),real(52955595LL<<34),-real(165927531LL<<33),
      real(55309177LL<<35),-real(246030477LL<<33),real(108465049LL<<34),
      -real(128185967LL<<33),real(278992987LL<<30),
      reale(709630,0xdb94f1d6c533LL),
      // C4[17], coeff of eps^23, polynomial in n of order 0
      -real(1121LL<<31),real(0x6ef59e61feaaea7LL),
      // C4[17], coeff of eps^22, polynomial in n of order 1
      -real(59LL<<37),-real(309LL<<32),real(0x14ce0db25fc00bf5LL),
      // C4[17], coeff of eps^21, polynomial in n of order 2
      -real(10703LL<<36),real(30413LL<<35),-real(148003LL<<31),
      reale(6324,0xb043d1b40e32fLL),
      // C4[17], coeff of eps^20, polynomial in n of order 3
      -real(177777LL<<38),real(15715LL<<39),real(4277LL<<38),
      -real(68103LL<<33),reale(44272,0xd1dabbec63649LL),
      // C4[17], coeff of eps^19, polynomial in n of order 4
      -real(407783LL<<37),real(2775087LL<<35),-real(1157751LL<<36),
      real(1167621LL<<35),-real(4428011LL<<31),reale(44272,0xd1dabbec63649LL),
      // C4[17], coeff of eps^18, polynomial in n of order 5
      real(1580535LL<<37),-real(334719LL<<39),real(882049LL<<37),
      -real(210231LL<<38),real(127323LL<<37),-real(580027LL<<32),
      reale(44272,0xd1dabbec63649LL),
      // C4[17], coeff of eps^17, polynomial in n of order 6
      real(801009LL<<36),-real(2422805LL<<35),real(785323LL<<37),
      -real(3419955LL<<35),real(1485435LL<<36),-real(1740081LL<<35),
      real(7540351LL<<31),reale(44272,0xd1dabbec63649LL),
      // C4[18], coeff of eps^23, polynomial in n of order 0
      -real(89LL<<35),real(0x3351994085c8a607LL),
      // C4[18], coeff of eps^22, polynomial in n of order 1
      real(763LL<<36),-real(1809LL<<33),real(0x15fe66403955fe03LL),
      // C4[18], coeff of eps^21, polynomial in n of order 2
      real(91LL<<39),real(35LL<<38),-real(235LL<<34),
      real(0x15fe66403955fe03LL),
      // C4[18], coeff of eps^20, polynomial in n of order 3
      real(667755LL<<37),-real(269591LL<<38),real(268793LL<<37),
      -real(508305LL<<34),reale(46802,0xb1f5dc9acf78fLL),
      // C4[18], coeff of eps^19, polynomial in n of order 4
      -real(51319LL<<40),real(132867LL<<38),-real(31255LL<<39),
      real(18753LL<<38),-real(42441LL<<34),reale(15600,0xe5fc9ede45285LL),
      // C4[18], coeff of eps^18, polynomial in n of order 5
      -real(1198615LL<<36),real(378917LL<<38),-real(1619009LL<<36),
      real(693861LL<<37),-real(806379LL<<36),real(1740081LL<<33),
      reale(46802,0xb1f5dc9acf78fLL),
      // C4[19], coeff of eps^23, polynomial in n of order 0
      -real(983LL<<35),real(0x3617bd362c26857dLL),
      // C4[19], coeff of eps^22, polynomial in n of order 1
      real(1LL<<46),-real(189LL<<37),reale(2596,0x737a284739077LL),
      // C4[19], coeff of eps^21, polynomial in n of order 2
      -real(473LL<<40),real(467LL<<39),-real(3525LL<<34),
      real(0x172ebece12ebf011LL),
      // C4[19], coeff of eps^20, polynomial in n of order 3
      real(2379LL<<41),-real(553LL<<42),real(329LL<<41),-real(2961LL<<35),
      reale(2596,0x737a284739077LL),
      // C4[19], coeff of eps^19, polynomial in n of order 4
      real(2405LL<<41),-real(10101LL<<39),real(4277LL<<40),-real(4935LL<<39),
      real(42441LL<<34),reale(2596,0x737a284739077LL),
      // C4[20], coeff of eps^23, polynomial in n of order 0
      -real(1LL<<38),real(0x1f5feefdb1f0c4fLL),
      // C4[20], coeff of eps^22, polynomial in n of order 1
      real(379LL<<42),-real(357LL<<40),reale(2729,0x9a383778d2ed9LL),
      // C4[20], coeff of eps^21, polynomial in n of order 2
      -real(249LL<<43),real(147LL<<42),-real(329LL<<38),
      reale(2729,0x9a383778d2ed9LL),
      // C4[20], coeff of eps^20, polynomial in n of order 3
      -real(4797LL<<40),real(2009LL<<41),-real(2303LL<<40),real(4935LL<<37),
      reale(2729,0x9a383778d2ed9LL),
      // C4[21], coeff of eps^23, polynomial in n of order 0
      -real(1327LL<<38),reale(2862,0xc0f646aa6cd3bLL),
      // C4[21], coeff of eps^22, polynomial in n of order 1
      real(11LL<<44),-real(49LL<<39),real(0x3ba4052178e24469LL),
      // C4[21], coeff of eps^21, polynomial in n of order 2
      real(473LL<<43),-real(539LL<<42),real(2303LL<<38),
      reale(2862,0xc0f646aa6cd3bLL),
      // C4[22], coeff of eps^23, polynomial in n of order 0
      -real(1LL<<41),real(0x5ac8f5f3162ebfdLL),
      // C4[22], coeff of eps^22, polynomial in n of order 1
      -real(23LL<<43),real(49LL<<40),real(0x1105ae1d9428c3f7LL),
      // C4[23], coeff of eps^23, polynomial in n of order 0
      real(1LL<<41),real(0xc5e28ed2c935abLL),
    };  // count = 2900
#elif GEOGRAPHICLIB_GEODESICEXACT_ORDER == 27
    static const real coeff[] = {
      // C4[0], coeff of eps^26, polynomial in n of order 0
      4654,real(327806325),
      // C4[0], coeff of eps^25, polynomial in n of order 1
      -331600,247203,real(5135632425LL),
      // C4[0], coeff of eps^24, polynomial in n of order 2
      -real(30660788480LL),real(15209307520LL),real(3757742824LL),
      real(0xbd65c2e6062dLL),
      // C4[0], coeff of eps^23, polynomial in n of order 3
      -real(0x4a56872d110LL),real(0x30d818a0d20LL),-real(0x183639ebbb0LL),
      real(0x1207973318dLL),real(0x472c0a3d3d1ee9LL),
      // C4[0], coeff of eps^22, polynomial in n of order 4
      -real(0x743607eea80LL),real(0x5536ade42a0LL),-real(0x37e9933c940LL),
      real(0x1bb15f964e0LL),real(469120197546LL),real(0x472c0a3d3d1ee9LL),
      // C4[0], coeff of eps^21, polynomial in n of order 5
      -real(0x1a80e82073690LL),real(0x1485d9e7af5c0LL),-real(0xf039fc9e8ff0LL),
      real(0x9d5f26153ce0LL),-real(0x4ddf0f750f50LL),real(0x39e793daa6ebLL),
      real(0xadde5e94360277dLL),
      // C4[0], coeff of eps^20, polynomial in n of order 6
      -real(0xe72f9d31220580LL),real(0xb817a196612bc0LL),
      -real(0x8e0a680913c900LL),real(0x67a3067b290a40LL),
      -real(0x43c43707776c80LL),real(0x217ef7b84400c0LL),
      real(0x83b895ad56e94LL),reale(16517,0x8519000aea763LL),
      // C4[0], coeff of eps^19, polynomial in n of order 7
      -real(0x5be35cb0a188d670LL),real(0x49fb9f6e0e1fa420LL),
      -real(0x3a970b1601b36050LL),real(0x2d0406e3051baec0LL),
      -real(0x20bde41e80026c30LL),real(0x155cea808b65d160LL),
      -real(0xa8bc4b2c853c610LL),real(0x7d3acd77deac86fLL),
      reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^18, polynomial in n of order 8
      -reale(2219,0x955c84d349100LL),real(0x6f523368eabed3a0LL),
      -real(0x58df9f4050ea48c0LL),real(0x45eb9b162449f0e0LL),
      -real(0x35736f4da3b86880LL),real(0x26bb8b2d01772220LL),
      -real(0x19350a3e2b857840LL),real(0xc6cd21a34a65f60LL),
      real(0x30a9f24aaae2862LL),reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^17, polynomial in n of order 9
      -reale(3520,0x86c418e66b430LL),reale(2768,0x78979286ec480LL),
      -reale(2191,0xabc9bb4d59ed0LL),real(0x6c38e96882e6a560LL),
      -real(0x54765a5d7300bb70LL),real(0x402d11108cfc5240LL),
      -real(0x2e4c264c23518e10LL),real(0x1e09e0cfb5ca8720LL),
      -real(0xec7bce3f9449ab0LL),real(0xaf0b9139605a58dLL),
      reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^16, polynomial in n of order 10
      -reale(6136,0x52223aecbfa00LL),reale(4597,0xf56d1171d1b00LL),
      -reale(3531,0xe10107f964800LL),reale(2747,0xc7a53bf3c9500LL),
      -reale(2142,0x9c25bfa8f9600LL),real(0x677abbdfa4dcef00LL),
      -real(0x4e0ad45efdfc2400LL),real(0x37ff2b5bd74de900LL),
      -real(0x2432b6ddc0003200LL),real(0x11c5dbb8178f4300LL),
      real(0x4536f43fdb6a550LL),reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^15, polynomial in n of order 11
      -reale(13102,0xf96f6011eba70LL),reale(8724,0xbd02d5fc04060LL),
      -reale(6234,0x68dfd557291d0LL),reale(4636,0xd96d16348cb80LL),
      -reale(3525,0x47255186b7b30LL),reale(2702,0xc781c601a46a0LL),
      -reale(2062,0x7b91b55fb7290LL),real(0x60521f1f549575c0LL),
      -real(0x44a70474ce1373f0LL),real(0x2c2e0084319d1ce0LL),
      -real(0x15a2a473a1b17b50LL),real(0xff41fd49dab95d3LL),
      reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^14, polynomial in n of order 12
      -reale(63391,0x70a4897dc9e80LL),reale(23343,0xc5a3f9fbbcce0LL),
      -reale(13453,0x278d24cdf3ac0LL),reale(8911,0x777a0315423a0LL),
      -reale(6323,0x2714f8a7fff00LL),reale(4656,0xe8c5e07109660LL),
      -reale(3491,0x6be5fd90e340LL),reale(2621,0xb84b17c4ad20LL),
      -real(0x78f908534453df80LL),real(0x55814182d129efe0LL),
      -real(0x36b7bc0c02deebc0LL),real(0x1ab5b755becbe6a0LL),
      real(0x672760e43e7e5beLL),reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^13, polynomial in n of order 13
      reale(112706,0xdfd869d806ed0LL),reale(29093,0xf8d3fc140cbc0LL),
      -reale(65760,0x7b52c14019950LL),reale(24105,0xa651ba0482d20LL),
      -reale(13822,0xd4286a2c4c370LL),reale(9095,0xad3608e2bd280LL),
      -reale(6394,0x2414e7ceec390LL),reale(4646,0x4bdec656d47e0LL),
      -reale(3413,0x76099d6b04db0LL),reale(2482,0x54f2fd0561940LL),
      -real(0x6c7d891fb0df15d0LL),real(0x44efe2727b65d2a0LL),
      -real(0x2183dc0de2efcff0LL),real(0x189262ba581c6bf1LL),
      reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^12, polynomial in n of order 14
      reale(22421,0x80a7495217980LL),-reale(122681,0x25b6cd6074ac0LL),
      reale(117806,0x7498b0aecaf00LL),reale(29700,0x9de1e174ab0c0LL),
      -reale(68413,0x428634ee0fb80LL),reale(24937,0xf2aac2170b440LL),
      -reale(14209,0x4f5514d0cb600LL),reale(9268,0x742c2dd2c8fc0LL),
      -reale(6433,0x2286f06b3b080LL),reale(4585,0x3348b70941340LL),
      -reale(3266,0x3bda622d31b00LL),reale(2252,0x1340649a90ec0LL),
      -real(0x589f5d02f1d02580LL),real(0x2adce3e44e715240LL),
      real(0xa36591ccc5a22bcLL),reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^11, polynomial in n of order 15
      real(0x3845a63e874b7f90LL),reale(2990,0x790a9d44cfaa0LL),
      reale(23275,0xc0709755ecab0LL),-reale(127863,0x516b98584c9c0LL),
      reale(123656,0x74905ab09b3d0LL),reale(30291,0xc8698ff57f9e0LL),
      -reale(71410,0x2ebef8806f110LL),reale(25848,0x521bca14dd980LL),
      -reale(14605,0xac6deef7d4ff0LL),reale(9413,0x816443bfd6920LL),
      -reale(6415,0x315eed8f094d0LL),reale(4438,0xfed32587f3cc0LL),
      -reale(3002,0xabba02cdaebb0LL),real(0x74ba3cd78aa5e860LL),
      -real(0x3812b2b32b2f8090LL),real(0x28bab2d4ac11f317LL),
      reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^10, polynomial in n of order 16
      real(0xbcd4fd6df5b2600LL),real(0x17fed2a1d906c020LL),
      real(0x3a338f7e05a82540LL),reale(3102,0x8ee9d52fa7060LL),
      reale(24235,0xac0c2ca98fc80LL),-reale(133761,0xdb81f4d32fb60LL),
      reale(130458,0x34533ae1a43c0LL),reale(30833,0xcd61b102f94e0LL),
      -reale(74830,0xb3a54c3df6d00LL),reale(26842,0xad19affdd3920LL),
      -reale(14996,0x635b9e8c37dc0LL),reale(9500,0x408e4569f0960LL),
      -reale(6294,0x8e3c24f515680LL),reale(4143,0x97d5a30101da0LL),
      -reale(2534,0x56aa081845f40LL),real(0x4b644b6e4da18de0LL),
      real(0x11925bb6ba64765aLL),reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^9, polynomial in n of order 17
      real(0x3fcae6c51cf8fd0LL),real(0x6afa1c71c2ac100LL),
      real(0xc2892977602fa30LL),real(0x18cb840e0ff332e0LL),
      real(0x3c56602ddecd9290LL),reale(3228,0x26f051b5c20c0LL),
      reale(25324,0xf8a24438674f0LL),-reale(140558,0x5b2d711d11960LL),
      reale(138496,0xa2474d581bd50LL),reale(31265,0x7dd7c9350e080LL),
      -reale(78781,0x407f0fc917850LL),reale(27920,0xd85d0c9896a60LL),
      -reale(15347,0xbd51776ab0ff0LL),reale(9468,0xaa167d507e040LL),
      -reale(5981,0xcd152be8bed90LL),reale(3570,0xf062f37e99e20LL),
      -real(0x68dc53d94dbff530LL),real(0x4ae92c9a7a683bf5LL),
      reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^8, polynomial in n of order 18
      real(0x1b54ebcbbde1f00LL),real(0x2947b9527677980LL),
      real(0x415d003e7b1b800LL),real(0x6df9566e0623680LL),
      real(0xc8ad7ddfed65100LL),real(0x19abdc3c4555e380LL),
      real(0x3eb74cbd79d9ca00LL),reale(3370,0x20d152b7a6080LL),
      reale(26575,0x8086d641a0300LL),-reale(148506,0xeae36b607280LL),
      reale(148190,0x3f5dc7314dc00LL),reale(31472,0x41aaeb33d4a80LL),
      -reale(83406,0xf30366e47cb00LL),reale(29065,0x630b32b837780LL),
      -reale(15585,0x2764a1e4e1200LL),reale(9192,0xabf11a369f480LL),
      -reale(5286,0x3613c4b401900LL),reale(2436,0x784ea73c0a180LL),
      real(0x2209232c3cc4cca8LL),reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^7, polynomial in n of order 19
      real(0xd73a52d8bd1790LL),real(0x13078939da8f2e0LL),
      real(0x1bc62bcb4923530LL),real(0x2a1bb9d3adccf00LL),
      real(0x42f03cdd160e0d0LL),real(0x711670ab4ed8b20LL),
      real(0xcf3f2963eb3be70LL),real(0x1aa1c278c7668b40LL),
      real(0x416120b2cbe67210LL),reale(3532,0x3a6649f1d3360LL),
      reale(28031,0x35f5ca2c79fb0LL),-reale(157970,0xd11b280f51880LL),
      reale(160182,0x9c904f3daeb50LL),reale(31228,0xe702b02a70ba0LL),
      -reale(88907,0xf3445bc050710LL),reale(30210,0xe03f62b8103c0LL),
      -reale(15533,0x7a0f6ace49370LL),reale(8379,0xc089c57da33e0LL),
      -reale(3746,0x32a85741515d0LL),reale(2585,0x396e1f38f6dbbLL),
      reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^6, polynomial in n of order 20
      real(0x73457ae9fefc80LL),real(0x9bfefa36a68d60LL),
      real(0xd7e57b2fb0d740LL),real(0x132c60dd72bf720LL),
      real(0x1c1d29144004a00LL),real(0x2ad464b0fcdcce0LL),
      real(0x446dc104a967cc0LL),real(0x7436e717eb8b6a0LL),
      real(0xd626d1c40bc9780LL),real(0x1badddc640275c60LL),
      real(0x445f879c8f67c240LL),reale(3719,0x5820c25fe6620LL),
      reale(29754,0xa45b204c52500LL),-reale(169504,0xe227b2d578420LL),
      reale(175522,0xa8a2f18c5e7c0LL),reale(30060,0x7f96216b245a0LL),
      -reale(95556,0xca707dfd4cd80LL),reale(31150,0x37da9e0a66b60LL),
      -reale(14734,0x203a74e6dd2c0LL),reale(6239,0x114e25ea99520LL),
      real(0x4f113ff5b79764b6LL),reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^5, polynomial in n of order 21
      real(0x40c53da188eed0LL),real(0x54ed187b34c440LL),
      real(0x7146df082c9bb0LL),real(0x9a154e844696a0LL),
      real(0xd666e59b550690LL),real(0x13262a46ef0dd00LL),
      real(0x1c3f2cd359b1b70LL),real(0x2b4dcc62e91c360LL),
      real(0x45a57497f9cc650LL),real(0x771c08f5a9775c0LL),
      real(0xdd1a4961392f330LL),real(0x1ccccddd60de2020LL),
      real(0x47bbc762b5878e10LL),reale(3937,0xc2066e54dee80LL),
      reale(31838,0x13ce9b56b82f0LL),-reale(183990,0x8ea49a06f320LL),
      reale(196055,0x20a74184cbdd0LL),reale(26856,0x50de39af9a740LL),
      -reale(103681,0x9284ca213d550LL),reale(31195,0x5686bd94fe9a0LL),
      -reale(11739,0xecc6d600c4a70LL),reale(7362,0xc12f75a94f319LL),
      reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^4, polynomial in n of order 22
      real(0x25018b34093680LL),real(0x2f66db340747c0LL),
      real(0x3d8eaf55c4d300LL),real(0x512efdf6054640LL),
      real(0x6cf4c335af0f80LL),real(0x952f237cecdcc0LL),
      real(0xd10b7e4cd0dc00LL),real(0x12cf85d69a3fb40LL),
      real(0x1bf83185acb2880LL),real(0x2b3ea99410c91c0LL),
      real(0x462f30f09fee500LL),real(0x7931c8e1f8c9040LL),
      real(0xe34caff0bb50180LL),real(0x1def0c2db115e6c0LL),
      real(0x4b7080401d466e00LL),reale(4194,0xbf682a6ae8540LL),
      reale(34423,0x2600aa7441a80LL),-reale(202943,0xe8d9bbd87a440LL),
      reale(225378,0x7bd3e279ef700LL),reale(18574,0x52c9633395a40LL),
      -reale(113350,0xffc66a8300c80LL),reale(27528,0x198b9d86370c0LL),
      reale(3947,0xb3131e15c994LL),reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^3, polynomial in n of order 23
      real(0x14ba9dec234d90LL),real(0x1a15f878f54920LL),
      real(0x2134b5fb572db0LL),real(0x2acf89c87d75c0LL),
      real(0x37fb978513cbd0LL),real(0x4a626dbdd79a60LL),
      real(0x64a2becb8c9bf0LL),real(0x8afd5ca732eb00LL),
      real(0xc4970cf56e1210LL),real(0x11deb4357fc9ba0LL),
      real(0x1add3c5ff77a230LL),real(0x2a08c939311e040LL),
      real(0x451c5af5bb5c050LL),real(0x7909ad73ef1ece0LL),
      real(0xe685850971be070LL),real(0x1edeb97922aff580LL),
      real(0x4f3a8e20463e7690LL),reale(4494,0x6f4eb7a652e20LL),
      reale(37733,0xf376431ecf6b0LL),-reale(229273,0xd3dfdae1d3540LL),
      reale(271637,0x92a93446bd4d0LL),-reale(5667,0x8cc9ebb9c00a0LL),
      -reale(121042,0xac8f4eff17b10LL),reale(39799,0x5b8561a065b3fLL),
      reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^2, polynomial in n of order 24
      real(0xab22c89592500LL),real(0xd46ccddd414a0LL),real(0x10a4eb8f1ddb40LL),
      real(0x15184ab619d7e0LL),real(0x1b0f2efb81a980LL),
      real(0x232d3128e64f20LL),real(0x2e6a3ee43c47c0LL),
      real(0x3e471bedb3b260LL),real(0x552919f15d6e00LL),
      real(0x7700089e6e39a0LL),real(0xaa7eb4de50d440LL),
      real(0xfb834e2f281ce0LL),real(0x1801af760623280LL),
      real(0x263a4a7c48d9420LL),real(0x401905d594140c0LL),
      real(0x72c2e250398d760LL),real(0xe012c263c05b700LL),
      real(0x1edcfb1205061ea0LL),real(0x51c797f92b334d40LL),
      reale(4810,0x460394707a1e0LL),reale(42101,0xccb76963dbb80LL),
      -reale(269613,0x72aa3b84666e0LL),reale(357865,0x4c16ffd0cb9c0LL),
      -reale(115779,0xf2f861d29c3a0LL),-reale(21708,0xbd8e92577d4aeLL),
      reale(1139708,0xdfbd02f131dafLL),
      // C4[0], coeff of eps^1, polynomial in n of order 25
      real(0x16b98c18c43f0LL),real(0x1be76827efc80LL),real(0x2291674649910LL),
      real(0x2b3d2747a6820LL),real(0x36a8d2fdcc830LL),real(0x45e795ad137c0LL),
      real(0x5a8eeaa036550LL),real(0x77007a4bcbf60LL),real(0x9ee5aa2960470LL),
      real(0xd8045ac825300LL),real(0x12bb93df5b3990LL),
      real(0x1a9b1c398546a0LL),real(0x26d2a92f5c98b0LL),
      real(0x3a7858f998ee40LL),real(0x5b6e62f9c0b5d0LL),
      real(0x959d5c24529de0LL),real(0x102f2d0b50524f0LL),
      real(0x1e1472bfb1ba980LL),real(0x3d69bf9cb587a10LL),
      real(0x8ee1210e8c36520LL),real(0x194d332fe8d44930LL),
      real(0x6534ccbfa35124c0LL),reale(15788,0x2cc4c78572650LL),
      -reale(115779,0xf2f861d29c3a0LL),reale(173669,0xec7492bbea570LL),
      -reale(75980,0x9773003236861LL),reale(379902,0xf53f00fb109e5LL),
      // C4[0], coeff of eps^0, polynomial in n of order 26
      real(0x104574695550b58LL),real(0x124efd1ef41bc1cLL),
      real(0x14b36c04f5f7ca0LL),real(0x1787788b9792f24LL),
      real(0x1ae5caaf52545e8LL),real(0x1ef111702bafd2cLL),
      real(0x23d6fb7cfc3d530LL),real(0x29d483e08118c34LL),
      real(0x313c47ee86cd878LL),real(0x3a800de5bbb223cLL),
      real(0x463f6a859617dc0LL),real(0x555ed8909112544LL),
      real(0x692d2b9362db308LL),real(0x83a245a495f5b4cLL),
      real(0xa7cc0a01a036650LL),real(0xda93e49d10b2a54LL),
      real(0x1243757f6f15c598LL),real(0x193422259e6ad85cLL),
      real(0x24309a0ea1d47ee0LL),real(0x36b22ea791accb64LL),
      real(0x588e3327aee70028LL),reale(2530,0x27feb6f2ec96cLL),
      reale(5262,0xb996ed2c7b770LL),reale(14472,0x7e5f0c3a53874LL),
      reale(86834,0xf63a495df52b8LL),-reale(303922,0x5dcc00c8da184LL),
      reale(759805,0xea7e01f6213caLL),reale(1139708,0xdfbd02f131dafLL),
      // C4[1], coeff of eps^26, polynomial in n of order 0
      4654,real(327806325),
      // C4[1], coeff of eps^25, polynomial in n of order 1
      real(22113584),5520955,real(0xf784431927LL),
      // C4[1], coeff of eps^24, polynomial in n of order 2
      real(29556996608LL),-real(15922652416LL),real(11273228472LL),
      real(0x2383148b21287LL),
      // C4[1], coeff of eps^23, polynomial in n of order 3
      real(0x165661ad6b70LL),-real(0x1009b31cabe0LL),real(0x7444963bdd0LL),
      real(0x1d0511c64f5LL),real(0x42b94999694cfa7LL),
      // C4[1], coeff of eps^22, polynomial in n of order 4
      real(696434041088LL),-real(561462728640LL),real(334369174656LL),
      -real(182661157184LL),real(127941872058LL),real(0x13691a10b39411LL),
      // C4[1], coeff of eps^21, polynomial in n of order 5
      real(0x2b50c847e5bec70LL),-real(0x25172ad2adc8640LL),
      real(0x187490c86e06510LL),-real(0x11cf5b364679120LL),
      real(0x7e9f37da26e7b0LL),real(0x1f979b01bfd5e3LL),
      reale(227941,0xc6590096a3923LL),
      // C4[1], coeff of eps^20, polynomial in n of order 6
      real(0x84a641c077c100LL),-real(0x75601a6b667780LL),
      real(0x51157a29d94600LL),-real(0x4247925ad10480LL),
      real(0x269068d8c2ab00LL),-real(0x15748d5a64a980LL),
      real(0xed190d6b360a4LL),reale(29731,0x892d0013a607fLL),
      // C4[1], coeff of eps^19, polynomial in n of order 7
      real(0x57e3d5e3e8a64d50LL),-real(0x4ee151925712ac60LL),
      real(0x379f60f9d8160ef0LL),-real(0x3036f6417460ec40LL),
      real(0x1eece80c1c746690LL),-real(0x16f21d696f523420LL),
      real(0x9ef6bfafd871830LL),real(0x27a3f6720674fabLL),
      reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^18, polynomial in n of order 8
      reale(2128,0x469250df87e00LL),-real(0x76ff6f2ca68ee740LL),
      real(0x544ea56af984a280LL),-real(0x4b3b3c5b1f3b3dc0LL),
      real(0x324e822f05811f00LL),-real(0x29dd8ae6f4502040LL),
      real(0x179c3b6434632b80LL),-real(0xd7628385c5d56c0LL),
      real(0x91fdd6e000a7926LL),reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^17, polynomial in n of order 9
      reale(3396,0xc29d3f547be10LL),-reale(2963,0x6657b77d7b180LL),
      reale(2082,0xa3af2d55cd2f0LL),-real(0x74e3fc23ed074b20LL),
      real(0x4f51e11c0cc64dd0LL),-real(0x45cc62cad46028c0LL),
      real(0x2b210825284d5ab0LL),-real(0x20cfde05bc67de60LL),
      real(0xdb6584e22cc2590LL),real(0x36aae0ede944991LL),
      reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^16, polynomial in n of order 10
      reale(5994,0xfab7bd428a400LL),-reale(4919,0xd8955c3980a00LL),
      reale(3376,0x641d9d71fd000LL),-reale(2975,0x320d339261600LL),
      real(0x7dd1b5a4fb9ffc00LL),-real(0x712cdc1424704200LL),
      real(0x486493a43f86e800LL),-real(0x3daeb06e6a40ce00LL),
      real(0x21506b8426325400LL),-real(0x13a656589a61fa00LL),
      real(0xcfa4dcbf923eff0LL),reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^15, polynomial in n of order 11
      reale(13117,0x6cbddabc52ed0LL),-reale(9318,0xa8f3ea9b44c20LL),
      reale(6040,0x7b2fdab4ba7f0LL),-reale(5022,0x22b8983435e80LL),
      reale(3330,0x281af37e2710LL),-reale(2968,0x456e895a2c0e0LL),
      real(0x7764510336be0030LL),-real(0x6af4843f7d4f5f40LL),
      real(0x3eba1ed514e18750LL),-real(0x31669b90045c25a0LL),
      real(0x13a17c0101ce1070LL),real(0x4e2a88c78d66acfLL),
      reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^14, polynomial in n of order 12
      reale(68147,0x8cb1a33fbb300LL),-reale(25030,0x19a83b314d5c0LL),
      reale(13399,0xd5b954b9ffe80LL),-reale(9632,0x5ff7adc5b8740LL),
      reale(6058,0x6185fb910e200LL),-reale(5122,0x24f31e326fcc0LL),
      reale(3246,0x498e64bf8a580LL),-reale(2929,0xc60f539a7ee40LL),
      real(0x6e041fee5d419100LL),-real(0x60b53ba76d5f13c0LL),
      real(0x3113d4fc9085ec80LL),-real(0x1e6533c87b7d2540LL),
      real(0x1357622acbb7b13aLL),reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^13, polynomial in n of order 13
      -reale(121532,0xe4514e2bd7670LL),-reale(15940,0x17553143d1340LL),
      reale(71019,0xc50f40d0125f0LL),-reale(26120,0x5d81b142df60LL),
      reale(13667,0x35bfe1bb73850LL),-reale(9984,0xe4f4c1c8f9780LL),
      reale(6033,0x4bb2ec6997cb0LL),-reale(5212,0x5459006443fa0LL),
      reale(3108,0x7a1250dedaf10LL),-reale(2836,0xbc55f0b59dbc0LL),
      real(0x605fcd3581f88b70LL),-real(0x4fb9f3b2da8b6fe0LL),
      real(0x1d6444fcd70bcdd0LL),real(0x74c81d1452803b5LL),
      reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^12, polynomial in n of order 14
      -reale(18279,0x4105927635f00LL),reale(111436,0xf9c78acad1e80LL),
      -reale(127455,0xb83d096a36600LL),-reale(14599,0xb6308ef406280LL),
      reale(74253,0x38e0bbebab300LL),-reale(27394,0x6661a055a9b80LL),
      reale(13898,0x35bd350d73c00LL),-reale(10384,0x95909b51f3c80LL),
      reale(5941,0x73f13b5b28500LL),-reale(5277,0x6484894bf580LL),
      reale(2891,0x688dd5accde00LL),-reale(2646,0x1bce07b5e7680LL),
      real(0x4c6028727ac69700LL),-real(0x32eae1a8c2946f80LL),
      real(0x1ea30b56650e6834LL),reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^11, polynomial in n of order 15
      -real(0x26534490cad1dfb0LL),-reale(2194,0x14a85ebaf95e0LL),
      -reale(18676,0x98f19d91af310LL),reale(115088,0x35b741cc34140LL),
      -reale(134245,0x8207aed455070LL),-reale(12735,0xf52bb5c1fbfa0LL),
      reale(77916,0x32c371fd8ec30LL),-reale(28918,0xb36d158cbf480LL),
      reale(14055,0x84fcc4e4ea6d0LL),-reale(10840,0xa60c8c5d6b960LL),
      reale(5745,0xafd650291c370LL),-reale(5282,0xabba6463d6a40LL),
      reale(2556,0x876a7d9212610LL),-reale(2272,0x615ae9eab6320LL),
      real(0x2e7aab3dc406b2b0LL),real(0xb7e588c69951913LL),
      reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^10, polynomial in n of order 16
      -real(0x6ec9ec72fa83400LL),-real(0xee6121f9ed5ac40LL),
      -real(0x2698258da225a980LL),-reale(2223,0x82921a72280c0LL),
      -reale(19088,0x4fb95e6188700LL),reale(119080,0xff5c72a1c6ec0LL),
      -reale(142117,0x7c3deb03b7480LL),-reale(10117,0x6e8319b8485c0LL),
      reale(82086,0xede392256e600LL),-reale(30795,0xed5c849e10640LL),
      reale(14073,0x47ff3f3e080LL),-reale(11359,0x76d81b264bac0LL),
      reale(5387,0x791e9eab0d300LL),-reale(5153,0xcdddc38eb4b40LL),
      real(0x7fb4f5b53eb31580LL),-real(0x5fcfbdbbdde05fc0LL),
      real(0x34b713242f2d630eLL),reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^9, polynomial in n of order 17
      -real(0x20f38bbaca812f0LL),-real(0x39b499036d51b00LL),
      -real(0x6e4d3364d687b10LL),-real(0xee56650d93fe5a0LL),
      -real(0x26cbb66f58b91d30LL),-reale(2250,0xe985ef9ea8440LL),
      -reale(19510,0x3134f0f32ad50LL),reale(123456,0xc66bc06159520LL),
      -reale(151362,0xfafa005fcdf70LL),-reale(6379,0xaa0075c90d80LL),
      reale(86843,0xd7e050f079870LL),-reale(33196,0x7f1161b25e020LL),
      reale(13831,0x3ac1850370650LL),-reale(11930,0x8d19c5e9856c0LL),
      reale(4775,0x36871b380b630LL),-reale(4708,0xdfb0fde91e560LL),
      real(0x4e466dbc0d5cf410LL),real(0x132845ea2b7be139LL),
      reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^8, polynomial in n of order 18
      -real(0xcaab4ddd8d4600LL),-real(0x13c31d1cbb16d00LL),
      -real(0x207a98d99de3000LL),-real(0x390c3dedd68b300LL),
      -real(0x6d71551ca261a00LL),-real(0xed90e825b918900LL),
      -real(0x26e62c786e462400LL),-reale(2274,0xbbaf6c5e10f00LL),
      -reale(19934,0x1db266a5f6e00LL),reale(128254,0x3ade3c4739b00LL),
      -reale(162383,0xab3413f131800LL),-real(0x3992c873ce48ab00LL),
      reale(92230,0x4a4593a3dbe00LL),-reale(36418,0x345102e4b0100LL),
      reale(13110,0x864dfe531f400LL),-reale(12475,0xa3edd9488700LL),
      reale(3771,0xc13fa20286a00LL),-reale(3469,0x365d076765d00LL),
      real(0x661b6984b64e65f8LL),reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^7, polynomial in n of order 19
      -real(0x5b1678b2b96e30LL),-real(0x83e7d604d6e1a0LL),
      -real(0xc5c1bd21f06210LL),-real(0x135402446a1f500LL),
      -real(0x1fd9e061288aff0LL),-real(0x381fb1c2d0ea860LL),
      -real(0x6c176a9d32ee3d0LL),-real(0xebcbb379725c7c0LL),
      -real(0x26dc285f96da89b0LL),-reale(2292,0x8c4f779be1f20LL),
      -reale(20344,0xed4bfa0642d90LL),reale(133496,0x33ba4ee858580LL),
      -reale(175742,0x64c709ffb5b70LL),reale(7288,0xff81f26b85a20LL),
      reale(98139,0x5735ff04360b0LL),-reale(41010,0x6c5dc3c9a6d40LL),
      reale(11505,0xfe66ab587ad0LL),-reale(12646,0x14c7a4cad9ca0LL),
      reale(2204,0x9aaf76ecb66f0LL),real(0x2076d1ad78dbacf7LL),
      reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^6, polynomial in n of order 20
      -real(0x2d4d049c656700LL),-real(0x3e4af5e8d022c0LL),
      -real(0x57ced7fe851580LL),-real(0x7f7034131ef240LL),
      -real(0xbf83d85dea6c00LL),-real(0x12c465612feb5c0LL),
      -real(0x1f04ac518a30280LL),-real(0x36d88216b840540LL),
      -real(0x6a13494183c7100LL),-real(0xe8a2e478ed378c0LL),
      -real(0x269ca36792944f80LL),-reale(2300,0x7badf4501a840LL),
      -reale(20714,0x7015050283600LL),reale(139156,0x8278406ccd440LL),
      -reale(192233,0x29cb54965bc80LL),reale(20133,0xdb20ab18364c0LL),
      reale(103930,0xc444b13858500LL),-reale(48022,0x859c77e028ec0LL),
      reale(8312,0x1287962dbf680LL),-reale(10954,0x169105fd99e40LL),
      reale(3795,0x3bfe126c62e22LL),reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^5, polynomial in n of order 21
      -real(0x1802918882e770LL),-real(0x1fcd949a6860c0LL),
      -real(0x2aeab9b7d2f010LL),-real(0x3b2acc792185e0LL),
      -real(0x539feddcdda2b0LL),-real(0x79b43080aca700LL),
      -real(0xb76e50170e2350LL),-real(0x1207f374f78a820LL),
      -real(0x1de74f0a09e95f0LL),-real(0x351484156246d40LL),
      -real(0x6722781c7da1e90LL),-real(0xe37fba15ed8da60LL),
      -real(0x260d3a8a453ee130LL),-reale(2292,0x258c84a62d380LL),
      -reale(20989,0x3411bcc4001d0LL),reale(145073,0x9b58d1932c360LL),
      -reale(212947,0x443e0cc67a470LL),reale(41274,0x9a63d1cc50640LL),
      reale(107042,0xff9bf7f6712f0LL),-reale(59294,0xf496954c0eee0LL),
      reale(2833,0xc664f5dce0050LL),real(0x17b85ffcea47049dLL),
      reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^4, polynomial in n of order 22
      -real(0xd20723e198100LL),-real(0x10e999b2026480LL),
      -real(0x161c2993f30e00LL),-real(0x1d62585afd4f80LL),
      -real(0x27ca0dc8a2fb00LL),-real(0x370cc97a8ce280LL),
      -real(0x4e170b46a3d800LL),-real(0x7213d21df5ad80LL),
      -real(0xac9b82d7503500LL),-real(0x1109444f53c4080LL),
      -real(0x1c6019c5f02a200LL),-real(0x329a7eb49a52b80LL),
      -real(0x62d84097135af00LL),-real(0xdb6f2c88eb4fe80LL),
      -real(0x2502e63c01a3ec00LL),-reale(2256,0x8389e52b04980LL),
      -reale(21063,0xc2942f767e900LL),reale(150710,0x347c6ec646380LL),
      -reale(239155,0x111ed671c3600LL),reale(78297,0xeac3242447880LL),
      reale(97157,0xffcea47049d00LL),-reale(74487,0xcca6f58949a80LL),
      reale(11841,0x219395a415cbcLL),reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^3, polynomial in n of order 23
      -real(0x7207334f38cb0LL),-real(0x8fe6a0f540760LL),
      -real(0xb7c4f4df6c510LL),-real(0xedcd97a176940LL),
      -real(0x1384e0d9162770LL),-real(0x1a108f169c7320LL),
      -real(0x2378674e3fafd0LL),-real(0x3154606a2c6100LL),
      -real(0x465a9ded7c5a30LL),-real(0x675a79a8aa6ee0LL),
      -real(0x9d4a8ab99e2290LL),-real(0xf9e328cb49d8c0LL),
      -real(0x1a2ce594ece04f0LL),-real(0x2efbcc23543daa0LL),
      -real(0x5c688ee5939fd50LL),-real(0xceb90d2fccdb080LL),
      -real(0x2331240c282307b0LL),-reale(2173,0x456299e8e9660LL),
      -reale(20716,0x42df2018b2010LL),reale(154405,0x43613e2a37c0LL),
      -reale(270827,0xec43372c34270LL),reale(146546,0xa61bf3c2f7de0LL),
      reale(26313,0x9ff2a1de69530LL),-reale(32563,0x1c55db833bf05LL),
      reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^2, polynomial in n of order 24
      -real(0x39a9fc22d9600LL),-real(0x47a4ffa857140LL),
      -real(0x59ea353148580LL),-real(0x721982b3023c0LL),
      -real(0x9291e22ef9d00LL),-real(0xbeda9ea6fc240LL),
      -real(0xfc517cd616480LL),-real(0x1535335443d4c0LL),
      -real(0x1d14474c2c6400LL),-real(0x28c4706fdbe340LL),
      -real(0x3aa43e35a32380LL),-real(0x56eefde83775c0LL),
      -real(0x859522b6982b00LL),-real(0xd663f0e8861440LL),
      -real(0x16b2ad2884e0280LL),-real(0x2932441ccc746c0LL),
      -real(0x51f4ee722e73200LL),-real(0xb97e18f372a9540LL),
      -real(0x1ff5b9ebacd64180LL),-real(0x7d04fcecbaaf87c0LL),
      -reale(19431,0x998fba7cdb900LL),reale(150594,0xe619e547a59c0LL),
      -reale(294712,0x9903e1bb02080LL),reale(231559,0xe5f0c3a538740LL),
      -reale(65126,0x38abb70677e0aLL),reale(3419126,0x9f3708d39590dLL),
      // C4[1], coeff of eps^1, polynomial in n of order 25
      -real(0x16b98c18c43f0LL),-real(0x1be76827efc80LL),
      -real(0x2291674649910LL),-real(0x2b3d2747a6820LL),
      -real(0x36a8d2fdcc830LL),-real(0x45e795ad137c0LL),
      -real(0x5a8eeaa036550LL),-real(0x77007a4bcbf60LL),
      -real(0x9ee5aa2960470LL),-real(0xd8045ac825300LL),
      -real(0x12bb93df5b3990LL),-real(0x1a9b1c398546a0LL),
      -real(0x26d2a92f5c98b0LL),-real(0x3a7858f998ee40LL),
      -real(0x5b6e62f9c0b5d0LL),-real(0x959d5c24529de0LL),
      -real(0x102f2d0b50524f0LL),-real(0x1e1472bfb1ba980LL),
      -real(0x3d69bf9cb587a10LL),-real(0x8ee1210e8c36520LL),
      -real(0x194d332fe8d44930LL),-real(0x6534ccbfa35124c0LL),
      -reale(15788,0x2cc4c78572650LL),reale(115779,0xf2f861d29c3a0LL),
      -reale(173669,0xec7492bbea570LL),reale(75980,0x9773003236861LL),
      reale(3419126,0x9f3708d39590dLL),
      // C4[2], coeff of eps^26, polynomial in n of order 0
      2894476,real(0xfe89d46f33LL),
      // C4[2], coeff of eps^25, polynomial in n of order 1
      -8609536,5603312,real(590597728875LL),
      // C4[2], coeff of eps^24, polynomial in n of order 2
      -real(104352359168LL),real(40707880576LL),real(10376961584LL),
      real(0xb18f66b7a5ca3LL),
      // C4[2], coeff of eps^23, polynomial in n of order 3
      -real(0x265f8c17d00LL),real(0x13bddd35200LL),-real(871294451456LL),
      real(553528081392LL),real(0xa1c12e8b2dd1e3LL),
      // C4[2], coeff of eps^22, polynomial in n of order 4
      -real(0x46e25cf59280LL),real(0x290af5269020LL),-real(0x22f7c7b01940LL),
      real(0xd08f4d0d560LL),real(0x355c24081bcLL),real(0xc015674546693d9LL),
      // C4[2], coeff of eps^21, polynomial in n of order 5
      -real(0x326f6045f923c80LL),real(0x1fb1615f9d3a600LL),
      -real(0x1db1797638c1780LL),real(0xe9780531c07300LL),
      -real(0x9d24cc38e5d280LL),real(0x60cf9034bf3868LL),
      reale(379902,0xf53f00fb109e5LL),
      // C4[2], coeff of eps^20, polynomial in n of order 6
      -real(0x4837c78c0550480LL),real(0x313ba08613af040LL),
      -real(0x2ee33229a4bc300LL),real(0x1a152ee5f2ae9c0LL),
      -real(0x172de5252da0180LL),real(0x824fa762c0c340LL),
      real(0x2180172e018ad8LL),reale(379902,0xf53f00fb109e5LL),
      // C4[2], coeff of eps^19, polynomial in n of order 7
      -real(0x5fc4bec46509e480LL),real(0x48096a7e75900b00LL),
      -real(0x41caf1fb886dd580LL),real(0x28558a32a56ef200LL),
      -real(0x26dce3ddd1a42680LL),real(0x120433e2d2025900LL),
      -real(0xce36e1803df1780LL),real(0x7a135866f905bb8LL),
      reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^18, polynomial in n of order 8
      -reale(2176,0xe1585afea1500LL),real(0x73bced2a00a143a0LL),
      -real(0x5fca97395e84bfc0LL),real(0x418b4cd8fc5e04e0LL),
      -real(0x3e6c34ea7ddb8a80LL),real(0x212422dcacab1620LL),
      -real(0x1f0466b0c7211540LL),real(0xa12130d17045760LL),
      real(0x29b0aa486315dbcLL),reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^17, polynomial in n of order 9
      -reale(3194,0x3409f96190200LL),reale(3129,0x198ba10e3f000LL),
      -reale(2211,0xeca78927c1e00LL),real(0x6cf94ec7bfac7400LL),
      -real(0x5f04d2df84f0ba00LL),real(0x39318494ff85f800LL),
      -real(0x38939121c731d600LL),real(0x1854a6f7e2957c00LL),
      -real(0x12decef0b13a7200LL),real(0xa9861a018e14120LL),
      reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^16, polynomial in n of order 10
      -reale(5172,0xb8c4b33583a00LL),reale(5700,0x1d26bd0962f00LL),
      -reale(3248,0x8acf908fbc800LL),reale(3050,0xed985975b4100LL),
      -reale(2251,0xef96e32335600LL),real(0x6370a1a9e900d300LL),
      -real(0x5c955afee309e400LL),real(0x2eb3ea14003fe500LL),
      -real(0x2e844e36822a7200LL),real(0xd8a8b891f217700LL),
      real(0x388df4ca3a6fb20LL),reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^15, polynomial in n of order 11
      -reale(11115,0xb2ff91ec6c600LL),reale(11728,0x761e1ef822c00LL),
      -reale(5178,0x9a27d63f52200LL),reale(5773,0x24fd2adb2f000LL),
      -reale(3328,0x5f0c31c71fe00LL),reale(2908,0x836ab328fb400LL),
      -reale(2291,0x629d070485a00LL),real(0x5681ee23b9ad7800LL),
      -real(0x56cafdb120433600LL),real(0x21dbd9f992213c00LL),
      -real(0x1d4bdf01a76d9200LL),real(0xf4e0cbd04176b20LL),
      reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^14, polynomial in n of order 12
      -reale(73826,0x9e48c9be75880LL),reale(32637,0x887aa6de960e0LL),
      -reale(10940,0x9647b1447b9c0LL),reale(12348,0xdd9347a34b3a0LL),
      -reale(5206,0x461aa415f3b00LL),reale(5776,0x82c559a327660LL),
      -reale(3445,0x2b71b5ef13c40LL),reale(2676,0xdbe2bf3d4c920LL),
      -reale(2313,0x6c289eed11d80LL),real(0x45af1f46068fcbe0LL),
      -real(0x4a646c774fde3ec0LL),real(0x127e48f8affd9ea0LL),
      real(0x4e336f38ab11704LL),reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^13, polynomial in n of order 13
      reale(130976,0x1a84c1eb6d80LL),-reale(14597,0x4f1d8a91fc600LL),
      -reale(76483,0x6c58cf65980LL),reale(35388,0xd1bf338007b00LL),
      -reale(10663,0x1c210a8b78080LL),reale(13004,0x14f125ca37c00LL),
      -reale(5285,0x554d73733c780LL),reale(5660,0xa57467d557d00LL),
      -reale(3609,0xe9c5b2656ee80LL),reale(2326,0xf26507322be00LL),
      -reale(2274,0xe6ae0b8fcb580LL),real(0x30f364de4c777f00LL),
      -real(0x3139417308d0dc80LL),real(0x173bf41713ca3b88LL),
      reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^12, polynomial in n of order 14
      reale(12302,0xe52cc8d8c2180LL),-reale(90162,0x247de245423c0LL),
      reale(136898,0x7ace803b76f00LL),-reale(20188,0x482d40173de40LL),
      -reale(79167,0xe510d7fd7c380LL),reale(38835,0xfee0572864740LL),
      -reale(10270,0x4559a0d3b600LL),reale(13648,0x338b156f30cc0LL),
      -reale(5468,0x80042be36a880LL),reale(5349,0x619325bd73240LL),
      -reale(3821,0xffa84c59adb00LL),real(0x729df2a6c14b77c0LL),
      -reale(2073,0x93dcfbe928d80LL),real(0x193a4a0699e49d40LL),
      real(0x6c8a3fc264f2d98LL),reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^11, polynomial in n of order 15
      real(0x12b65c49560e1680LL),real(0x4c91348dd4c57d00LL),
      reale(12186,0xb870c2ef8b380LL),-reale(91199,0x47a39f34d9e00LL),
      reale(143440,0xa133e98363080LL),-reale(27237,0xaf8901f443900LL),
      -reale(81724,0x1b06c40663280LL),reale(43231,0xcee7486ccec00LL),
      -reale(9771,0xb47d34b793580LL),reale(14177,0x876b1df11100LL),
      -reale(5844,0x5970f546f9880LL),reale(4733,0x71ff0d3b37600LL),
      -reale(4034,0xaeeb7c4e61b80LL),real(0x4b0e043dd17f5b00LL),
      -real(0x5c6dac5851097e80LL),real(0x259ade3cf4689f28LL),
      reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^10, polynomial in n of order 16
      real(0x285b74a086cfe00LL),real(0x61629f583f6fc20LL),
      real(0x11e1f0840e822e40LL),real(0x4a2acb7177936860LL),
      reale(12009,0x162afd0a23e80LL),-reale(92025,0x51c6b64b59b60LL),
      reale(150657,0xe159fc0830ec0LL),-reale(36240,0x8903bcca1af20LL),
      -reale(83842,0x8f32e14ed8100LL),reale(48929,0x80db803df8d20LL),
      -reale(9247,0x4a711a73d90c0LL),reale(14370,0x3118e0d87960LL),
      -reale(6545,0xcfaa0092b4080LL),reale(3681,0xa71da4ef975a0LL),
      -reale(4055,0x6bd2ceb58b040LL),real(0x201a58611bc4e1e0LL),
      real(0x8ca8a9bec5eeb0cLL),reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^9, polynomial in n of order 17
      real(0x8f791b0d72f300LL),real(0x116eee5fb7db000LL),
      real(0x2544a69b0af6d00LL),real(0x5ae50a5c0f6ba00LL),
      real(0x10e6ab279c402700LL),real(0x472bda650b6c4400LL),
      reale(11750,0x4a89b28f5a100LL),-reale(92512,0x1ccd7f1613200LL),
      reale(158574,0x53a9410005b00LL),-reale(47896,0xbfb8d60312800LL),
      -reale(84919,0xb4a50d4cf2b00LL),reale(56401,0x32e93db7ce200LL),
      -reale(8956,0x3835fd4c87100LL),reale(13782,0xdee88bf296c00LL),
      -reale(7712,0x7aed9801af700LL),reale(2126,0x5791e5314f600LL),
      -reale(3273,0xe9400d1963d00LL),real(0x4230ff2c7e6defd0LL),
      reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^8, polynomial in n of order 18
      real(0x289b91a48ebf00LL),real(0x45ee5b14465380LL),
      real(0x7f92734c023800LL),real(0xfa5ad187871c80LL),
      real(0x21cddd2df61b100LL),real(0x5372a978dde2580LL),
      real(0xfbd02001ed7aa00LL),real(0x436e93187af7ee80LL),
      reale(11383,0x2dcd21f7ea300LL),-reale(92459,0xff89d11970880LL),
      reale(167131,0xf0a2167d11c00LL),-reale(63199,0x7fe973623f80LL),
      -reale(83766,0xa02debe66b00LL),reale(66187,0xcedf7a1cac980LL),
      -reale(9608,0xefbab691d7200LL),reale(11585,0x75dbe72dc9280LL),
      -reale(9220,0x22c92d6997900LL),real(0x18709d3bc0679b80LL),
      real(0x5b7e325c6742390LL),reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^7, polynomial in n of order 19
      real(0xd108e5f6f6100LL),real(0x14cfb44a7f1600LL),
      real(0x227bc5972bab00LL),real(0x3bea4dd1053000LL),
      real(0x6e5f06564db500LL),real(0xdaf2ed1ea74a00LL),
      real(0x1dec9104c41ff00LL),real(0x4ae6e1cc221e400LL),
      real(0xe5bde12a5950900LL),real(0x3ec229ad8ff17e00LL),
      reale(10869,0xc2e1de8335300LL),-reale(91550,0xfd5202ded6800LL),
      reale(176075,0x65a5499a95d00LL),-reale(83531,0x98920703e4e00LL),
      -reale(77994,0x11133349c5900LL),reale(78539,0xb0828e93b4c00LL),
      -reale(12981,0x6d9e1d7114f00LL),reale(6537,0x5c156837be600LL),
      -reale(9404,0xf97b75bc90500LL),reale(2071,0xc05f52f113a50LL),
      reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^6, polynomial in n of order 20
      real(0x4748ad3ff9e80LL),real(0x6b926f7e60d60LL),real(0xa71fa4085b840LL),
      real(0x10c991e0a3ab20LL),real(0x1c15b3b145b200LL),
      real(0x314f7c7c43f8e0LL),real(0x5be1ff458cabc0LL),
      real(0xb89930a80796a0LL),real(0x199734a3c07c580LL),
      real(0x411aa25f2292460LL),real(0xcb87e4542581f40LL),
      real(0x38e7a442bb914220LL),reale(10156,0x20944a9a6d900LL),
      -reale(89265,0x51d50a4f57020LL),reale(184683,0x63f792d3912c0LL),
      -reale(110680,0x89cae6d0a5260LL),-reale(62727,0xfdf47fc1380LL),
      reale(91791,0x3f8035a7d3b60LL),-reale(22895,0xcc844c9bf79c0LL),
      -real(0x5652aea374b626e0LL),-real(0x38edb32bcbdda4acLL),
      reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^5, polynomial in n of order 21
      real(0x185346b40be80LL),real(0x234a30239ea00LL),real(0x345f5bcfbb580LL),
      real(0x4fc2f91719900LL),real(0x7d257d9ac0c80LL),real(0xcb49d34f58800LL),
      real(0x1580c944df8380LL),real(0x263bb5e9cb7700LL),
      real(0x483bd94933da80LL),real(0x935c1fd3f92600LL),
      real(0x14c807d3436d180LL),real(0x35e9298d8a45500LL),
      real(0xac6bf9cef462880LL),real(0x318eb0c51232c400LL),
      reale(9164,0xf22328f6f9f80LL),-reale(84728,0x78acb3795cd00LL),
      reale(191114,0x47ac3650f680LL),-reale(146268,0x68f68696f9e00LL),
      -reale(28124,0xaf1a222081280LL),reale(95633,0xf3c35e98b1100LL),
      -reale(42101,0xccb76963dbb80LL),reale(4250,0xa99770cb50078LL),
      reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^4, polynomial in n of order 22
      real(0x7c86a4240e80LL),real(0xaf5db2064cc0LL),real(0xfb958bed1300LL),
      real(0x17080cf847940LL),real(0x2288f92359780LL),real(0x352f6beaa45c0LL),
      real(0x54760062cdc00LL),real(0x8b024608ff240LL),real(0xeea60450a2080LL),
      real(0x1af0609151bec0LL),real(0x33c8072244a500LL),
      real(0x6bad7af287eb40LL),real(0xf83a707fcba980LL),
      real(0x293d0a92ebeb7c0LL),real(0x87aa233703e6e00LL),
      real(0x2855283ce7ee6440LL),reale(7785,0x74e297d243280LL),
      -reale(76427,0xf39041d0ccf40LL),reale(190726,0x777542b243700LL),
      -reale(188315,0x1030e5dfaa2c0LL),reale(42101,0xccb76963dbb80LL),
      reale(46959,0xb31b5803129c0LL),-reale(23682,0x43272b482b978LL),
      reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^3, polynomial in n of order 23
      real(0x21a7e921c980LL),real(0x2e51be6e8f00LL),real(0x40c19fbec480LL),
      real(0x5c1e6062c200LL),real(0x8599d6a9df80LL),real(0xc60160b77500LL),
      real(0x12cb7c4c7da80LL),real(0x1d5985b996800LL),real(0x2f524aaed7580LL),
      real(0x4f30941955b00LL),real(0x8a76dd63f7080LL),real(0xff32326380e00LL),
      real(0x1f5b1b59928b80LL),real(0x42dd3cfeae4100LL),
      real(0x9e90e4efcb8680LL),real(0x1b33e235264b400LL),
      real(0x5cdaf2eb93f2180LL),real(0x1cd398a25fa82700LL),
      reale(5865,0x9368046121c80LL),-reale(61723,0xe7c88c9baa600LL),
      reale(171645,0xcc7599f993780LL),-reale(213747,0x992d035d6f300LL),
      reale(126305,0x66263c2b93280LL),-reale(28944,0xfcbe1874a70e8LL),
      reale(5698544,0x5eb10eb5f946bLL),
      // C4[2], coeff of eps^2, polynomial in n of order 24
      real(0x5f08c3cb900LL),real(0x807038c0ca0LL),real(0xaffaed32440LL),
      real(0xf4c5be483e0LL),real(0x15a2490f6f80LL),real(0x1f28eae1cb20LL),
      real(0x2dce80c7fac0LL),real(0x44e60304c260LL),real(0x6a58ca3b2600LL),
      real(0xa90e89d449a0LL),real(0x1160126eb5140LL),real(0x1db88b51940e0LL),
      real(0x354168d7adc80LL),real(0x64e3bca9a8820LL),real(0xcc99ed98827c0LL),
      real(0x1c3fb9ad58ff60LL),real(0x45c01ca2899300LL),
      real(0xc88852534b86a0LL),real(0x2d1eac1f8a97e40LL),
      real(0xee21e1c2e9afde0LL),reale(3238,0x9997f46a24980LL),
      -reale(36434,0x3fed7daa1bae0LL),reale(105254,0x7fca8779a54c0LL),
      -reale(115779,0xf2f861d29c3a0LL),reale(43417,0x7b1d24aefa95cLL),
      reale(5698544,0x5eb10eb5f946bLL),
      // C4[3], coeff of eps^26, polynomial in n of order 0
      433472,real(72882272925LL),
      // C4[3], coeff of eps^25, polynomial in n of order 1
      real(76231168),real(19985680),real(0x958a9334879LL),
      // C4[3], coeff of eps^24, polynomial in n of order 2
      real(969805824),-real(756467712),real(427576864),real(0x33a763b318f5LL),
      // C4[3], coeff of eps^23, polynomial in n of order 3
      real(0xe7cfd39aa00LL),-real(0xe6239d55400LL),real(0x44ffe5cce00LL),
      real(0x123fa804df0LL),real(0x73400ac32a3f24fLL),
      // C4[3], coeff of eps^22, polynomial in n of order 4
      real(633551529LL<<15),-real(0x130f2c71c000LL),real(0x7e08a8b4000LL),
      -real(0x69e0a004000LL),real(0x39175efa340LL),real(0x59a39697cb86721LL),
      // C4[3], coeff of eps^21, polynomial in n of order 5
      real(0xe1a59555817c700LL),-real(0xce92ef160470400LL),
      real(0x6a50b28bc94d100LL),-real(0x6ec5ce0328fa200LL),
      real(0x1e2919432b73b00LL),real(0x81169f96b647f8LL),
      reale(2659320,0xb4b906dd74543LL),
      // C4[3], coeff of eps^20, polynomial in n of order 6
      real(0x4a951ec0f743800LL),-real(0x39128060ba74400LL),
      real(0x258d1de3ebd5000LL),-real(0x25e6a8ece22dc00LL),
      real(0xe953314d336800LL),-real(0xd6fbba5b80b400LL),
      real(0x6d3d6d3e79ea90LL),reale(531864,0x2425015f7daa7LL),
      // C4[3], coeff of eps^19, polynomial in n of order 7
      real(0x7366685d2da15300LL),-real(0x46390dd9eadeba00LL),
      real(0x3de3739917104900LL),-real(0x34e3ad131262bc00LL),
      real(0x1ae64995e9a59f00LL),-real(0x1d6cea9b561f3e00LL),
      real(0x70d3407961b9500LL),real(0x1ea45bc7b594048LL),
      reale(7977962,0x1e2b14985cfc9LL),
      // C4[3], coeff of eps^18, polynomial in n of order 8
      reale(2991,8707772229LL<<17),-real(0x5c0b6a6cd5328000LL),
      real(0x6cf3b04ea6358000LL),-real(0x47da0c907a958000LL),
      real(0x334344c895550000LL),-real(0x3257cd9b75628000LL),
      real(0x11d874d9e96c8000LL),-real(0x1273b92365d58000LL),
      real(0x8b048eddb8dae80LL),reale(7977962,0x1e2b14985cfc9LL),
      // C4[3], coeff of eps^17, polynomial in n of order 9
      reale(4599,0x20675bc677c00LL),-reale(2190,0x6a6db0c48a000LL),
      reale(3019,0xad2c946b04400LL),-real(0x5cc951aa5f7ff800LL),
      real(0x61f2b89850d68c00LL),-real(0x49aa7ace4eb85000LL),
      real(0x26482ceb1d4d5400LL),-real(0x2b88fb70a186a800LL),
      real(0x8bf6f0c9a679c00LL),real(0x26ce624431e62e0LL),
      reale(7977962,0x1e2b14985cfc9LL),
      // C4[3], coeff of eps^16, polynomial in n of order 10
      real(0x383bee2531d2a000LL),-real(0x2821094d061d1000LL),
      real(0x2c347b321d4c8000LL),-real(0x125d6736b20ff000LL),
      real(0x1a6c4162f9ae6000LL),-real(0xdca07dd1a07d000LL),
      real(0xba2cc7913be4000LL),-real(0xa8a49fd40deb000LL),
      real(0x36dcb24ee422000LL),-real(0x4159df2ed6e9000LL),
      real(0x1bdad6784709c40LL),reale(1139708,0xdfbd02f131dafLL),
      // C4[3], coeff of eps^15, polynomial in n of order 11
      reale(7381,0x14c34c0c1f400LL),-reale(13257,0xf5b9dadc0c800LL),
      reale(7086,0x404eb1053bc00LL),-reale(4054,0xe4ed62e9ea000LL),
      reale(5287,0x17e93cc880400LL),-real(0x7bc6aed7afe87800LL),
      reale(2758,0x364797381cc00LL),-real(0x676ee80244a35000LL),
      real(0x3b6d32d9ca041400LL),-real(0x43e3e0c280942800LL),
      real(0xa86d2e316b1dc00LL),real(0x300bec0027818e0LL),
      reale(7977962,0x1e2b14985cfc9LL),
      // C4[3], coeff of eps^14, polynomial in n of order 12
      reale(66948,0x4f30b3f870000LL),-reale(52646,0x686a3833a8000LL),
      reale(7561,0xd0b8bda7a8000LL),-reale(13026,0x7d89ec00d8000LL),
      reale(8130,0xd3b0b583a0000LL),-reale(3523,0xd290763e28000LL),
      reale(5530,0x8b9708b698000LL),-real(0x7e52c154efd58000LL),
      reale(2356,0x7673a06ad0000LL),-real(0x6f6a34d21b028000LL),
      real(0x220d8444fca88000LL),-real(0x2fac85fa2e858000LL),
      real(0x11c823101280e280LL),reale(7977962,0x1e2b14985cfc9LL),
      // C4[3], coeff of eps^13, polynomial in n of order 13
      -reale(129173,0x58489bc283900LL),reale(59789,0xf9dc41e63d400LL),
      reale(65695,0x9083acc5cc100LL),-reale(58445,0x2f2cc6e161a00LL),
      reale(8184,0x5e79915d1b00LL),-reale(12353,0x83a959670c800LL),
      reale(9463,0x4211f61d49500LL),-reale(2966,0xe12b8e3527600LL),
      reale(5543,0x52a28a556ef00LL),-reale(2249,0xe1f749ba16400LL),
      real(0x6b0d1cda5c5fe900LL),-real(0x70ab303245f3d200LL),
      real(0xb596d16f1a34300LL),real(0x35b4de912478078LL),
      reale(7977962,0x1e2b14985cfc9LL),
      // C4[3], coeff of eps^12, polynomial in n of order 14
      -reale(6933,0xfc2bb7bd6800LL),reale(63382,0x668969a617c00LL),
      -reale(132589,0xf0bdf2e789000LL),reale(69768,0x70d2052fd2400LL),
      reale(63007,0x6d053a2cb4800LL),-reale(65233,0xb829e1b817400LL),
      reale(9601,0xec9983923a000LL),-reale(11042,0x4317b942ccc00LL),
      reale(11048,0xa50acd625f800LL),-reale(2545,0x7c97f16176400LL),
      reale(5107,0xc83f2d67d000LL),-reale(2697,0x85e48cc53bc00LL),
      real(0x36af107261fea800LL),-real(0x57b6b3b8f7f45400LL),
      real(0x1b355635bf037310LL),reale(7977962,0x1e2b14985cfc9LL),
      // C4[3], coeff of eps^11, polynomial in n of order 15
      -real(0x718d19ce618f700LL),-real(0x22292bb4d2a0a600LL),
      -reale(6561,0x7bb8e05b06500LL),reale(61876,0xa080215cbc400LL),
      -reale(135759,0x6c0a25f10b300LL),reale(81504,0x4116e653fae00LL),
      reale(58147,0xb03676e9edf00LL),-reale(73011,0xd75b35d7e2800LL),
      reale(12405,0x6d2fd911f1100LL),-reale(8886,0xdfa5214b6fe00LL),
      reale(12677,0x826d436a8a300LL),-reale(2577,0x6d77ecdf41400LL),
      reale(3947,0x879d1c7c5500LL),-reale(3192,0x95f286c2eaa00LL),
      real(0x7343398f272e700LL),real(0x20b3728b7b6b2d8LL),
      reale(7977962,0x1e2b14985cfc9LL),
      // C4[3], coeff of eps^10, polynomial in n of order 16
      -real(0xaaaed768da0000LL),-real(0x1d8d58546174000LL),
      -real(0x650ff776c6dc000LL),-real(0x1f0fa133b6eac000LL),
      -reale(6125,0x868b157bb8000LL),reale(59813,0x741ec012c000LL),
      -reale(138411,0xa7483b2cd4000LL),reale(95264,0x22057cd374000LL),
      reale(50003,0x3a5ca8a530000LL),-reale(81502,0xff7b30e274000LL),
      reale(17542,0xf2776c79b4000LL),-reale(5812,0xc63b637b2c000LL),
      reale(13748,0x38a6c4d018000LL),-reale(3547,0xbf6bf7e154000LL),
      real(0x78ab12d1827bc000LL),-reale(2957,0x6b24852f8c000LL),
      real(0x2bef42096127d7c0LL),reale(7977962,0x1e2b14985cfc9LL),
      // C4[3], coeff of eps^9, polynomial in n of order 17
      -real(0x3cadc0edd6600LL),-real(0x8587ee4c4e000LL),
      -real(0x14633459f95a00LL),-real(0x397bc2059d8400LL),
      -real(0xc89f8adb490e00LL),-real(0x3f2a86a64b5a800LL),
      -real(0x32218961953c0200LL),reale(8146,0xa930f21b73400LL),
      -reale(20015,0x8b16989f1b600LL),reale(15890,0x8aa3fb72d9000LL),
      reale(5271,0xbcd5aeda65600LL),-reale(12822,0x9424c22ae1400LL),
      reale(3774,0x46bb658aca200LL),-real(0x148a80159bb73800LL),
      real(0x736580900f31ae00LL),-real(0x336f49c74ee95c00LL),
      -real(0x249e756eeea0600LL),-real(0x13841fc89043bb0LL),
      reale(1139708,0xdfbd02f131dafLL),
      // C4[3], coeff of eps^8, polynomial in n of order 18
      -real(0x5318540751000LL),-real(0xa0702ad537800LL),
      -real(0x14a9549a688000LL),-real(0x2e31b9dc878800LL),
      -real(0x72dceb1c83f000LL),-real(0x14a6c8c8df91800LL),
      -real(0x49c3e43ec426000LL),-real(0x17df3e19aed32800LL),
      -reale(5017,0x9bceef61ed000LL),reale(53301,0x74feac5bf4800LL),
      -reale(140139,0x5706164944000LL),reale(129320,0x1fd8eca933800LL),
      reale(16403,0x87db178e25000LL),-reale(95278,0x1e65e67825800LL),
      reale(40665,0x6f4b03ec9e000LL),-real(0x1c82af8b65ac6800LL),
      reale(8049,0x334ede6a77000LL),-reale(7540,0x5b108b15f800LL),
      real(0x49ca297e3ffdbce0LL),reale(7977962,0x1e2b14985cfc9LL),
      // C4[3], coeff of eps^7, polynomial in n of order 19
      -real(0x11fa490472e00LL),-real(0x1fe0e98340400LL),
      -real(0x3b2a552443a00LL),-real(0x73f5544ad2000LL),
      -real(0xf2e5765f90600LL),-real(0x2290ce0f423c00LL),
      -real(0x57b83400ee1200LL),-real(0x1023f65b9bfd800LL),
      -real(0x3b36c6db61bde00LL),-real(0x13c7b72049527400LL),
      -reale(4323,0x73be8c4caea00LL),reale(48359,0x7d21dc7197000LL),
      -reale(137343,0xc18958973b600LL),reale(148676,0xd51cb5c775400LL),
      -reale(14754,0xa89f0bc9ec200LL),-reale(92175,0x33d1092c54800LL),
      reale(60290,0x88af4d43b7200LL),-reale(5855,0x8c9719d08e400LL),
      -real(0x48b16aa4982d9a00LL),-real(0x51dba59b00547450LL),
      reale(7977962,0x1e2b14985cfc9LL),
      // C4[3], coeff of eps^6, polynomial in n of order 20
      -real(0x3f0527da8000LL),-real(0x69410a894000LL),-real(0xb5f68cf74000LL),
      -real(0x14766cd18c000LL),-real(5178956321LL<<17),
      -real(0x4cf42ca274000LL),-real(0xa45199d7cc000LL),
      -real(0x17e337e696c000LL),-real(0x3e169088698000LL),
      -real(0xbbd1c494494000LL),-real(0x2c70014b4ca4000LL),
      -real(0xf67e7406420c000LL),-reale(3524,0xcb63f52610000LL),
      reale(41859,0x1cfdfa000c000LL),-reale(129839,0xf92d750efc000LL),
      reale(166586,0x5d10da3394000LL),-reale(59706,0x5fbf7c0388000LL),
      -reale(68020,0xa047f74594000LL),reale(75721,0x1307a9002c000LL),
      -reale(24384,0xc0b45d798c000LL),real(0x6534ccbfa35124c0LL),
      reale(7977962,0x1e2b14985cfc9LL),
      // C4[3], coeff of eps^5, polynomial in n of order 21
      -real(0xcd30266b700LL),-real(0x147d4e1fec00LL),-real(0x21a6b4a64100LL),
      -real(0x390579acce00LL),-real(0x6423741d2b00LL),-real(0xb749b833f000LL),
      -real(0x1602ad6953500LL),-real(0x2ccfc753d1200LL),
      -real(0x61e5d62301f00LL),-real(0xe995b2fcff400LL),
      -real(0x270c826fb7a900LL),-real(0x7a09e7f3045600LL),
      -real(0x1dfb4c385ed9300LL),-real(0xaddceca1091f800LL),
      -reale(2624,0xc45e83fdb9d00LL),reale(33433,0x20d0a109f6600LL),
      -reale(114656,0xa3de6d0238700LL),reale(175907,0x1d4b03fe80400LL),
      -reale(116168,0x7b17e334f1100LL),-reale(3810,0x1e1c2e9afde00LL),
      reale(45340,0x664f5dce00500LL),-reale(17205,0xff74273e2678LL),
      reale(7977962,0x1e2b14985cfc9LL),
      // C4[3], coeff of eps^4, polynomial in n of order 22
      -real(784468838400LL),-real(0x11a0a388400LL),-real(0x1bda05d7000LL),
      -real(0x2d25cb21c00LL),-real(0x4b5283d5800LL),-real(0x81d5381f400LL),
      -real(0xe84e582c000LL),-real(0x1b2017768c00LL),-real(0x354f35942800LL),
      -real(0x6f49195e6400LL),-real(0xf9ffb1d81000LL),-real(0x267769207fc00LL),
      -real(0x6a9801634f800LL),-real(0x15adc2fc41d400LL),
      -real(0x5947d2bb916000LL),-real(0x222d7eabcda6c00LL),
      -real(0x22707489da53c800LL),reale(7620,0x3c385d35fbc00LL),
      -reale(29197,0x886c2c8e2b000LL),reale(53341,0xa58a8c79e2400LL),
      -reale(51817,0x997f46a249800LL),reale(25908,0xccbfa35124c00LL),
      -reale(5262,0xb996ed2c7b770LL),reale(2659320,0xb4b906dd74543LL),
      // C4[3], coeff of eps^3, polynomial in n of order 23
      -real(242883621120LL),-real(365079728640LL),-real(559688344320LL),
      -real(876931046400LL),-real(0x147bd04f500LL),-real(0x21c7b15a600LL),
      -real(0x396d13e6700LL),-real(0x650be18b000LL),-real(0xb8f375f7900LL),
      -real(0x16253c45ba00LL),-real(0x2cc1928ceb00LL),-real(0x6065d92f8400LL),
      -real(0xe04f74737d00LL),-real(0x23eadf138ce00LL),
      -real(0x682920857ef00LL),-real(0x1651f4aee45800LL),
      -real(0x61a68e7d270100LL),-real(0x281b43aa424e200LL),
      -real(0x2bddd20238857300LL),reale(10668,0x544ee8e52d400LL),
      -reale(45340,0x664f5dce00500LL),reale(90680,0xcc9ebb9c00a00LL),
      -reale(84203,0x996ed2c7b7700LL),reale(28944,0xfcbe1874a70e8LL),
      reale(7977962,0x1e2b14985cfc9LL),
      // C4[4], coeff of eps^26, polynomial in n of order 0
      real(74207744),real(0x377b3e1aa351LL),
      // C4[4], coeff of eps^25, polynomial in n of order 1
      -real(85649408),real(42776448),real(0x7a5a1b59863LL),
      // C4[4], coeff of eps^24, polynomial in n of order 2
      -real(0x5d090f66800LL),real(0x15cb8432c00LL),real(412184096896LL),
      real(0x3e897844a5071ebLL),
      // C4[4], coeff of eps^23, polynomial in n of order 3
      -real(0xbff3f70d800LL),real(0x44c7b31b000LL),-real(0x48108b34800LL),
      real(0x21db9c9a980LL),real(0x4fc9e010f5dcf23LL),
      // C4[4], coeff of eps^22, polynomial in n of order 4
      -real(0xd6b769b7e000LL),real(0x72b1142e1800LL),-real(0x82aa7be7f000LL),
      real(0x1aa8532e0800LL),real(0x779e97cc600LL),real(0x40d4060dc7c384c7LL),
      // C4[4], coeff of eps^21, polynomial in n of order 5
      -real(0x474af3a87693800LL),real(0x3c389a0df442000LL),
      -real(0x37e1a3d92db8800LL),real(0x12d1db00bd71000LL),
      -real(0x15fc16a85bcd800LL),real(0x99491c279c9880LL),
      reale(1139708,0xdfbd02f131dafLL),
      // C4[4], coeff of eps^20, polynomial in n of order 6
      -real(0x303d69b47fe22400LL),real(0x3f4d2c93a259b200LL),
      -real(0x29be542895db1800LL),real(0x17eb54d9d2a59e00LL),
      -real(0x1b89924120220c00LL),real(0x4aa7a22c8d50a00LL),
      real(0x157745851f3d4c0LL),reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^19, polynomial in n of order 7
      -real(0x44c3305a70de1000LL),real(0x6d1c9adfcac5e000LL),
      -real(0x312f88327b293000LL),real(0x3351684a1a554000LL),
      -real(0x2ab43a21fd0e5000LL),real(0xdaac481cc1ca000LL),
      -real(0x120b854707e97000LL),real(0x7289c72302f3500LL),
      reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^18, polynomial in n of order 8
      -reale(2256,0x7b501df238000LL),reale(2620,0x5abb698ccf000LL),
      -real(0x3cfd86157c22a000LL),real(0x656f30f9d7a5d000LL),
      -real(0x3529aafa1251c000LL),real(0x23979dd758c6b000LL),
      -real(0x27cfd52f91a0e000LL),real(0x52c1297ffdf9000LL),
      real(0x1899e61f0915c00LL),reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^17, polynomial in n of order 9
      -reale(5647,0x92962c0679000LL),reale(3064,0xd620df9a18000LL),
      -real(0x73b5708edb717000LL),reale(2782,0xf8e2a6bab2000LL),
      -real(0x3aa55028ed4d5000LL),real(0x54f5b0489ac0c000LL),
      -real(0x3a8372ad6ebf3000LL),real(0x128f31db99de6000LL),
      -real(0x1bbb3cddeb8b1000LL),real(0x9c3f5d344ffbb00LL),
      reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^16, polynomial in n of order 10
      -reale(12546,0xd0659481f7000LL),reale(2321,0x6f75c5bce2800LL),
      -reale(5209,0xc9bfbad2ac000LL),reale(3693,0x4f3d4dd785800LL),
      -real(0x59b26230b2e61000LL),reale(2785,0x7ef843b608800LL),
      -real(0x4086b5731d656000LL),real(0x3b22d2695822b800LL),
      -real(0x3bbf747f663cb000LL),real(0x50e2c41c71ae800LL),
      real(0x19182d9cca60700LL),reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^15, polynomial in n of order 11
      -reale(14655,0xa7ccf7b3e3000LL),reale(5703,0xb41e60048e000LL),
      -reale(13723,0x6fa2143b1000LL),reale(2794,0x80dd2a6158000LL),
      -reale(4434,0xbdbd659d5f000LL),reale(4398,0x1bf890b722000LL),
      -real(0x462f1f0759b2d000LL),reale(2504,0xfcfacf17ac000LL),
      -real(0x4eb2a95e9a75b000LL),real(0x1bef3eef6f4b6000LL),
      -real(0x2d8008caddc29000LL),real(0xdbb189dc4eba300LL),
      reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^14, polynomial in n of order 12
      -reale(31110,0xd0a51132f4000LL),reale(76716,0x887753c58b000LL),
      -reale(19285,0xcfd85f57f6000LL),reale(3558,0x4fcfd1ab09000LL),
      -reale(14554,0xbf2d0ac9f8000LL),reale(3850,0x9631322307000LL),
      -reale(3313,0x90f8abbffa000LL),reale(4999,0xf3c6aed085000LL),
      -real(0x44308029330fc000LL),real(0x72cd2f325ae83000LL),
      -real(0x5cc3eeffca3fe000LL),real(0x2f990ef34001000LL),
      real(0xedd65cb262fc00LL),reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^13, polynomial in n of order 13
      reale(109832,0xfe67f2664d000LL),-reale(101414,0x365d952fe4000LL),
      -reale(21578,0x2c7dffdd75000LL),reale(81484,0xfb5b01862000LL),
      -reale(25828,0x7adf44b697000LL),real(0x527645ab2c368000LL),
      -reale(14626,0xa0f5b7bcd9000LL),reale(5668,0x89f8307d6e000LL),
      -real(0x7c6deea8217fb000LL),reale(5148,0xb3c77272b4000LL),
      -real(0x5ea4f23e05fbd000LL),real(0x33d79ea3e6f7a000LL),
      -real(0x512f5a2dc7bdf000LL),real(0x13f171801c8d4d00LL),
      reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^12, polynomial in n of order 14
      reale(3290,0xf070eb97f3400LL),-reale(37925,0x14cc0872bb200LL),
      reale(108756,0x262a302ba0800LL),-reale(111139,0xba49ef60cbe00LL),
      -reale(8978,0x96e5af6312400LL),reale(85061,0xe9667b666b600LL),
      -reale(34830,0xb50884d615000LL),-real(0x1ae66991075c5600LL),
      -reale(13337,0xd2d72b2557c00LL),reale(8254,0x43d2c57af1e00LL),
      -real(0x39646320240ca800LL),reale(4333,0x5a8eb4efe1200LL),
      -reale(2317,0x387052d25d400LL),-real(0x4971411b9aa7a00LL),
      -real(0x239dc6f1135e6c0LL),reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^11, polynomial in n of order 15
      real(0x22fb18f3d6fc800LL),real(0xc812a63656dd000LL),
      reale(2929,0x54e6120875800LL),-reale(35121,0x48d05c62be000LL),
      reale(106528,0xc02be4bd3e800LL),-reale(121104,0xca8db31999000LL),
      reale(7480,0x3b39caec37800LL),reale(86076,0xd8784a9f2c000LL),
      -reale(46728,0xdb6f945bbf800LL),-real(0x1e17ea5787b8f000LL),
      -reale(10012,0x630283c6800LL),reale(11072,0xcb500e9316000LL),
      -real(0x3d2315ebbfcfd800LL),reale(2196,0x522d08f7fb000LL),
      -reale(2582,0x2942c8d084800LL),real(0x1dbc900c41177d80LL),
      reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^10, polynomial in n of order 16
      real(0x2367980c018000LL),real(0x717a5d0aad6800LL),
      real(0x1c7a6b9a7155000LL),real(0xa7a0b73a0f93800LL),
      reale(2540,0xdc02459a12000LL),-reale(31836,0xf2625ff3ef800LL),
      reale(102741,0xc61b0075cf000LL),-reale(130713,0xb431635532800LL),
      reale(28618,0x913148900c000LL),reale(82224,0x225affaa4a800LL),
      -reale(61371,0x71836a73b7000LL),reale(3358,0xd2d9334507800LL),
      -reale(4436,0x51714c11fa000LL),reale(12409,0x2e12e0f984800LL),
      -reale(3099,0xb59c601f3d000LL),-real(0x185351aa9adbe800LL),
      -real(0xfcd867cd32b4e00LL),reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^9, polynomial in n of order 17
      real(0x3b98569230800LL),real(0x954e9f9ae8000LL),real(0x1a387f0ed5f800LL),
      real(0x561911aabbb000LL),real(0x163673b1889e800LL),
      real(0x870aa0c397ae000LL),reale(2128,0x4412890e0d800LL),
      -reale(28018,0x9edd02151f000LL),reale(96862,0x40aaeaffcc800LL),
      -reale(138876,0x18d8a92e8c000LL),reale(55003,0xc4365147fb800LL),
      reale(69831,0x65a81c2787000LL),-reale(76836,0x9198c23745800LL),
      reale(14324,0xf9d757893a000LL),real(0x610a50cc5ec29800LL),
      reale(9036,0xddda1962ad000LL),-reale(5866,0x301cbcb97800LL),
      real(0x2b3d64f38f7c3a80LL),reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^8, polynomial in n of order 18
      real(0x7c44a1c56800LL),real(0x10e1a40b9f400LL),real(0x2778995e94000LL),
      real(0x6511d82348c00LL),real(0x122fbee15d1800LL),
      real(0x3d60d47d162400LL),real(0x10572b5ec96f000LL),
      real(0x670e5c5512cbc00LL),real(0x6a1969ca184cc800LL),
      -reale(23632,0x6fc488059ac00LL),reale(88223,0x601afc7b4a000LL),
      -reale(143685,0x3819032af1400LL),reale(86217,0x78ea8eac47800LL),
      reale(43622,0x50ec504da8400LL),-reale(86857,0xe4e3b378db000LL),
      reale(34767,0x1af4459111c00LL),real(0x470ee9f8c8f42800LL),
      -real(0xf0a395fd8dd4c00LL),-real(0x55da5cd875ef3c80LL),
      reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^7, polynomial in n of order 19
      real(0x114b06357800LL),real(0x2239f3629000LL),real(0x475e8ebd2800LL),
      real(0x9e5523c88000LL),real(0x17aa424dfd800LL),real(0x3e2133dde7000LL),
      real(0xb7f09cec78800LL),real(0x280af153ee6000LL),
      real(0xb0d866e91e3800LL),real(0x48b6aeda5425000LL),
      real(0x4ec10b7f840de800LL),-reale(18693,0xda891ccdbc000LL),
      reale(76065,0x2aaa760409800LL),-reale(141961,0xc3f732a21d000LL),
      reale(119123,0xd1c84be04800LL),-real(0x7f4b67756e45e000LL),
      -reale(76606,0xe7a6860690800LL),reale(56790,0xce45bec021000LL),
      -reale(14598,0xc436164715800LL),real(0x23b84843a30d9480LL),
      reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^6, polynomial in n of order 20
      real(0x2492f246000LL),real(0x43b68382800LL),real(0x827fc7ff000LL),
      real(0x10769dabb800LL),real(0x231371038000LL),real(0x4fad3dfb4800LL),
      real(0xc39532c71000LL),real(0x2109cc8eed800LL),real(0x650cdd3e2a000LL),
      real(0x16d3054b8e6800LL),real(0x69275cf4ee3000LL),
      real(0x2d6bb9aa2a1f800LL),real(0x342dc9db6781c000LL),
      -reale(13325,0xb15a42ce7800LL),reale(59725,0xe775950b55000LL),
      -reale(128819,0x4abda20fae800LL),reale(144216,0xdf24ba0e000LL),
      -reale(65935,0x168961cdb5800LL),-reale(23422,0x325c674239000LL),
      reale(39625,0x392517e583800LL),-reale(12954,0x665fd1a892600LL),
      reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^5, polynomial in n of order 21
      real(273177999360LL),real(481049600000LL),real(875104847872LL),
      real(0x180866df000LL),real(0x2f4b74a1800LL),real(0x61abf5b8000LL),
      real(0xd562fc0e800LL),real(0x1f2598191000LL),real(0x4ed8f85ab800LL),
      real(0xdc91252ca000LL),real(0x2bd44913d8800LL),real(0xa584ade1c3000LL),
      real(0x322090df0f5800LL),real(0x16f6266186dc000LL),
      real(0x1c472a543df62800LL),-reale(7859,0x7aaf0fd58b000LL),
      reale(39234,0x9eeb23497f800LL),-reale(98180,0xb70c1a0b12000LL),
      reale(140051,0xe6fe7071ac800LL),-reale(115827,0x9358bc0159000LL),
      reale(51817,0x997f46a249800LL),-reale(9715,0xccc7dd3e6dc80LL),
      reale(10257379,0xdda51a7ac0b27LL),
      // C4[4], coeff of eps^4, polynomial in n of order 22
      real(18103127040LL),real(30658521600LL),real(53362944000LL),
      real(95756838400LL),real(177805329408LL),real(343155696128LL),
      real(692078714880LL),real(0x155e2e7de00LL),real(0x30194583c00LL),
      real(0x741fc16da00LL),real(0x131155285800LL),real(0x379d38605600LL),
      real(0xb96166967400LL),real(0x2e2dfa3db5200LL),real(0xee14dc9ed9000LL),
      real(0x752e44962ece00LL),real(0x9cf0406db58ac00LL),
      -reale(3007,0xfcd2e16ce3600LL),reale(16844,0xbb0354c82c800LL),
      -reale(48007,0x7b6318074ba00LL),reale(77726,0x663ee9f36e400LL),
      -reale(64771,0xffdf184adbe00LL),reale(21050,0xe65bb4b1eddc0LL),
      reale(10257379,0xdda51a7ac0b27LL),
      // C4[5], coeff of eps^26, polynomial in n of order 0
      356096,real(98232628725LL),
      // C4[5], coeff of eps^25, polynomial in n of order 1
      real(19006687232LL),real(5473719680LL),real(0x1580fd4afdbe65LL),
      // C4[5], coeff of eps^24, polynomial in n of order 2
      real(91538057LL<<15),-real(0x378568c4000LL),real(0x16cc31e2a00LL),
      real(0x4c6f2137745e091LL),
      // C4[5], coeff of eps^23, polynomial in n of order 3
      real(0xef2f223e3800LL),-real(0x110fb2e7bf000LL),real(0x282bb4606800LL),
      real(0xbe30d7a6780LL),reale(2828,0xfcd03d1974f5LL),
      // C4[5], coeff of eps^22, polynomial in n of order 4
      real(0x5e4a1598000LL),-real(0x48b6e92a000LL),real(97904939LL<<14),
      -real(0x20e8326e000LL),real(850763001088LL),real(0x2081a7235aaf593LL),
      // C4[5], coeff of eps^21, polynomial in n of order 5
      real(0x40db2f49b455f800LL),-real(0x1e99bb32c4c22000LL),
      real(0x173ba0294630c800LL),-real(0x194707e3169c1000LL),
      real(0x2d83efe695c9800LL),real(0xdf3e0617af3080LL),
      reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^20, polynomial in n of order 6
      real(0x216feaa994ce0000LL),-real(0xab5f967e8690000LL),
      real(0x47922226ed5LL<<18),-real(0xb74a91dab5f0000LL),
      real(0x3c54ceff81a0000LL),-real(0x5d7cb98f1a50000LL),
      real(0x1f9a69370b20800LL),reale(4178932,0x89b50ac9b6cd7LL),
      // C4[5], coeff of eps^19, polynomial in n of order 7
      real(0x737c719d74a11000LL),-real(0x33cb00709b02e000LL),
      real(0x64aa4f647e063000LL),-real(0x22d04f5347fb4000LL),
      real(0x244213a9e6215000LL),-real(0x2372b83384fba000LL),
      real(0x29c5a12d1767000LL),real(0xd64e2b028e9d00LL),
      reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^18, polynomial in n of order 8
      real(0x4d6c482dac2a0000LL),-reale(2329,0xb1fe2723dc000LL),
      reale(2244,0xda129de1b8000LL),-real(0x25b9c94d1ec14000LL),
      real(0x5915813997350000LL),-real(0x2b18411354f8c000LL),
      real(0x1038d20e1fbe8000LL),-real(0x1a9977b2ea9c4000LL),
      real(0x7df995f732ef600LL),reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^17, polynomial in n of order 9
      real(0x514388ef27d31000LL),-reale(6020,0x2be450c918000LL),
      real(0x6fa66bdc836df000LL),-real(0x67912be26fab2000LL),
      reale(2539,0xf65fb2006d000LL),-real(0x237e1033f4d8c000LL),
      real(0x3efb5ba75c79b000LL),-real(0x32b52fd83cbe6000LL),
      real(0x17d40e2c1a29000LL),real(0x7dfd16a9c2e300LL),
      reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^16, polynomial in n of order 10
      reale(12470,0xf777d5cb70000LL),-reale(8994,0x34ff96fbd8000LL),
      real(0x8b5e07446e3LL<<18),-reale(5684,0xa351b76ba8000LL),
      reale(2676,0xe4b7624210000LL),-real(0x3b4e8fe27b2f8000LL),
      reale(2525,0xe113384060000LL),-real(0x317b33e66b8c8000LL),
      real(0x1afebbc488cb0000LL),-real(0x2abc78cdb6418000LL),
      real(0xab0b32cc6da3c00LL),reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^15, polynomial in n of order 11
      reale(45753,0x27312c684b000LL),real(0x6b25908081df2000LL),
      reale(10080,0x3e3c4e94e9000LL),-reale(11483,0x3052990658000LL),
      real(0x186dcc47df2a7000LL),-reale(4654,0xe97b33c9a2000LL),
      reale(3765,0x192eb8a145000LL),-real(0x1ea7f016e242c000LL),
      real(0x7c08a9e80a083000LL),-real(0x48a61c5124e36000LL),
      -real(0x1ab8464a6fdf000LL),-real(0xc3b3128c53f500LL),
      reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^14, polynomial in n of order 12
      -reale(29853,0xf97fbea090000LL),-reale(72661,0xb2e53c820c000LL),
      reale(55735,0xd505afdac8000LL),-real(0x19eb9cd373704000LL),
      reale(6447,8655275741LL<<17),-reale(13735,0x934f51ea3c000LL),
      real(0x503c7c1e17a78000LL),-reale(2910,0x8f0f066334000LL),
      reale(4611,0xa07ae6cfd0000LL),-real(0x28ec95124696c000LL),
      real(0x386dc5f3bf428000LL),-real(0x49a3cdb95c464000LL),
      real(0xec86977ad08e600LL),reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^13, polynomial in n of order 13
      -reale(77964,0x27205a1bd000LL),reale(116550,0x911cc360c4000LL),
      -reale(45605,0xab8dec641b000LL),-reale(66195,0xc9de18da12000LL),
      reale(66624,0xae21593727000LL),-reale(5576,0x36f63ac28000LL),
      real(0x6f2264aae1649000LL),-reale(14832,0x2c940b773e000LL),
      reale(3661,0xe0e147ff8b000LL),-real(0x37687d20b9d14000LL),
      reale(4430,0xd2ef37d92d000LL),-real(0x61330ed553f6a000LL),
      -real(0x8fc7d2821691000LL),-real(0x4de8f81581e0b00LL),
      reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^12, polynomial in n of order 14
      -real(0x520b481798460000LL),reale(18997,713316873LL<<16),
      -reale(73060,0xebcc7589c0000LL),reale(119587,0x641c11f8f0000LL),
      -reale(63450,0xfff4f2db20000LL),-reale(54596,0x54a14049b0000LL),
      reale(77203,5136366291LL<<19),-reale(15161,0x669695c550000LL),
      -reale(2898,7333080783LL<<17),-reale(13401,0xbb1dc317f0000LL),
      reale(7364,7522322675LL<<18),real(0xcbde6dd32070000LL),
      reale(2498,0xb270ac8f60000LL),-reale(2207,0xe5e147ba30000LL),
      real(0x146e5a4ec1af3800LL),reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^11, polynomial in n of order 15
      -real(0x8e2d12e55cc800LL),-real(0x3c744345ee05000LL),
      -real(0x436e3347c2885800LL),reale(16354,0x603aee4aee000LL),
      -reale(66895,0x3561b9526e800LL),reale(120525,0x7fafccca1000LL),
      -reale(82888,0x6ce782c3a7800LL),-reale(36026,0xb730ca850c000LL),
      reale(84916,0xe33bbac3af800LL),-reale(30329,0x9a1820a639000LL),
      -reale(5003,0x6724146c89800LL),-reale(8175,0xa51f341306000LL),
      reale(10601,0xdf58b3eb8d800LL),-real(0x51534d8656793000LL),
      -real(0x13f74fe07242b800LL),-real(0x1338322158bf8680LL),
      reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^10, polynomial in n of order 16
      -real(0x5e9d97de20000LL),-real(0x15f51b48a5a000LL),
      -real(0x679f3a6a83c000LL),-real(0x2da38dbb53ee000LL),
      -real(0x351287a208998000LL),reale(13549,0xfdc5cc829e000LL),
      -reale(59298,0x35ebc8a374000LL),reale(118312,0x8f7a13080a000LL),
      -reale(102644,0xbe9581710000LL),-reale(8663,0x3283e8b4ea000LL),
      reale(85056,0xa0c3d6fa54000LL),-reale(50541,0x58fecea57e000LL),
      real(0x9e0314066f78000LL),-real(0x56026edfbaf2000LL),
      reale(9162,0x6ada71271c000LL),-reale(4514,0x3f8f2be686000LL),
      real(0x19aa7dbc9bd2b100LL),reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^9, polynomial in n of order 17
      -real(0x689b7f794800LL),-real(0x12aa316a68000LL),
      -real(0x3c5fe03b7b800LL),-real(0xe70662316b000LL),
      -real(0x468257445d2800LL),-real(0x204dea1c904e000LL),
      -real(0x275c24b79c179800LL),reale(10640,0x725f868a0f000LL),
      -reale(50163,0x8367062950800LL),reale(111598,0xa3db986ecc000LL),
      -reale(120105,0x1af4e4a837800LL),reale(28289,0xbddfd64f09000LL),
      reale(70122,0x41f96206f1800LL),-reale(70104,0xcd1cf1241a000LL),
      reale(17631,0x83f469b94a800LL),reale(3507,0xd4dd7e683000LL),
      real(0x234fa818af3f3800LL),-real(0x5217ce807fb7e980LL),
      reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^8, polynomial in n of order 18
      -real(0x8baa3048000LL),-real(0x155e3991c000LL),-real(237891401LL<<18),
      -real(0xa66484064000LL),-real(0x22acb24838000LL),
      -real(0x89475b1e6c000LL),-real(0x2b8ce25f7b0000LL),
      -real(0x14dd31b8f8b4000LL),-real(0x1acbb07dd4628000LL),
      reale(7723,0xe6c1cd6b44000LL),-reale(39540,0xb1d09a9920000LL),
      reale(98832,0x70f12b47fc000LL),-reale(130553,0x474c4a5618000LL),
      reale(72091,0x9d4697d7f4000LL),reale(31173,0xcb977f1d70000LL),
      -reale(72484,0xa77099aa54000LL),reale(42073,0x76abc75bf8000LL),
      -reale(8983,0xdb34fa045c000LL),real(0x7851cafec6ea600LL),
      reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^7, polynomial in n of order 19
      -real(808445556736LL),-real(0x19fd8659000LL),-real(0x3ce45316800LL),
      -real(0x98e89f08000LL),-real(0x1a16c5239800LL),-real(0x4ef4224b7000LL),
      -real(0x11089a8d8c800LL),-real(0x461e8219c6000LL),
      -real(0x1740d89936f800LL),-real(0xbb97ef56095000LL),
      -real(0xffd8608f0242800LL),reale(4956,0x2ae7ba647c000LL),
      -reale(27803,0x8886c0e865800LL),reale(78703,0x691d56f30d000LL),
      -reale(126581,0xb2ac252438800LL),reale(111405,0x65dae188be000LL),
      -reale(33040,0xc82f8ec41b800LL),-reale(31122,0xa51c18fcd1000LL),
      reale(33849,0xe315529991800LL),-reale(10096,0xcfcaaeb453f80LL),
      reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^6, polynomial in n of order 20
      -real(57693732864LL),-real(118378242048LL),-real(254261280768LL),
      -real(575562375168LL),-real(10565709LL<<17),-real(0x341c17b2000LL),
      -real(0x92ee7ecc000LL),-real(0x1ccf17876000LL),-real(0x6786d9e38000LL),
      -real(0x1bdf19e19a000LL),-real(0x9bb8377424000LL),
      -real(0x5352681ef5e000LL),-real(0x79ce0dfd0cd0000LL),
      reale(2563,0x29027cc1fe000LL),-reale(15917,0xface8c747c000LL),
      reale(51375,0x61bf7d963a000LL),-reale(99436,0x390f87b768000LL),
      reale(119998,0xa6d5e6f116000LL),-reale(88555,0x279c7be1d4000LL),
      reale(36577,0x210e8c3652000LL),-reale(6477,0x332fe8d449300LL),
      reale(12536797,0x9d1f205d24685LL),
      // C4[5], coeff of eps^5, polynomial in n of order 21
      -real(2537256960LL),-real(4922368000LL),-real(9913649152LL),
      -real(20825468928LL),-real(45893163008LL),-real(3260719LL<<15),
      -real(265153996800LL),-real(709434249216LL),-real(0x1e3bc54b800LL),
      -real(0x62f2289a000LL),-real(0x174e12bf8800LL),-real(0x69ee83c3b000LL),
      -real(0x2753bfa335800LL),-real(0x1693a2298bc000LL),
      -real(0x23ce232de3a2800LL),real(0x33ca29bdcdd43000LL),
      -reale(5754,0x693a6155df800LL),reale(21176,0x3b8f28c122000LL),
      -reale(47646,0x86021bb28c800LL),reale(67058,0x11f0010e41000LL),
      -reale(51817,0x997f46a249800LL),reale(16192,0xfff7c612b6f80LL),
      reale(12536797,0x9d1f205d24685LL),
      // C4[6], coeff of eps^26, polynomial in n of order 0
      real(71266816),real(0x75209f8d91abLL),
      // C4[6], coeff of eps^25, polynomial in n of order 1
      -real(61697<<14),real(365122560),real(0x64173937d043LL),
      // C4[6], coeff of eps^24, polynomial in n of order 2
      -real(0x10389da9c000LL),real(0x19e75ef2000LL),real(558875851776LL),
      real(0xd767bab38dc330dLL),
      // C4[6], coeff of eps^23, polynomial in n of order 3
      -real(0x142d81502c000LL),real(0x6dee9f4b8000LL),-real(0xae181cf64000LL),
      real(0x39153b46b400LL),reale(3342,0x41381bc9272f3LL),
      // C4[6], coeff of eps^22, polynomial in n of order 4
      -real(0x13480fca8c000LL),real(0x16106a2c37000LL),
      -real(0x1502d2e846000LL),real(0x16180c1bd000LL),real(0x74238242a00LL),
      reale(3342,0x41381bc9272f3LL),
      // C4[6], coeff of eps^21, polynomial in n of order 5
      -real(0x1c0b06f2aed0000LL),real(0x44926ab731c0000LL),
      -real(0x2031c71e85b0000LL),real(0xca25cdaf0e0000LL),
      -real(0x14c7d62b6490000LL),real(0x61052e04125000LL),
      reale(1139708,0xdfbd02f131dafLL),
      // C4[6], coeff of eps^20, polynomial in n of order 6
      -real(0x3c147e5183b90000LL),real(0x5c8a793ab7a08000LL),
      -real(0xa71b84c4013LL<<17),real(0x26583d412b938000LL),
      -real(0x1ec1409e52930000LL),real(0xd82d55b5068000LL),
      real(0x4a1c5add9a3000LL),reale(14816215,0x5c99263f881e3LL),
      // C4[6], coeff of eps^19, polynomial in n of order 7
      -reale(2884,0x97776797f0000LL),real(0x5dcb94a5bbaa0000LL),
      -real(0x2147754a866d0000LL),real(0x59b9e153ee1c0000LL),
      -real(0x1d3317b06cdb0000LL),real(0xfd67f86b28e0000LL),
      -real(0x193b89a255c90000LL),real(0x662541f54195000LL),
      reale(14816215,0x5c99263f881e3LL),
      // C4[6], coeff of eps^18, polynomial in n of order 8
      -reale(5404,0x5e66e1f930000LL),real(0x194c5bcfa9f36000LL),
      -reale(2201,0x4f230944e4000LL),reale(2053,0x73a8845e02000LL),
      -real(0x127ebba7aac98000LL),real(0x433c97a5782ce000LL),
      -real(0x29997437ffc4c000LL),-real(0xb36408ece66000LL),
      -real(0x4eb946c9b6ac00LL),reale(14816215,0x5c99263f881e3LL),
      // C4[6], coeff of eps^17, polynomial in n of order 9
      -reale(2829,0x5744c85a98000LL),real(0x53fda6bff9540000LL),
      -reale(5946,0xc179df32e8000LL),real(0x424987c8bd3f0000LL),
      -real(0x4d6fba1e72f38000LL),reale(2362,0x7a9b39aaa0000LL),
      -real(0x1a7dd6520d788000LL),real(0x1ca5a49549150000LL),
      -real(0x279b8ad82b3d8000LL),real(0x8624b660e613800LL),
      reale(14816215,0x5c99263f881e3LL),
      // C4[6], coeff of eps^16, polynomial in n of order 10
      reale(3052,0x1cc54fce28000LL),reale(15175,0x33b0e2aba4000LL),
      -reale(5744,0xc5440d7e0000LL),-real(0xd3fdde9c4364000LL),
      -reale(5627,0x42b2a45de8000LL),reale(2296,0xc920e17994000LL),
      -real(0x15ef23de88bf0000LL),reale(2060,0x9b7c8a7a8c000LL),
      -real(0x3634e9b2229f8000LL),-real(0x3eaac877287c000LL),
      -real(0x1ee323a1ca0c800LL),reale(14816215,0x5c99263f881e3LL),
      // C4[6], coeff of eps^15, polynomial in n of order 11
      -reale(77304,0xeb4d9089c8000LL),reale(21636,0x8867f71d90000LL),
      reale(6061,8670344157LL<<15),reale(12960,6074462725LL<<18),
      -reale(9403,0x25b985468000LL),-real(0x35c5d916ffb10000LL),
      -reale(4114,0x3d13bbebb8000LL),reale(3690,0x5a8c0420a0000LL),
      -real(0x7db1fc00af08000LL),real(0x3ee56918f4c50000LL),
      -real(0x41d90b24a2658000LL),real(0xb0f65a4ddefb800LL),
      reale(14816215,0x5c99263f881e3LL),
      // C4[6], coeff of eps^14, polynomial in n of order 12
      reale(84445,0xef949ea0f8000LL),reale(19627,0xf0e541fbce000LL),
      -reale(80833,0x5f741237dc000LL),reale(34575,0x1644d05d7a000LL),
      reale(6828,0x4cfbe5cb50000LL),reale(8288,0x561945cd26000LL),
      -reale(12838,0x6d3e328184000LL),real(0x15c5608ef0ed2000LL),
      -real(0x653ba29de4a58000LL),reale(4217,0x4b5d86267e000LL),
      -real(0x3b46409683b2c000LL),-real(0x974d654f27d6000LL),
      -real(0x674dea252558c00LL),reale(14816215,0x5c99263f881e3LL),
      // C4[6], coeff of eps^13, polynomial in n of order 13
      reale(45373,0x376f121df0000LL),-reale(98871,0xe30dbcdfc0000LL),
      reale(96522,0x4174515a90000LL),-real(0x2d5b0f36d6d20000LL),
      -reale(79483,0x53270530d0000LL),reale(50297,8337588523LL<<19),
      reale(3071,0x5d816f2bd0000LL),real(0x5cfb30543d820000LL),
      -reale(14132,0x4c1b1cdf90000LL),reale(3907,0xfc9bf30ac0000LL),
      real(0x1e5e0fff75d10000LL),reale(2700,0x7f35ecdd60000LL),
      -real(0x74992b46f6e50000LL),real(0xe2f417f6bbc1000LL),
      reale(14816215,0x5c99263f881e3LL),
      // C4[6], coeff of eps^12, polynomial in n of order 14
      real(0x1b3ddeae39bf0000LL),-reale(7839,0x62697a1358000LL),
      reale(39400,0x7dae3b2360000LL),-reale(93477,0x2dd7a51de8000LL),
      reale(106917,0x5f76290ad0000LL),-reale(25706,0x4975ab7078000LL),
      -reale(70221,0xf8d5dabdc0000LL),reale(66679,0x434a03a4f8000LL),
      -reale(7926,0xc17b4a4650000LL),-reale(5104,0x4c6b9c2d98000LL),
      -reale(10825,0x972fc79ee0000LL),reale(8339,0xae0935c7d8000LL),
      -real(0xb5e35652d770000LL),-real(0xb97cf166cab8000LL),
      -real(0x1484ac4370939000LL),reale(14816215,0x5c99263f881e3LL),
      // C4[6], coeff of eps^11, polynomial in n of order 15
      real(0x1da928c9710000LL),real(0xef3463c3520000LL),
      real(0x1433e03669f30000LL),-reale(6121,0xc895edf4c0000LL),
      reale(32842,0x2af7b46f50000LL),-reale(85281,0xda67593ea0000LL),
      reale(113905,0x4294ec3770000LL),-reale(54341,1789231857LL<<19),
      -reale(49473,0x80d6dfd870000LL),reale(78594,0x71ba158da0000LL),
      -reale(27684,0xd5e2e99050000LL),-reale(5831,3589595121LL<<18),
      -reale(2437,0x3d76dec030000LL),reale(8713,0x93ccba19e0000LL),
      -reale(3467,0xfccc93810000LL),real(0xf2bb44edf33d000LL),
      reale(14816215,0x5c99263f881e3LL),
      // C4[6], coeff of eps^10, polynomial in n of order 16
      real(0xcab3dac70000LL),real(0x3665759289000LL),real(0x12ce11eabe2000LL),
      real(0x9df70180dbb000LL),real(0xdfd754eb8954000LL),
      -reale(4487,0x5dd2369613000LL),reale(25849,0xff24cd52c6000LL),
      -reale(73908,0x17b3db62e1000LL),reale(115119,0x8d3c9a9638000LL),
      -reale(83691,0x41fe3e02af000LL),-reale(14375,0xada6f2de56000LL),
      reale(76590,0xeb60670083000LL),-reale(52128,0x9a91d83ce4000LL),
      reale(7010,0x5a128dfcb5000LL),reale(3866,0xf6d75c088e000LL),
      real(0x469f50315e7e7000LL),-real(0x4bbe9f188165a200LL),
      reale(14816215,0x5c99263f881e3LL),
      // C4[6], coeff of eps^9, polynomial in n of order 17
      real(0x8ddfb274000LL),real(120826333LL<<18),real(0x6b145a40c000LL),
      real(0x1dc5136a58000LL),real(0xab5ca60ba4000LL),real(0x5e28748a970000LL),
      real(0x8cad0403953c000LL),-reale(3003,0xaeb1521f78000LL),
      reale(18707,0x350991ecd4000LL),-reale(59284,0x6845654460000LL),
      reale(107702,0xd776bbe6c000LL),-reale(107579,0xe340531948000LL),
      reale(33813,0xa464b8b604000LL),reale(48035,0x81a4fa0dd0000LL),
      -reale(64047,0xa4265c8064000LL),reale(31225,0xe027c1dce8000LL),
      -reale(5635,0xd5d68038cc000LL),-real(0x50368754849c400LL),
      reale(14816215,0x5c99263f881e3LL),
      // C4[6], coeff of eps^8, polynomial in n of order 18
      real(490704814080LL),real(0x13aa0f5a000LL),real(31022013LL<<17),
      real(0xc68497e6000LL),real(0x2fcbb8aac000LL),real(0xdd4302e72000LL),
      real(0x534405e9b8000LL),real(0x30298b6eefe000LL),
      real(0x4c5dcf34c0c4000LL),-real(0x6d574da684a76000LL),
      reale(11873,5016286141LL<<16),-reale(42009,0x509c0961ea000LL),
      reale(89073,0x6259ee06dc000LL),-reale(115683,0xae64a27b5e000LL),
      reale(82889,0x8f2a67cde8000LL),-reale(11935,0xb1dc537ad2000LL),
      -reale(33312,0xcf05a2430c000LL),reale(28876,0xae4eda7bba000LL),
      -reale(8101,0x83645851a5400LL),reale(14816215,0x5c99263f881e3LL),
      // C4[6], coeff of eps^7, polynomial in n of order 19
      real(7458340864LL),real(560703LL<<15),real(48303816704LL),
      real(522951LL<<18),real(426386014208LL),real(45283889LL<<15),
      real(0x56a252ac000LL),real(440127317LL<<16),real(0xa648bd1f4000LL),
      real(0x65fb114118000LL),real(0xacffeca0b3c000LL),
      -real(0x860da206139LL<<17),real(0x7d0a1c0732284000LL),
      -reale(7961,0x1b3e7a1f58000LL),reale(19682,0xa4af1c3bcc000LL),
      -reale(31917,0xccc8ef8390000LL),reale(34094,0x3798b7b14000LL),
      -reale(23101,0x583f152fc8000LL),reale(8983,0xdb34fa045c000LL),
      -real(0x5f40c0b45d798c00LL),reale(4938738,0x74330cbfd80a1LL),
      // C4[6], coeff of eps^6, polynomial in n of order 20
      real(651542528),real(1480134656),real(3538968576LL),real(8971595776LL),
      real(371371LL<<16),real(71493373952LL),real(230978592768LL),
      real(838422294528LL),real(0x334e2804000LL),real(0x106060339000LL),
      real(0x6e2b415ae000LL),real(0x484c62e3a3000LL),real(0x848c0aa1558000LL),
      -real(0xe0b56a0582f3000LL),real(0x745df25523d02000LL),
      -reale(8378,0x6c27f21289000LL),reale(23938,0x5996b3a2ac000LL),
      -reale(45881,0xd660d84d1f000LL),reale(58395,0x10d8591c56000LL),
      -reale(42673,0x513ba394b5000LL),reale(12954,0x665fd1a892600LL),
      reale(14816215,0x5c99263f881e3LL),
      // C4[7], coeff of eps^26, polynomial in n of order 0
      real(9763<<15),real(0x75209f8d91abLL),
      // C4[7], coeff of eps^25, polynomial in n of order 1
      real(239317LL<<16),real(5250319360LL),real(0x4082f7e0f93b2fLL),
      // C4[7], coeff of eps^24, polynomial in n of order 2
      real(179518703LL<<19),-real(591371495LL<<18),real(0x28b139bd9800LL),
      reale(3231,0x13f0854e6fdc3LL),
      // C4[7], coeff of eps^23, polynomial in n of order 3
      real(0x2cef3d4baf0000LL),-real(77130417375LL<<17),real(0xef66e7c50000LL),
      real(0x5431e6572400LL),reale(119549,0xe1c344562ad2fLL),
      // C4[7], coeff of eps^22, polynomial in n of order 4
      real(217227301LL<<22),-real(289844049LL<<20),real(78161061LL<<21),
      -real(250072603LL<<20),real(0x3ccfc393c000LL),
      reale(3856,0x72a333c0b70f1LL),
      // C4[7], coeff of eps^21, polynomial in n of order 5
      real(0x4e0ae513ee240000LL),-real(827903427791LL<<20),
      real(0xa247f543e5fLL<<18),-real(0x3412b66b53fLL<<19),
      -real(88149449003LL<<18),-real(0x22c21c78f4d000LL),
      reale(17095633,0x1c132c21ebd41LL),
      // C4[7], coeff of eps^20, polynomial in n of order 6
      real(0x17d653fb3b3LL<<21),-real(0x28623ac8329LL<<20),
      real(0x157258d15a9LL<<22),-real(0x11bb996f2dfLL<<20),
      real(568501848145LL<<21),-real(0x17b5bd88f85LL<<20),
      real(0x53401a2130be000LL),reale(17095633,0x1c132c21ebd41LL),
      // C4[7], coeff of eps^19, polynomial in n of order 7
      -real(0x83a0cdc49940000LL),-reale(2692,4590415189LL<<19),
      real(0x5a9e6c539a840000LL),-real(834402440151LL<<20),
      real(0x4606e5f7741c0000LL),-real(0x420b2360847LL<<19),
      -real(530800397043LL<<18),-real(0xe57fab5d571000LL),
      reale(17095633,0x1c132c21ebd41LL),
      // C4[7], coeff of eps^18, polynomial in n of order 8
      reale(3472,126556531LL<<23),-reale(5076,3517313787LL<<20),
      -real(76794078375LL<<21),-real(0x6a9c1a13021LL<<20),
      reale(2051,1043338611LL<<22),-real(704701202247LL<<20),
      real(0xfa27346673LL<<21),-real(0x245598aac6dLL<<20),
      real(0x69deaea556c4000LL),reale(17095633,0x1c132c21ebd41LL),
      // C4[7], coeff of eps^17, polynomial in n of order 9
      reale(15000,0xe6601a91a0000LL),-real(0x261369ca72fLL<<20),
      real(0x42e9870754860000LL),-reale(5748,0xcbf4457740000LL),
      real(0x3d07c1e90b320000LL),-real(0x3f02d96efefLL<<19),
      real(0x7fb986a3c79e0000LL),-real(0x995e2453d1fLL<<18),
      -real(0x4ae4d5f0bb60000LL),-real(0x2b86668e596d800LL),
      reale(17095633,0x1c132c21ebd41LL),
      // C4[7], coeff of eps^16, polynomial in n of order 10
      -real(0x73f9d78b0d9LL<<20),real(0x9cf538ea065LL<<19),
      reale(15740,149203411LL<<22),-reale(4248,1728572757LL<<19),
      -real(0x407b444d4cfLL<<20),-reale(4968,2121468799LL<<19),
      reale(2638,499248115LL<<21),real(0x88c04a730380000LL),
      real(0x44a3b895a7bLL<<20),-real(0x74a26c7b8a3LL<<19),
      real(0x855f1c455087000LL),reale(17095633,0x1c132c21ebd41LL),
      // C4[7], coeff of eps^15, polynomial in n of order 11
      reale(61154,0xdd701642e0000LL),-reale(66911,9396541691LL<<18),
      reale(7800,0xcd3506c5a0000LL),reale(6879,1489841009LL<<20),
      reale(13340,0xebc72e5460000LL),-reale(8995,2037240317LL<<18),
      -real(0x58226c8c268e0000LL),-reale(2527,381291855LL<<19),
      reale(3789,0xabee5235e0000LL),-real(0x7b29f7fc67fLL<<18),
      -real(0x7ff214bf2760000LL),-real(0x75bce0e31735800LL),
      reale(17095633,0x1c132c21ebd41LL),
      // C4[7], coeff of eps^14, polynomial in n of order 12
      -reale(101656,596927171LL<<22),reale(53043,574431381LL<<20),
      reale(45405,240861115LL<<21),-reale(76255,2673908009LL<<20),
      reale(23050,192030143LL<<23),reale(9407,3846737689LL<<20),
      reale(7022,1974859325LL<<21),-reale(12738,252856997LL<<20),
      real(0x137e788e9bfLL<<22),real(0x118e235259dLL<<20),
      reale(2782,635761855LL<<21),-real(0x61e77094421LL<<20),
      real(0x9e768b34c754000LL),reale(17095633,0x1c132c21ebd41LL),
      // C4[7], coeff of eps^13, polynomial in n of order 13
      -reale(21345,0xc6c0a8cac0000LL),reale(63537,3528773151LL<<20),
      -reale(101990,1331648317LL<<18),reale(73206,6215106713LL<<19),
      reale(21832,587136209LL<<18),-reale(78785,930736779LL<<21),
      reale(42984,0xe415720fc0000LL),reale(4706,912279695LL<<19),
      -real(0x6fb64418f6cc0000LL),-reale(11952,1697246539LL<<20),
      reale(6137,3764705851LL<<18),real(0x39d9405b105LL<<19),
      -real(779141568695LL<<18),-real(0x14a7906c9982d000LL),
      reale(17095633,0x1c132c21ebd41LL),
      // C4[7], coeff of eps^12, polynomial in n of order 14
      -real(254469508501LL<<21),reale(2615,141135587LL<<20),
      -reale(16754,480949921LL<<22),reale(54113,1487459045LL<<20),
      -reale(98062,1801972559LL<<21),reale(91200,2801526327LL<<20),
      -reale(9603,108846763LL<<23),-reale(69011,498726663LL<<20),
      reale(62980,2002280887LL<<21),-reale(11145,4221789365LL<<20),
      -reale(7195,1009585291LL<<22),-reale(4457,3739558579LL<<20),
      reale(7974,18407933LL<<21),-reale(2668,664195297LL<<20),
      real(0x8b8039451326000LL),reale(17095633,0x1c132c21ebd41LL),
      // C4[7], coeff of eps^11, polynomial in n of order 15
      -real(5353180065LL<<18),-real(25442595013LL<<19),
      -real(0x4cec268118c0000LL),real(0x702c4e5b497LL<<20),
      -reale(12304,5733646405LL<<18),reale(43346,2744696673LL<<19),
      -reale(88871,6285139975LL<<18),reale(103468,468195229LL<<21),
      -reale(46365,0xbad7731a40000LL),-reale(41349,1257587961LL<<19),
      reale(72365,0x9597fe7540000LL),-reale(36580,2571848483LL<<20),
      real(0xc0cfef1c9f3LL<<18),reale(3419,2944620333LL<<19),
      real(0x5d00262e0cc40000LL),-real(0x44e0e913b4a79000LL),
      reale(17095633,0x1c132c21ebd41LL),
      // C4[7], coeff of eps^10, polynomial in n of order 16
      -real(1386231LL<<24),-real(109742265LL<<20),-real(354075457LL<<21),
      -real(7044729419LL<<20),-real(48190848741LL<<22),
      real(0x4592e53c723LL<<20),-reale(8214,1225367123LL<<21),
      reale(31749,3931639185LL<<20),-reale(73861,194985719LL<<23),
      reale(105371,3738827519LL<<20),-reale(81325,759307621LL<<21),
      reale(5533,2607378797LL<<20),reale(54935,128097033LL<<22),
      -reale(54849,213867813LL<<20),reale(23331,2117756809LL<<21),
      -reale(3571,955076279LL<<20),-real(0xa766ab1fb094000LL),
      reale(17095633,0x1c132c21ebd41LL),
      // C4[7], coeff of eps^9, polynomial in n of order 17
      -real(9271959LL<<16),-real(2137131LL<<20),-real(0x8adb5490000LL),
      -real(374926717LL<<17),-real(5060508635LL<<16),-real(0xc549443040000LL),
      -real(0x1658a10fa0d0000LL),real(0x250f39cc17720000LL),
      -reale(4742,48259999LL<<16),reale(20239,6692003029LL<<19),
      -reale(53602,0x26a4a24510000LL),reale(92339,8168900207LL<<17),
      -reale(101236,0x6fb3cfe30000LL),reale(59785,2334542613LL<<18),
      real(0x5c1211516deb0000LL),-reale(32944,0x86c05c8b60000LL),
      reale(24775,0x5aee521590000LL),-reale(6657,0xade066fea8c00LL),
      reale(17095633,0x1c132c21ebd41LL),
      // C4[7], coeff of eps^8, polynomial in n of order 18
      -real(31473LL<<19),-real(194623LL<<18),-real(41393LL<<22),
      -real(2533665LL<<18),-real(5617311LL<<19),-real(60523827LL<<18),
      -real(107394483LL<<20),-real(4758923477LL<<18),-real(73625727245LL<<19),
      real(0xf5289483e640000LL),-reale(2141,878914353LL<<21),
      reale(10163,0xf2a381edc0000LL),-reale(30731,8395289531LL<<19),
      reale(63101,0xdb7b98c940000LL),-reale(89756,3102076305LL<<20),
      reale(87316,6648120707LL<<18),-reale(55353,8132528169LL<<19),
      reale(20534,9081852529LL<<18),-reale(3368,0xf233ddc1a2800LL),
      reale(17095633,0x1c132c21ebd41LL),
      // C4[7], coeff of eps^7, polynomial in n of order 19
      -real(4693<<16),-real(6435<<17),-real(37895LL<<16),-real(7579LL<<20),
      -real(428505LL<<16),-real(854413LL<<17),-real(7933835LL<<16),
      -real(11246865LL<<18),-real(338155741LL<<16),-real(0xee3402ee0000LL),
      -real(0x1efc2a618f0000LL),real(517531990885LL<<19),
      -real(0x243e4ae81d610000LL),reale(3081,0xb7f72703e0000LL),
      -reale(10639,0x4442fa8130000LL),reale(25534,4122358181LL<<18),
      -reale(43524,0x45cc2f5650000LL),reale(51336,0x52534b86a0000LL),
      -reale(35935,0x6cd3e81170000LL),reale(10668,0x544ee8e52d400LL),
      reale(17095633,0x1c132c21ebd41LL),
      // C4[8], coeff of eps^26, polynomial in n of order 0
      real(1703<<17),real(0x7c72a9866ac5bLL),
      // C4[8], coeff of eps^25, polynomial in n of order 1
      -real(177229LL<<20),real(727155LL<<16),real(0x491cf6cbc520f1LL),
      // C4[8], coeff of eps^24, polynomial in n of order 2
      -real(9929683361LL<<18),-real(175790329LL<<17),-real(0x88fc23ec000LL),
      reale(40280,0xc561288d94a7fLL),
      // C4[8], coeff of eps^23, polynomial in n of order 3
      -real(11862711753LL<<19),real(5010641713LL<<20),-real(14709027619LL<<19),
      real(0x62bf29e3e8000LL),reale(135489,0xddbb2b5096ef1LL),
      // C4[8], coeff of eps^22, polynomial in n of order 4
      -real(6145646087LL<<23),real(131879372361LL<<21),
      -real(33613471903LL<<22),-real(3256336589LL<<21),
      -real(0xacc29a2990000LL),reale(1761368,0x42813317aa23dLL),
      // C4[8], coeff of eps^21, polynomial in n of order 5
      -real(0x6a942373c4bLL<<19),real(0x26ec3bfe245LL<<21),
      -real(0x8f791d3a3680000LL),real(0x11c215e6335LL<<20),
      -real(0x2c38227cc2fLL<<19),real(0x4429220c0f48000LL),
      reale(19375050,0xdb8d32044f89fLL),
      // C4[8], coeff of eps^20, polynomial in n of order 6
      -reale(2934,444315969LL<<20),real(0x6a3b64139b1LL<<19),
      -real(467101336651LL<<21),real(0x8d6914ca9b7LL<<19),
      -real(0x1951684536bLL<<20),-real(344981960323LL<<19),
      -real(0x1536c8746170000LL),reale(19375050,0xdb8d32044f89fLL),
      // C4[8], coeff of eps^19, polynomial in n of order 7
      -reale(3511,3705843547LL<<19),-real(0x204aea957e3LL<<20),
      -reale(2145,1225061153LL<<19),real(0x33d58e2ac0fLL<<21),
      -real(10655273223LL<<19),real(0x21f191654dfLL<<20),
      -real(0x4229ae891cdLL<<19),real(0x53ff9bb26958000LL),
      reale(19375050,0xdb8d32044f89fLL),
      // C4[8], coeff of eps^18, polynomial in n of order 8
      reale(2327,223378273LL<<24),reale(3002,681494021LL<<21),
      -reale(5098,180818405LL<<22),-real(5074441169LL<<21),
      -real(428729715071LL<<23),real(0x3cce86cb309LL<<21),
      -real(434398966071LL<<22),-real(156882519885LL<<21),
      -real(0x33e11620e250000LL),reale(19375050,0xdb8d32044f89fLL),
      // C4[8], coeff of eps^17, polynomial in n of order 9
      -reale(6703,1474120015LL<<19),reale(14458,426935549LL<<22),
      -real(511886207649LL<<19),-real(39076914681LL<<20),
      -reale(5282,7254660115LL<<19),real(0x33346658ebdLL<<21),
      real(0xd2bcdb640d80000LL),real(0x48aecde6f2dLL<<20),
      -real(0x66a76bcf857LL<<19),real(0x650db91f67c8000LL),
      reale(19375050,0xdb8d32044f89fLL),
      // C4[8], coeff of eps^16, polynomial in n of order 10
      -reale(41674,2212282947LL<<19),-reale(7593,6666692295LL<<18),
      real(0x22d5b967639LL<<21),reale(15266,7870015191LL<<18),
      -reale(4856,4082442485LL<<19),-real(0x76ec691ccd2c0000LL),
      -reale(3302,2416313159LL<<20),reale(3252,0xd63fbdd4c0000LL),
      -real(0xa56dc66b5380000LL),-real(0x5b75ff5133c0000LL),
      -real(0x7d0ead839928000LL),reale(19375050,0xdb8d32044f89fLL),
      // C4[8], coeff of eps^15, polynomial in n of order 11
      reale(6774,8529353663LL<<19),reale(68916,757502869LL<<20),
      -reale(58358,4821135835LL<<19),reale(3030,627685345LL<<22),
      reale(8321,1974413611LL<<19),reale(11199,994841075LL<<20),
      -reale(10210,402696815LL<<19),-real(0x10cbfe9c35fLL<<21),
      -real(0x88d945e9f480000LL),reale(2764,2004030417LL<<20),
      -real(0xa3f22386a83LL<<19),real(0x6eb0baaefa68000LL),
      reale(19375050,0xdb8d32044f89fLL),
      // C4[8], coeff of eps^14, polynomial in n of order 12
      reale(80789,157273055LL<<23),-reale(92413,883895019LL<<21),
      reale(33037,752031121LL<<22),reale(52633,1725093895LL<<21),
      -reale(71257,198988971LL<<24),reale(21774,462183721LL<<21),
      reale(9867,923099607LL<<22),reale(2235,815700763LL<<21),
      -reale(11863,140786955LL<<23),reale(4226,1910142077LL<<21),
      real(854212143197LL<<22),real(169477509103LL<<21),
      -real(0x1429c96cdeb90000LL),reale(19375050,0xdb8d32044f89fLL),
      // C4[8], coeff of eps^13, polynomial in n of order 13
      reale(7986,2577537059LL<<19),-reale(31049,1388317679LL<<21),
      reale(71398,6484881669LL<<19),-reale(96607,3840488041LL<<20),
      reale(60036,7375804551LL<<19),reale(24533,301249307LL<<22),
      -reale(73258,7729848599LL<<19),reale(45499,3314021057LL<<20),
      -real(0x3ea6bf07b95LL<<19),-reale(6268,968456357LL<<21),
      -reale(5889,8030103219LL<<19),reale(7129,2010513003LL<<20),
      -reale(2060,6603694641LL<<19),real(0x4aa8326c4b38000LL),
      reale(19375050,0xdb8d32044f89fLL),
      // C4[8], coeff of eps^12, polynomial in n of order 14
      real(110457315575LL<<20),-real(0x55e7441aebbLL<<19),
      reale(5489,1587292819LL<<21),-reale(23107,1250112621LL<<19),
      reale(59020,876513493LL<<20),-reale(93669,6579434335LL<<19),
      reale(83160,151752881LL<<22),-reale(14191,6428793873LL<<19),
      -reale(55802,147123789LL<<20),reale(63340,7698024701LL<<19),
      -reale(24299,306146767LL<<21),-reale(2685,2028352693LL<<19),
      reale(2706,1245782417LL<<20),real(0xd3e9bdc0259LL<<19),
      -real(0x3e4f75bd92cb0000LL),reale(19375050,0xdb8d32044f89fLL),
      // C4[8], coeff of eps^11, polynomial in n of order 15
      real(349722603LL<<19),real(1945948591LL<<20),real(0xe000999c080000LL),
      -real(852080688837LL<<21),reale(3397,2932652343LL<<19),
      -reale(15561,3567671555LL<<20),reale(44311,3271472077LL<<19),
      -reale(82040,520836183LL<<22),reale(95750,3608174083LL<<19),
      -reale(56326,3054118709LL<<20),-reale(14075,6930316647LL<<19),
      reale(56094,1163367017LL<<21),-reale(46280,7703552945LL<<19),
      reale(17576,4216930841LL<<20),-reale(2261,6650055195LL<<19),
      -real(0xc8e19a260718000LL),reale(19375050,0xdb8d32044f89fLL),
      // C4[8], coeff of eps^10, polynomial in n of order 16
      real(53199LL<<25),real(4832235LL<<21),real(18086833LL<<22),
      real(422991569LL<<21),real(3456128781LL<<23),-real(417864400569LL<<21),
      real(0x1c1175e6463LL<<22),-reale(9006,876789843LL<<21),
      reale(28706,92601679LL<<24),-reale(61776,681174429LL<<21),
      reale(90600,218403669LL<<22),-reale(86125,255162935LL<<21),
      reale(41671,220860591LL<<23),reale(9900,1945963071LL<<21),
      -reale(31426,812677625LL<<22),reale(21427,717745189LL<<21),
      -reale(5580,0x8f2cafdf0000LL),reale(19375050,0xdb8d32044f89fLL),
      // C4[8], coeff of eps^9, polynomial in n of order 17
      real(47583LL<<19),real(12411LL<<23),real(964865LL<<19),
      real(2862477LL<<20),real(45013059LL<<19),real(139025201LL<<21),
      real(19339324389LL<<19),-real(313753792905LL<<20),
      real(0x5b827ae7827LL<<19),-reale(4043,135949957LL<<22),
      reale(14485,4274671945LL<<19),-reale(36111,1380328223LL<<20),
      reale(64526,2642754443LL<<19),-reale(82901,1325528645LL<<21),
      reale(74876,5403462445LL<<19),-reale(44997,1726039605LL<<20),
      reale(16070,4300719215LL<<19),-reale(2566,0xd0ea909388000LL),
      reale(19375050,0xdb8d32044f89fLL),
      // C4[8], coeff of eps^8, polynomial in n of order 18
      real(1053<<18),real(7293<<17),real(1749LL<<21),real(121635LL<<17),
      real(309043LL<<18),real(3853577LL<<17),real(8003583LL<<19),
      real(420632751LL<<17),real(7839064905LL<<18),-real(550302356331LL<<17),
      real(754118043861LL<<20),-real(0x433703efa18a0000LL),
      reale(4345,0xa637f297c0000LL),-reale(12473,0x9f7aaa1be0000LL),
      reale(26308,41230677LL<<19),-reale(40979,0xc6d64da720000LL),
      reale(45533,1464249973LL<<18),-reale(30801,0xcafec6ea60000LL),
      reale(8983,0xdb34fa045c000LL),reale(19375050,0xdb8d32044f89fLL),
      // C4[9], coeff of eps^26, polynomial in n of order 0
      real(3679<<17),real(0xf744df0e6c69LL),
      // C4[9], coeff of eps^25, polynomial in n of order 1
      -real(48841LL<<20),-real(336765LL<<16),real(0x19892cc90d5217fLL),
      // C4[9], coeff of eps^24, polynomial in n of order 2
      real(24659297LL<<26),-real(64440233LL<<25),real(414215087LL<<20),
      reale(45019,0xaf6c96bc5ad9dLL),
      // C4[9], coeff of eps^23, polynomial in n of order 3
      real(0x55f7a92f661LL<<19),-real(0x115bb8ed6d9LL<<20),
      -real(198450589909LL<<19),-real(0xb7278b5afc8000LL),
      reale(21654468,0x9b0737e6b33fdLL),
      // C4[9], coeff of eps^22, polynomial in n of order 4
      real(52440485279LL<<23),-real(8663417169LL<<21),real(29836121623LL<<22),
      -real(64017745099LL<<21),real(0x517eabcb370000LL),
      reale(1968588,0xe17edcf27917LL),
      // C4[9], coeff of eps^21, polynomial in n of order 5
      real(0x29ddd14eea5LL<<19),-real(683397694747LL<<21),
      real(0x8a9d0ded323LL<<19),-real(0x12a27ad79ebLL<<20),
      -real(365440747903LL<<19),-real(0x1a278f54ba58000LL),
      reale(21654468,0x9b0737e6b33fdLL),
      // C4[9], coeff of eps^20, polynomial in n of order 6
      -real(258517517319LL<<23),-reale(2449,779805879LL<<22),
      real(333316352075LL<<24),real(89662817151LL<<22),
      real(311028248083LL<<23),-real(514657501435LL<<22),
      real(0x42edd4687ca0000LL),reale(21654468,0x9b0737e6b33fdLL),
      // C4[9], coeff of eps^19, polynomial in n of order 7
      reale(4708,5969586757LL<<19),-reale(3967,769306643LL<<20),
      -real(0x4aa5ebcacc1LL<<19),-real(0x2338c762cc1LL<<21),
      real(0xdfce640f299LL<<19),-real(0xee4b32a131LL<<20),
      -real(544152989037LL<<19),-real(0x392f1a561e88000LL),
      reale(21654468,0x9b0737e6b33fdLL),
      // C4[9], coeff of eps^18, polynomial in n of order 8
      reale(10651,141986579LL<<24),reale(2483,579021431LL<<21),
      real(0x171656e9461LL<<22),-reale(5106,1475723195LL<<21),
      real(424307179891LL<<23),real(353847768099LL<<21),
      real(0x12b721ceb0bLL<<22),-real(0x16800175f8fLL<<21),
      real(0x4cd03e8801b0000LL),reale(21654468,0x9b0737e6b33fdLL),
      // C4[9], coeff of eps^17, polynomial in n of order 9
      -reale(12598,568269079LL<<19),-reale(5705,584195995LL<<22),
      reale(14434,7986153127LL<<19),-real(0x53c43a7b401LL<<20),
      -real(0xc35a517653bLL<<19),-reale(3831,956767451LL<<21),
      reale(2686,1674924547LL<<19),real(257168565717LL<<20),
      -real(441477690591LL<<19),-real(0x7fc3df35f858000LL),
      reale(21654468,0x9b0737e6b33fdLL),
      // C4[9], coeff of eps^16, polynomial in n of order 10
      reale(73082,82142393LL<<24),-reale(36372,269994261LL<<23),
      -reale(8446,14363443LL<<26),reale(3801,517661957LL<<23),
      reale(13381,17268719LL<<24),-reale(7345,464489969LL<<23),
      -real(211182139987LL<<25),-real(326075858199LL<<23),
      reale(2675,147207653LL<<24),-real(589098042253LL<<23),
      real(0x4cdddf4aa2c0000LL),reale(21654468,0x9b0737e6b33fdLL),
      // C4[9], coeff of eps^15, polynomial in n of order 11
      -reale(70228,3204573753LL<<19),-reale(3665,997072835LL<<20),
      reale(67162,8124243837LL<<19),-reale(56077,490889175LL<<22),
      reale(5918,7641432915LL<<19),reale(10294,3043462539LL<<20),
      reale(5723,2367840009LL<<19),-reale(10993,938348055LL<<21),
      reale(2675,6462906463LL<<19),real(0x3a39e82b059LL<<20),
      real(0xb502c3128a80000LL),-real(0x1358f80d9c038000LL),
      reale(21654468,0x9b0737e6b33fdLL),
      // C4[9], coeff of eps^14, polynomial in n of order 12
      -reale(45939,459571779LL<<23),reale(81202,1438384695LL<<21),
      -reale(84011,799287213LL<<22),reale(28155,23125821LL<<21),
      reale(46736,266493023LL<<24),-reale(68202,2086496557LL<<21),
      reale(29667,382040805LL<<22),reale(5608,647828697LL<<21),
      -reale(4401,355658689LL<<23),-reale(6763,1986460369LL<<21),
      reale(6284,996052535LL<<22),-real(0x31efd65ac4bLL<<21),
      real(0x21519ecdd470000LL),reale(21654468,0x9b0737e6b33fdLL),
      // C4[9], coeff of eps^13, polynomial in n of order 13
      -reale(2321,7142809405LL<<19),reale(11407,565078561LL<<21),
      -reale(34996,6437021595LL<<19),reale(70236,3027507143LL<<20),
      -reale(89750,2730116057LL<<19),reale(59647,532152523LL<<22),
      reale(10736,8218389321LL<<19),-reale(61423,3588044783LL<<20),
      reale(52845,5572842763LL<<19),-reale(15060,1433211893LL<<21),
      -reale(4428,664807251LL<<19),real(0x7ac3d0f14dbLL<<20),
      real(0xe0ec3bda56fLL<<19),-real(0x3854598234228000LL),
      reale(21654468,0x9b0737e6b33fdLL),
      // C4[9], coeff of eps^12, polynomial in n of order 14
      -real(2301546703LL<<23),real(147057720589LL<<22),
      -real(359161692259LL<<24),reale(7105,778398699LL<<22),
      -reale(23999,359671965LL<<23),reale(54661,162239065LL<<22),
      -reale(84322,1670081LL<<25),reale(82245,254480119LL<<22),
      -reale(34604,180181675LL<<23),-reale(26937,29999003LL<<22),
      reale(54122,167282399LL<<24),-reale(38795,392755901LL<<22),
      reale(13349,275194759LL<<23),-real(0x161047343cfLL<<22),
      -real(0xd052410afde0000LL),reale(21654468,0x9b0737e6b33fdLL),
      // C4[9], coeff of eps^11, polynomial in n of order 15
      -real(33392709LL<<19),-real(215980657LL<<20),-real(15729792143LL<<19),
      real(133575397083LL<<21),-real(0x5183d845f39LL<<19),
      reale(3762,3694580381LL<<20),-reale(14049,8382023811LL<<19),
      reale(36325,545288329LL<<22),-reale(66629,6532309165LL<<19),
      reale(85703,4252949035LL<<20),-reale(71810,6020466679LL<<19),
      reale(27704,1818174537LL<<21),reale(15098,409579871LL<<19),
      -reale(29448,934474951LL<<20),reale(18689,3410848533LL<<19),
      -reale(4754,0x309583fd38000LL),reale(21654468,0x9b0737e6b33fdLL),
      // C4[9], coeff of eps^10, polynomial in n of order 16
      -real(893LL<<25),-real(92625LL<<21),-real(399779LL<<22),
      -real(10904803LL<<21),-real(105333207LL<<23),real(15302554267LL<<21),
      -real(86594321625LL<<22),real(0xfe4052cb09LL<<21),
      -reale(2108,118544893LL<<24),reale(6191,505418439LL<<21),
      -reale(13344,231933903LL<<22),reale(21384,2064906293LL<<21),
      -reale(25319,426528669LL<<23),reale(21525,1826875827LL<<21),
      -reale(12380,255070469LL<<22),reale(4285,1002542497LL<<21),
      -real(0x29d9aac7ec250000LL),reale(7218156,0x33ad12a23bbffLL),
      // C4[9], coeff of eps^9, polynomial in n of order 17
      -real(969<<19),-real(285LL<<23),-real(25175LL<<19),-real(85595LL<<20),
      -real(1557829LL<<19),-real(5632151LL<<21),-real(929304915LL<<19),
      real(18163686975LL<<20),-real(446826699585LL<<19),
      real(387249806307LL<<22),-real(0xd080dd307cfLL<<19),
      reale(5560,386556505LL<<20),-reale(13900,1932782525LL<<19),
      reale(26517,756597539LL<<21),-reale(38450,1381788619LL<<19),
      reale(40711,4015921907LL<<20),-reale(26784,1441242297LL<<19),
      reale(7700,0x72bfb1ba98000LL),reale(21654468,0x9b0737e6b33fdLL),
      // C4[10], coeff of eps^26, polynomial in n of order 0
      -real(5057<<18),real(0x10edb70f760db7LL),
      // C4[10], coeff of eps^25, polynomial in n of order 1
      -real(4901LL<<25),real(14157LL<<21),real(0x4082f7e0f93b2fLL),
      // C4[10], coeff of eps^24, polynomial in n of order 2
      -real(8688787LL<<25),-real(2064227LL<<24),-real(9250461LL<<21),
      reale(7108,0x5f112546294adLL),
      // C4[10], coeff of eps^23, polynomial in n of order 3
      real(363248763LL<<25),real(3123548769LL<<26),-real(5801671447LL<<25),
      real(14176223919LL<<21),reale(3419126,0x9f3708d39590dLL),
      // C4[10], coeff of eps^22, polynomial in n of order 4
      -real(490568702783LL<<22),real(0x422ec2346b3LL<<20),
      -real(446296001151LL<<21),-real(174052882927LL<<20),
      -real(0xed1818f25bLL<<17),reale(23933886,0x5a813dc916f5bLL),
      // C4[10], coeff of eps^21, polynomial in n of order 5
      -reale(2585,173491781LL<<22),real(226504425479LL<<24),
      real(118144668093LL<<22),real(325346294119LL<<23),
      -real(464280225409LL<<22),real(919092918513LL<<18),
      reale(23933886,0x5a813dc916f5bLL),
      // C4[10], coeff of eps^20, polynomial in n of order 6
      -reale(2656,138725573LL<<22),-real(0x1a9c614c5c3LL<<21),
      -real(765139808215LL<<23),real(0x32058af918bLL<<21),
      -real(117685929879LL<<22),-real(106680176295LL<<21),
      -real(0xf144800341LL<<18),reale(23933886,0x5a813dc916f5bLL),
      // C4[10], coeff of eps^19, polynomial in n of order 7
      reale(3432,329072245LL<<22),reale(2930,283183745LL<<23),
      -reale(4577,1044295185LL<<22),real(34786730571LL<<24),
      real(54685893801LL<<22),real(647412775723LL<<23),
      -real(676279973341LL<<22),real(0xe9c610e7bdLL<<18),
      reale(23933886,0x5a813dc916f5bLL),
      // C4[10], coeff of eps^18, polynomial in n of order 8
      -reale(11084,235058537LL<<23),reale(11858,1143524977LL<<20),
      real(0x2545fd77485LL<<21),-real(0x307c82cee9dLL<<20),
      -reale(4103,193833065LL<<22),reale(2140,2163909077LL<<20),
      real(446041302231LL<<21),-real(54302113593LL<<20),
      -real(0x7f8004b3e7a0000LL),reale(23933886,0x5a813dc916f5bLL),
      // C4[10], coeff of eps^17, polynomial in n of order 9
      -reale(16525,309616105LL<<23),-reale(12873,31213113LL<<26),
      -real(811482588455LL<<23),reale(13789,37992821LL<<24),
      -reale(4637,221662373LL<<23),-real(274288421561LL<<25),
      -real(562238052579LL<<23),reale(2541,30117927LL<<24),
      -real(493061811809LL<<23),real(451991259993LL<<19),
      reale(23933886,0x5a813dc916f5bLL),
      // C4[10], coeff of eps^16, polynomial in n of order 10
      -reale(4541,277413243LL<<23),reale(9880,364937465LL<<22),
      -reale(5569,118974143LL<<25),-real(671603509225LL<<22),
      real(626562721155LL<<23),real(0x126f9949db5LL<<22),
      -real(372257463743LL<<24),real(225505748691LL<<22),
      real(72729834113LL<<23),real(40056084593LL<<22),
      -real(360891225041LL<<19),reale(3419126,0x9f3708d39590dLL),
      // C4[10], coeff of eps^15, polynomial in n of order 11
      reale(82722,490551845LL<<23),-reale(64701,63547469LL<<24),
      real(116234844999LL<<23),reale(58506,49315063LL<<26),
      -reale(58413,433206103LL<<23),reale(16792,103491845LL<<24),
      reale(8555,183955915LL<<23),-reale(2317,84066185LL<<25),
      -reale(7194,11825619LL<<23),reale(5493,119743831LL<<24),
      -real(667673139889LL<<23),real(58009080297LL<<19),
      reale(23933886,0x5a813dc916f5bLL),
      // C4[10], coeff of eps^14, polynomial in n of order 12
      reale(18981,396462873LL<<22),-reale(46078,2701457279LL<<20),
      reale(76015,1126083519LL<<21),-reale(79652,3524648005LL<<20),
      reale(36586,273717939LL<<23),reale(28474,1008822389LL<<20),
      -reale(61326,649857063LL<<21),reale(42595,3757087663LL<<20),
      -reale(8326,955589709LL<<22),-reale(5139,3984305559LL<<20),
      real(0x284545d9df3LL<<21),real(0x72b007891a3LL<<20),
      -real(0x33009c87a9620000LL),reale(23933886,0x5a813dc916f5bLL),
      // C4[10], coeff of eps^13, polynomial in n of order 13
      real(543312976219LL<<22),-reale(3062,112501267LL<<24),
      reale(11988,59347917LL<<22),-reale(32439,32307605LL<<23),
      reale(61980,821355519LL<<22),-reale(81948,80095793LL<<25),
      reale(67313,1055324017LL<<22),-reale(16748,109351667LL<<23),
      -reale(34832,1017554013LL<<22),reale(50577,16270159LL<<24),
      -reale(32450,61276651LL<<22),reale(10213,501613231LL<<23),
      -real(913358656441LL<<22),-real(0xcb30b375e9c0000LL),
      reale(23933886,0x5a813dc916f5bLL),
      // C4[10], coeff of eps^12, polynomial in n of order 14
      real(553451171LL<<22),-real(41782403663LL<<21),real(122732484303LL<<23),
      -real(0x2eaf28525b9LL<<21),reale(6402,476837273LL<<22),
      -reale(19347,183862947LL<<21),reale(42585,247358789LL<<24),
      -reale(68666,1206346765LL<<21),reale(79038,878189199LL<<22),
      -reale(58930,1207602423LL<<21),reale(17031,374958661LL<<23),
      reale(18189,4759455LL<<21),-reale(27348,414989947LL<<22),
      reale(16435,1788023477LL<<21),-reale(4106,0xe7ddb41f40000LL),
      reale(23933886,0x5a813dc916f5bLL),
      // C4[10], coeff of eps^11, polynomial in n of order 15
      real(259293LL<<22),real(1935549LL<<23),real(164593143LL<<22),
      -real(1654671183LL<<24),real(83533307473LL<<22),
      -real(296200453241LL<<23),reale(2600,89083243LL<<22),
      -reale(8792,113023813LL<<25),reale(22203,397075141LL<<22),
      -reale(42668,107392175LL<<23),reale(62615,179754463LL<<22),
      -reale(69317,125076421LL<<24),reale(56036,868613689LL<<22),
      -reale(31065,284420581LL<<23),reale(10475,628806483LL<<22),
      -real(0x6470cd13038c0000LL),reale(23933886,0x5a813dc916f5bLL),
      // C4[10], coeff of eps^10, polynomial in n of order 16
      real(133LL<<24),real(15675LL<<20),real(77539LL<<21),real(2448017LL<<20),
      real(27681423LL<<22),-real(4770431897LL<<20),real(32525672025LL<<21),
      -real(503497402947LL<<20),real(327672913029LL<<23),
      -reale(2314,857372269LL<<20),reale(6672,231933903LL<<21),
      -reale(14969,2031875351LL<<20),reale(26346,292729733LL<<22),
      -reale(36032,1727726401LL<<20),reale(36664,1180419909LL<<21),
      -reale(23570,290549227LL<<20),reale(6696,0xabcf39720000LL),
      reale(23933886,0x5a813dc916f5bLL),
      // C4[11], coeff of eps^26, polynomial in n of order 0
      real(611LL<<23),real(0xe6baee73ea363LL),
      // C4[11], coeff of eps^25, polynomial in n of order 1
      -real(76597LL<<26),-real(1573935LL<<21),real(0x477bca00497fe9bfLL),
      // C4[11], coeff of eps^24, polynomial in n of order 2
      real(5977365LL<<29),-real(9705069LL<<28),real(85309807LL<<22),
      reale(54497,0x83837319e73d9LL),
      // C4[11], coeff of eps^23, polynomial in n of order 3
      real(66340583679LL<<26),-real(4467880351LL<<27),-real(2404066379LL<<26),
      -real(68755156353LL<<21),reale(26213304,0x19fb43ab7aab9LL),
      // C4[11], coeff of eps^22, polynomial in n of order 4
      real(257415529LL<<33),real(402685503LL<<30),real(652792679LL<<32),
      -real(1631824579LL<<30),real(23005724469LL<<23),
      reale(26213304,0x19fb43ab7aab9LL),
      // C4[11], coeff of eps^21, polynomial in n of order 5
      -real(453253333179LL<<23),-real(222902987187LL<<25),
      real(749255628291LL<<23),-real(3378231395LL<<24),
      -real(18492933151LL<<23),-real(0xf79dae93c9LL<<18),
      reale(26213304,0x19fb43ab7aab9LL),
      // C4[11], coeff of eps^20, polynomial in n of order 6
      reale(4082,27256381LL<<26),-reale(3843,110832251LL<<25),
      -real(11608614857LL<<27),-real(12679113309LL<<25),
      real(80017732991LL<<26),-real(73865834735LL<<25),
      real(381345882225LL<<19),reale(26213304,0x19fb43ab7aab9LL),
      // C4[11], coeff of eps^19, polynomial in n of order 7
      reale(8515,117356323LL<<23),reale(2727,54002259LL<<24),
      real(95356337593LL<<23),-reale(4154,53817247LL<<25),
      real(882540340143LL<<23),real(80081392881LL<<24),real(12076046661LL<<23),
      -real(0x7d57ec14bd40000LL),reale(26213304,0x19fb43ab7aab9LL),
      // C4[11], coeff of eps^18, polynomial in n of order 8
      -reale(12505,951035LL<<32),-reale(6194,13758139LL<<28),
      reale(12920,711829LL<<30),-reale(2328,16199449LL<<28),
      -reale(2121,1768403LL<<31),-real(23812716991LL<<28),
      reale(2380,3848439LL<<30),-real(12910651229LL<<28),
      real(75285764519LL<<21),reale(26213304,0x19fb43ab7aab9LL),
      // C4[11], coeff of eps^17, polynomial in n of order 9
      reale(63004,136371221LL<<24),-reale(23253,27876759LL<<27),
      -reale(10033,153577157LL<<24),reale(4968,6981291LL<<25),
      reale(9826,264428161LL<<24),-reale(8260,44635479LL<<26),
      real(151361303079LL<<24),real(120235734969LL<<25),
      real(86414162541LL<<24),-real(0x22b971cd551LL<<19),
      reale(26213304,0x19fb43ab7aab9LL),
      // C4[11], coeff of eps^16, polynomial in n of order 10
      -reale(42751,388403LL<<27),-reale(21692,36263017LL<<26),
      reale(62310,8348465LL<<29),-reale(46929,14252519LL<<26),
      reale(7047,32285691LL<<27),reale(9444,49195723LL<<26),
      -real(6177911663LL<<28),-reale(7300,34477811LL<<26),
      reale(4777,27041001LL<<27),-real(65141092289LL<<26),
      -real(44359884933LL<<20),reale(26213304,0x19fb43ab7aab9LL),
      // C4[11], coeff of eps^15, polynomial in n of order 11
      -reale(54975,7679265LL<<24),reale(76586,39901517LL<<25),
      -reale(65943,41351227LL<<24),reale(16034,25331417LL<<27),
      reale(40019,7760011LL<<24),-reale(57816,89862917LL<<25),
      reale(33374,245812081LL<<24),-reale(3538,1406183LL<<26),
      -reale(5247,183755081LL<<24),real(95390660393LL<<25),
      real(490233600157LL<<24),-real(0x5c9b8397461LL<<19),
      reale(26213304,0x19fb43ab7aab9LL),
      // C4[11], coeff of eps^14, polynomial in n of order 12
      -reale(5607,843033LL<<31),reale(17587,15869457LL<<28),
      -reale(40024,1555693LL<<30),reale(66142,8872067LL<<28),
      -reale(76302,93659LL<<32),reale(52531,9300813LL<<28),
      -reale(2628,1860027LL<<30),-reale(39200,13058241LL<<28),
      reale(46365,693773LL<<31),-reale(27149,11145943LL<<28),
      reale(7864,1548807LL<<30),-real(7962030629LL<<28),
      -real(412888159761LL<<21),reale(26213304,0x19fb43ab7aab9LL),
      // C4[11], coeff of eps^13, polynomial in n of order 13
      -real(41790907379LL<<23),real(76324858599LL<<25),
      -reale(2753,104130613LL<<23),reale(9526,224954257LL<<24),
      -reale(24463,68256343LL<<23),reale(47309,29696781LL<<26),
      -reale(68504,135442201LL<<23),reale(71563,253449815LL<<24),
      -reale(47678,505267003LL<<23),reale(8918,86883405LL<<25),
      reale(19900,176522371LL<<23),-reale(25292,67707491LL<<24),
      reale(14565,326677345LL<<23),-reale(3589,0xb1b7cdcc40000LL),
      reale(26213304,0x19fb43ab7aab9LL),
      // C4[11], coeff of eps^12, polynomial in n of order 14
      -real(2670507LL<<26),real(235653561LL<<25),-real(820617391LL<<27),
      real(25869702111LL<<25),-real(68305888497LL<<26),
      reale(3896,38584213LL<<25),-reale(11280,10173573LL<<28),
      reale(25274,31321979LL<<25),-reale(44240,19038327LL<<26),
      reale(60354,128468529LL<<25),-reale(63152,8090597LL<<27),
      reale(48923,66496087LL<<25),-reale(26288,683965LL<<26),
      reale(8669,60420749LL<<25),-real(0xa3ae57ad353LL<<19),
      reale(26213304,0x19fb43ab7aab9LL),
      // C4[11], coeff of eps^11, polynomial in n of order 15
      -real(3933LL<<23),-real(33649LL<<24),-real(3312023LL<<23),
      real(38979963LL<<25),-real(2334466673LL<<23),real(9974539421LL<<24),
      -real(115419670443LL<<23),real(61272170729LL<<26),
      -reale(2977,381931269LL<<23),reale(7656,260966955LL<<24),
      -reale(15739,178079295LL<<23),reale(25923,81221929LL<<25),
      -reale(33768,486785817LL<<23),reale(33232,239529529LL<<24),
      -reale(20951,91935571LL<<23),reale(5892,8880483819LL<<18),
      reale(26213304,0x19fb43ab7aab9LL),
      // C4[12], coeff of eps^26, polynomial in n of order 0
      -real(1LL<<33),real(0x2f0618f20f09a7LL),
      // C4[12], coeff of eps^25, polynomial in n of order 1
      -real(62273LL<<28),real(123651LL<<24),real(0x19e65bbd524850fbLL),
      // C4[12], coeff of eps^24, polynomial in n of order 2
      -real(93684917LL<<28),-real(76423549LL<<27),-real(693037063LL<<24),
      reale(2191747,0xd5a68f81111b3LL),
      // C4[12], coeff of eps^23, polynomial in n of order 3
      real(311968535LL<<28),real(1760740793LL<<29),-real(1954369859LL<<28),
      real(3073971433LL<<24),reale(9497573,0xf32718849f75dLL),
      // C4[12], coeff of eps^22, polynomial in n of order 4
      -real(7646768769LL<<30),real(19951096269LL<<28),real(491010815LL<<29),
      -real(320366609LL<<28),-real(523396783LL<<29),
      reale(28492721,0xd975498dde617LL),
      // C4[12], coeff of eps^21, polynomial in n of order 5
      -reale(3026,1811699LL<<28),-real(2760169147LL<<30),
      -real(4156137093LL<<28),real(9751170709LL<<29),-real(8065022455LL<<28),
      real(9009785085LL<<24),reale(28492721,0xd975498dde617LL),
      // C4[12], coeff of eps^20, polynomial in n of order 6
      reale(3415,105419659LL<<25),real(300203870565LL<<24),
      -reale(4036,50457863LL<<26),real(324492084003LL<<24),
      real(46663751897LL<<25),real(14263553185LL<<24),
      -real(262021003825LL<<21),reale(28492721,0xd975498dde617LL),
      // C4[12], coeff of eps^19, polynomial in n of order 7
      -reale(9657,23913255LL<<26),reale(11274,14202393LL<<27),
      -real(33749559685LL<<26),-real(32700069373LL<<28),
      -real(114920432067LL<<26),reale(2209,3347763LL<<27),
      -real(43334519585LL<<26),real(23877094395LL<<22),
      reale(28492721,0xd975498dde617LL),
      // C4[12], coeff of eps^18, polynomial in n of order 8
      -reale(10357,4316059LL<<29),-reale(12252,51976653LL<<26),
      real(53111513007LL<<27),reale(10573,38618953LL<<26),
      -reale(6814,7336347LL<<28),-real(6527663009LL<<26),
      real(27052895205LL<<27),real(24601392501LL<<26),-real(17553357101LL<<26),
      reale(28492721,0xd975498dde617LL),
      // C4[12], coeff of eps^17, polynomial in n of order 9
      -reale(37197,16059447LL<<26),reale(60620,6214685LL<<29),
      -reale(35564,62593849LL<<26),real(3388754439LL<<27),
      reale(9080,41918565LL<<26),real(21802684253LL<<28),
      -reale(7184,50608669LL<<26),reale(4144,5922861LL<<27),
      -real(50938551167LL<<26),-real(22779400371LL<<22),
      reale(28492721,0xd975498dde617LL),
      // C4[12], coeff of eps^16, polynomial in n of order 10
      reale(72847,10250567LL<<26),-reale(50730,68954325LL<<25),
      -real(18441684165LL<<28),reale(46646,59050757LL<<25),
      -reale(52485,42154095LL<<26),reale(25457,83058719LL<<25),
      -real(7107757125LL<<27),-reale(5015,25213703LL<<25),
      real(15647388379LL<<26),real(240182800403LL<<25),
      -real(724544787239LL<<22),reale(28492721,0xd975498dde617LL),
      // C4[12], coeff of eps^15, polynomial in n of order 11
      reale(23399,60546659LL<<26),-reale(46215,22779895LL<<27),
      reale(67437,13577137LL<<26),-reale(68612,7311579LL<<29),
      reale(38802,65276767LL<<26),reale(8195,42655LL<<27),
      -reale(41126,40169811LL<<26),reale(42002,3198053LL<<28),
      -reale(22751,40888613LL<<26),reale(6086,3052981LL<<27),
      -real(14786628311LL<<26),-real(192226168043LL<<22),
      reale(28492721,0xd975498dde617LL),
      // C4[12], coeff of eps^14, polynomial in n of order 12
      real(18933494775LL<<28),-reale(4401,33536241LL<<26),
      reale(12915,15192945LL<<27),-reale(29094,11527179LL<<26),
      reale(50530,7111165LL<<29),-reale(66729,34095909LL<<26),
      reale(63904,14901367LL<<27),-reale(38025,42880575LL<<26),
      reale(2775,16493565LL<<28),reale(20705,54393511LL<<26),
      -reale(23355,27541123LL<<27),reale(12999,62947213LL<<26),
      -reale(3169,31971073LL<<26),reale(28492721,0xd975498dde617LL),
      // C4[12], coeff of eps^13, polynomial in n of order 13
      real(168754105LL<<26),-real(365884805LL<<28),real(8561579455LL<<26),
      -real(18298927075LL<<27),real(119493273445LL<<26),
      -reale(4555,4035095LL<<29),reale(9255,49236715LL<<26),
      -reale(14989,8672597LL<<27),reale(19228,2329233LL<<26),
      -reale(19175,5958615LL<<28),reale(14321,64798871LL<<26),
      -reale(7491,16431175LL<<27),reale(2423,48133949LL<<26),
      -real(387864634927LL<<22),reale(9497573,0xf32718849f75dLL),
      // C4[12], coeff of eps^12, polynomial in n of order 14
      real(198835LL<<25),-real(20309575LL<<24),real(82800575LL<<26),
      -real(3096741505LL<<24),real(9853268425LL<<25),-real(92620723195LL<<24),
      real(42100328725LL<<27),-reale(3631,95393589LL<<24),
      reale(8507,100242399LL<<25),-reale(16264,217481391LL<<24),
      reale(25338,57859733LL<<26),-reale(31673,155080937LL<<24),
      reale(30296,62498037LL<<25),-reale(18783,217084003LL<<24),
      reale(5237,1702548307LL<<21),reale(28492721,0xd975498dde617LL),
      // C4[13], coeff of eps^26, polynomial in n of order 0
      real(83LL<<25),real(0xb952c68e4fbe9LL),
      // C4[13], coeff of eps^25, polynomial in n of order 1
      -real(71903LL<<28),-real(1749945LL<<24),reale(5818,0x23b391cd899edLL),
      // C4[13], coeff of eps^24, polynomial in n of order 2
      real(16903565LL<<32),-real(16862357LL<<31),real(47373573LL<<26),
      reale(789029,0x386f296be7703LL),
      // C4[13], coeff of eps^23, polynomial in n of order 3
      real(16624462311LL<<28),real(913717761LL<<29),-real(74700691LL<<28),
      -real(16672249061LL<<24),reale(30772139,0x98ef4f7042175LL),
      // C4[13], coeff of eps^22, polynomial in n of order 4
      -real(876500127LL<<32),-real(1654287687LL<<30),real(2350551113LL<<31),
      -real(1761427069LL<<30),real(3376471371LL<<25),
      reale(30772139,0x98ef4f7042175LL),
      // C4[13], coeff of eps^21, polynomial in n of order 5
      real(32655563463LL<<28),-reale(3801,39657LL<<30),real(14063722833LL<<28),
      real(3085833575LL<<29),real(1328082427LL<<28),-real(31671991379LL<<24),
      reale(30772139,0x98ef4f7042175LL),
      // C4[13], coeff of eps^20, polynomial in n of order 6
      reale(9254,348341LL<<33),real(891770565LL<<32),-real(427379969LL<<34),
      -real(2022490653LL<<32),real(1067054247LL<<33),-real(569056495LL<<32),
      real(430257975LL<<27),reale(30772139,0x98ef4f7042175LL),
      // C4[13], coeff of eps^19, polynomial in n of order 7
      -reale(12155,12992869LL<<26),-real(50480950653LL<<27),
      reale(10690,33612001LL<<26),-reale(5460,12466223LL<<28),
      -real(37884419769LL<<26),real(23471963137LL<<27),real(26726056077LL<<26),
      -real(264030652949LL<<22),reale(30772139,0x98ef4f7042175LL),
      // C4[13], coeff of eps^18, polynomial in n of order 8
      reale(55507,1702051LL<<31),-reale(25284,15301089LL<<28),
      -reale(4552,6848911LL<<29),reale(8014,12209149LL<<28),
      reale(2646,1117571LL<<30),-reale(6924,9493077LL<<28),
      reale(3590,1368763LL<<29),-real(9963972599LL<<28),
      -real(15032559759LL<<23),reale(30772139,0x98ef4f7042175LL),
      // C4[13], coeff of eps^17, polynomial in n of order 9
      -reale(35540,37090525LL<<26),-reale(14632,1678969LL<<29),
      reale(49583,4861453LL<<26),-reale(46373,31739579LL<<27),
      reale(18858,13991831LL<<26),real(34153973959LL<<28),
      -reale(4603,63875327LL<<26),-real(5127820649LL<<27),
      real(116479282059LL<<26),-real(662201165171LL<<22),
      reale(30772139,0x98ef4f7042175LL),
      // C4[13], coeff of eps^16, polynomial in n of order 10
      -reale(50761,3025121LL<<30),reale(66355,4425085LL<<29),
      -reale(59858,324757LL<<32),reale(26575,1103635LL<<29),
      reale(16255,479225LL<<30),-reale(41402,7301831LL<<29),
      reale(37768,714123LL<<31),-reale(19110,4265457LL<<29),
      reale(4727,1747987LL<<30),-real(399638603LL<<29),
      -real(44359884933LL<<24),reale(30772139,0x98ef4f7042175LL),
      // C4[13], coeff of eps^15, polynomial in n of order 11
      -reale(6366,57157311LL<<26),reale(16360,10192555LL<<27),
      -reale(33060,46450725LL<<26),reale(52401,3371487LL<<29),
      -reale(63840,37321515LL<<26),reale(56445,29554125LL<<27),
      -reale(29837,31975057LL<<26),-real(31142569185LL<<28),
      reale(20917,29855465LL<<26),-reale(21569,9966225LL<<27),
      reale(11677,61095555LL<<26),-reale(2823,85634603LL<<22),
      reale(30772139,0x98ef4f7042175LL),
      // C4[13], coeff of eps^14, polynomial in n of order 12
      -real(583637535LL<<30),real(11022475035LL<<28),
      -reale(2387,6604529LL<<29),reale(6865,10202825LL<<28),
      -reale(15869,914837LL<<31),reale(29710,9184775LL<<28),
      -reale(45043,8145271LL<<29),reale(54812,7153333LL<<28),
      -reale(52441,1442741LL<<30),reale(37945,12833459LL<<28),
      -reale(19389,6190909LL<<29),reale(6169,7752673LL<<28),
      -real(487958734263LL<<23),reale(30772139,0x98ef4f7042175LL),
      // C4[13], coeff of eps^13, polynomial in n of order 13
      -real(23263695LL<<26),real(59053995LL<<28),-real(1639451385LL<<26),
      real(4222829325LL<<27),-real(33859413315LL<<26),real(13601644665LL<<29),
      -reale(4256,19212781LL<<26),reale(9227,30696251LL<<27),
      -reale(16594,3848759LL<<26),reale(24654,470841LL<<28),
      -reale(29745,41662305LL<<26),reale(27762,19442409LL<<27),
      -reale(16966,1393323LL<<26),reale(4695,1022390371LL<<22),
      reale(30772139,0x98ef4f7042175LL),
      // C4[14], coeff of eps^26, polynomial in n of order 0
      -real(6781LL<<26),real(0x5fa345ccc643905LL),
      // C4[14], coeff of eps^25, polynomial in n of order 1
      -real(5869LL<<31),real(7353LL<<27),real(0x148e6926290dbdd9LL),
      // C4[14], coeff of eps^24, polynomial in n of order 2
      real(299903009LL<<31),real(37927009LL<<30),-real(2056312073LL<<27),
      reale(33051557,0x58695552a5cd3LL),
      // C4[14], coeff of eps^23, polynomial in n of order 3
      -real(368055047LL<<31),real(374500339LL<<32),-real(256592557LL<<31),
      real(207940889LL<<27),reale(11017185,0xc8231c70e1ef1LL),
      // C4[14], coeff of eps^22, polynomial in n of order 4
      -reale(3490,1015519LL<<31),real(4441335459LL<<29),real(1543166497LL<<30),
      real(845740769LL<<29),-real(7622621279LL<<26),
      reale(33051557,0x58695552a5cd3LL),
      // C4[14], coeff of eps^21, polynomial in n of order 5
      real(1872610391LL<<32),-real(324912469LL<<34),-reale(2076,886527LL<<32),
      real(978081451LL<<33),-real(478972501LL<<32),real(98710857LL<<28),
      reale(33051557,0x58695552a5cd3LL),
      // C4[14], coeff of eps^20, polynomial in n of order 6
      -reale(4075,185993LL<<32),reale(10359,933297LL<<31),
      -reale(4245,239235LL<<33),-real(1849695177LL<<31),real(616423549LL<<32),
      real(879958205LL<<31),-real(3876332285LL<<28),
      reale(33051557,0x58695552a5cd3LL),
      // C4[14], coeff of eps^19, polynomial in n of order 7
      -reale(16507,1038671LL<<32),-reale(7426,284587LL<<33),
      reale(6609,461219LL<<32),reale(3685,34759LL<<34),
      -reale(6576,786795LL<<32),reale(3109,191175LL<<33),
      -real(486788729LL<<32),-real(537600147LL<<28),
      reale(33051557,0x58695552a5cd3LL),
      // C4[14], coeff of eps^18, polynomial in n of order 8
      -reale(24788,2034733LL<<30),reale(49876,16016773LL<<27),
      -reale(40130,10305415LL<<28),reale(13469,4482399LL<<27),
      reale(3498,5005267LL<<29),-reale(4111,23511239LL<<27),
      -real(7714983213LL<<28),real(56106110739LL<<27),
      -real(151831927709LL<<24),reale(33051557,0x58695552a5cd3LL),
      // C4[14], coeff of eps^17, polynomial in n of order 9
      reale(63443,3600701LL<<29),-reale(50757,496203LL<<32),
      reale(16003,4273171LL<<29),reale(22072,52127LL<<30),
      -reale(40595,5066263LL<<29),reale(33806,1004469LL<<31),
      -reale(16095,1666881LL<<29),reale(3680,908597LL<<30),
      real(584087189LL<<29),-real(20381568753LL<<25),
      reale(33051557,0x58695552a5cd3LL),
      // C4[14], coeff of eps^16, polynomial in n of order 10
      reale(19689,3969453LL<<29),-reale(36282,12968943LL<<28),
      reale(53123,80009LL<<31),-reale(60234,3770241LL<<28),
      reale(49410,2521755LL<<29),-reale(22943,4183315LL<<28),
      -reale(5331,2542455LL<<30),reale(20742,9731931LL<<28),
      -reale(19939,3671159LL<<29),reale(10552,6717897LL<<28),
      -reale(2533,118946129LL<<25),reale(33051557,0x58695552a5cd3LL),
      // C4[14], coeff of eps^15, polynomial in n of order 11
      real(8533174455LL<<29),-reale(3246,843591LL<<30),
      reale(8406,531117LL<<29),-reale(17842,647003LL<<32),
      reale(31156,7375267LL<<29),-reale(44630,771985LL<<30),
      reale(51879,2231193LL<<29),-reale(47870,1100123LL<<31),
      reale(33689,2160271LL<<29),-reale(16864,3290075LL<<30),
      reale(5288,925829LL<<29),-real(103506398177LL<<25),
      reale(33051557,0x58695552a5cd3LL),
      // C4[14], coeff of eps^14, polynomial in n of order 12
      real(66723345LL<<29),-real(1495097175LL<<27),real(3274386375LL<<28),
      -real(23122205325LL<<27),real(8392504155LL<<30),
      -reale(4840,16188355LL<<27),reale(9826,9115409LL<<28),
      -reale(16767,17260281LL<<27),reale(23911,7831387LL<<29),
      -reale(27976,32288815LL<<27),reale(25559,3357275LL<<28),
      -reale(15423,21986149LL<<27),reale(4241,135611051LL<<24),
      reale(33051557,0x58695552a5cd3LL),
      // C4[15], coeff of eps^26, polynomial in n of order 0
      real(71LL<<30),real(0x2213ecbbb96785dLL),
      // C4[15], coeff of eps^25, polynomial in n of order 1
      real(6799LL<<34),-real(2467695LL<<27),reale(43244,0xc47e8e0e2a501LL),
      // C4[15], coeff of eps^24, polynomial in n of order 2
      real(1754601LL<<37),-real(1107369LL<<36),real(11866753LL<<28),
      reale(1859525,0x141dc611b72bLL),
      // C4[15], coeff of eps^23, polynomial in n of order 3
      real(72562737LL<<34),real(46462031LL<<35),real(31074907LL<<34),
      -real(3658156407LL<<27),reale(35330975,0x17e35b3509831LL),
      // C4[15], coeff of eps^22, polynomial in n of order 4
      -real(13531387LL<<38),-reale(2167,14381LL<<36),real(55828981LL<<37),
      -real(25230703LL<<36),real(3197649LL<<30),
      reale(35330975,0x17e35b3509831LL),
      // C4[15], coeff of eps^21, polynomial in n of order 5
      reale(9732,8631LL<<37),-reale(3185,3623LL<<39),-real(35580543LL<<37),
      real(7839601LL<<38),real(14184443LL<<37),-real(3642815981LL<<28),
      reale(35330975,0x17e35b3509831LL),
      // C4[15], coeff of eps^20, polynomial in n of order 6
      -reale(8973,2493LL<<39),reale(5093,1677LL<<40),reale(4452,53LL<<40),
      -reale(6181,1625LL<<40),reale(2693,7137LL<<39),-real(1482145LL<<40),
      -real(287239701LL<<29),reale(35330975,0x17e35b3509831LL),
      // C4[15], coeff of eps^19, polynomial in n of order 7
      reale(48363,42681LL<<36),-reale(34138,1171LL<<37),
      reale(9135,63227LL<<36),reale(4396,12847LL<<38),-reale(3596,33667LL<<36),
      -real(22964529LL<<37),real(105092799LL<<36),-real(8732815777LL<<28),
      reale(35330975,0x17e35b3509831LL),
      // C4[15], coeff of eps^18, polynomial in n of order 8
      -reale(41806,3793LL<<39),reale(7070,18565LL<<36),
      reale(26107,27709LL<<37),-reale(39103,10113LL<<36),
      reale(30179,111LL<<38),-reale(13593,42919LL<<36),reale(2866,20031LL<<37),
      real(9747283LL<<36),-real(584087189LL<<30),
      reale(35330975,0x17e35b3509831LL),
      // C4[15], coeff of eps^17, polynomial in n of order 9
      -reale(38751,968439LL<<32),reale(52907,129341LL<<35),
      -reale(56213,262777LL<<32),reale(42911,476327LL<<33),
      -reale(17166,1038043LL<<32),-reale(7920,60547LL<<34),
      reale(20320,95267LL<<32),-reale(18461,171251LL<<33),
      reale(9586,798401LL<<32),-reale(2289,97706315LL<<25),
      reale(35330975,0x17e35b3509831LL),
      // C4[15], coeff of eps^16, polynomial in n of order 10
      -real(182681295LL<<35),reale(3304,139139LL<<34),-reale(6521,16667LL<<37),
      reale(10722,226797LL<<34),-reale(14618,113609LL<<35),
      reale(16325,9799LL<<34),-reale(14590,57403LL<<36),
      reale(10019,97137LL<<34),-reale(4925,40451LL<<35),real(399638603LL<<34),
      -real(14786628311LL<<26),reale(11776991,0xb2a11e67032bbLL),
      // C4[15], coeff of eps^15, polynomial in n of order 11
      -real(76608285LL<<32),real(147323625LL<<33),-real(936978255LL<<32),
      reale(2382,112581LL<<35),-reale(5377,114593LL<<32),
      reale(10315,142015LL<<33),-reale(16818,394707LL<<32),
      reale(23142,22533LL<<34),-reale(26356,277413LL<<32),
      reale(23629,395541LL<<33),-reale(14101,658135LL<<32),
      reale(3855,122649445LL<<25),reale(35330975,0x17e35b3509831LL),
      // C4[16], coeff of eps^26, polynomial in n of order 0
      -real(22951LL<<32),reale(14038,0xf79362a6f2da9LL),
      // C4[16], coeff of eps^25, polynomial in n of order 1
      -real(9017LL<<35),real(4815LL<<31),reale(9206,0xf354c01a236f3LL),
      // C4[16], coeff of eps^24, polynomial in n of order 2
      real(1146319LL<<36),real(916151LL<<35),-real(5763591LL<<32),
      reale(1979494,0x5c2d55f3c2615LL),
      // C4[16], coeff of eps^23, polynomial in n of order 3
      -real(15250071LL<<35),real(5353311LL<<36),-real(2240893LL<<35),
      -real(332469LL<<31),reale(1979494,0x5c2d55f3c2615LL),
      // C4[16], coeff of eps^22, polynomial in n of order 4
      -reale(2280,6539LL<<38),-real(78951693LL<<36),real(12301989LL<<37),
      real(28829297LL<<36),-real(214091115LL<<32),
      reale(37610392,0xd75d61176d38fLL),
      // C4[16], coeff of eps^21, polynomial in n of order 5
      reale(3604,40151LL<<35),reale(4989,25591LL<<37),-reale(5766,5439LL<<35),
      reale(2335,38023LL<<36),-real(36776117LL<<35),-real(73810821LL<<31),
      reale(37610392,0xd75d61176d38fLL),
      // C4[16], coeff of eps^20, polynomial in n of order 6
      -reale(28603,8635LL<<37),reale(5695,63155LL<<36),reale(4892,14039LL<<38),
      -reale(3091,58619LL<<36),-real(29081577LL<<37),real(100489431LL<<36),
      -real(125982325LL<<34),reale(37610392,0xd75d61176d38fLL),
      // C4[16], coeff of eps^19, polynomial in n of order 7
      -real(44186749LL<<35),reale(28752,63675LL<<36),-reale(37202,72039LL<<35),
      reale(26902,18169LL<<37),-reale(11512,105649LL<<35),
      reale(2229,59753LL<<36),real(26382181LL<<35),-real(267675387LL<<31),
      reale(37610392,0xd75d61176d38fLL),
      // C4[16], coeff of eps^18, polynomial in n of order 8
      reale(51956,3503LL<<39),-reale(52000,65291LL<<36),
      reale(36995,30461LL<<37),-reale(12343,54705LL<<36),
      -reale(9828,2065LL<<38),reale(19743,35337LL<<36),
      -reale(17124,20865LL<<37),reale(8752,19043LL<<36),
      -reale(2081,554945LL<<32),reale(37610392,0xd75d61176d38fLL),
      // C4[16], coeff of eps^17, polynomial in n of order 9
      reale(11351,15263LL<<35),-reale(21031,10301LL<<38),
      reale(32809,38737LL<<35),-reale(42826,1799LL<<36),
      reale(46156,123299LL<<35),-reale(40102,7421LL<<37),
      reale(26942,16853LL<<35),-reale(13031,12333LL<<36),
      reale(3987,20263LL<<35),-real(1198915809LL<<31),
      reale(37610392,0xd75d61176d38fLL),
      // C4[16], coeff of eps^16, polynomial in n of order 10
      real(100180065LL<<34),-real(583401555LL<<33),reale(2759,6541LL<<36),
      -reale(5863,45661LL<<33),reale(10706,132871LL<<34),
      -reale(16773,276519LL<<33),reale(22364,92173LL<<35),
      -reale(24871,48433LL<<33),reale(21929,91821LL<<34),
      -reale(12958,132347LL<<33),reale(3525,1706711LL<<30),
      reale(37610392,0xd75d61176d38fLL),
      // C4[17], coeff of eps^26, polynomial in n of order 0
      real(1LL<<32),real(0x62a61c3e4dd975LL),
      // C4[17], coeff of eps^25, polynomial in n of order 1
      real(4057LL<<35),-real(45015LL<<31),reale(8569,0x3d59f665e75a3LL),
      // C4[17], coeff of eps^24, polynomial in n of order 2
      real(43463LL<<40),-real(16895LL<<39),-real(11395LL<<34),
      reale(299923,0x634cafeea1549LL),
      // C4[17], coeff of eps^23, polynomial in n of order 3
      -real(1242717LL<<35),real(138325LL<<36),real(435713LL<<35),
      -real(3030063LL<<31),reale(299923,0x634cafeea1549LL),
      // C4[17], coeff of eps^22, polynomial in n of order 4
      real(2302621LL<<39),-real(9225219LL<<37),real(1747781LL<<38),
      -real(372113LL<<37),-real(1948863LL<<32),
      reale(2099463,0xb718cf86694ffLL),
      // C4[17], coeff of eps^21, polynomial in n of order 5
      reale(2996,69763LL<<35),reale(5105,30307LL<<37),-reale(2616,23051LL<<35),
      -real(67503821LL<<36),real(191814791LL<<35),-real(933454921LL<<31),
      reale(39889810,0x96d766f9d0eedLL),
      // C4[17], coeff of eps^20, polynomial in n of order 6
      reale(30327,1293LL<<39),-reale(35084,3299LL<<38),reale(23968,2311LL<<40),
      -reale(9776,7093LL<<38),real(14159215LL<<39),real(3853577LL<<38),
      -real(61360803LL<<33),reale(39889810,0x96d766f9d0eedLL),
      // C4[17], coeff of eps^19, polynomial in n of order 7
      -reale(47757,47889LL<<35),reale(31664,61447LL<<36),
      -reale(8326,73443LL<<35),-reale(11212,17667LL<<37),
      reale(19076,23915LL<<35),-reale(15916,62675LL<<36),
      reale(8026,42649LL<<35),-real(3989637911LL<<31),
      reale(39889810,0x96d766f9d0eedLL),
      // C4[17], coeff of eps^18, polynomial in n of order 8
      -reale(22252,923LL<<40),reale(33139,2449LL<<37),-reale(41618,6905LL<<38),
      reale(43458,30291LL<<37),-reale(36814,1531LL<<39),
      reale(24253,3845LL<<37),-reale(11561,2707LL<<38),reale(3500,30023LL<<37),
      -real(522604327LL<<32),reale(39889810,0x96d766f9d0eedLL),
      // C4[17], coeff of eps^17, polynomial in n of order 9
      -real(175857885LL<<35),reale(3123,8343LL<<38),-reale(6297,123891LL<<35),
      reale(11012,26677LL<<36),-reale(16654,74217LL<<35),
      reale(21593,16599LL<<37),-reale(23509,7807LL<<35),reale(20422,743LL<<36),
      -reale(11961,60789LL<<35),reale(3239,1180923LL<<31),
      reale(39889810,0x96d766f9d0eedLL),
      // C4[18], coeff of eps^26, polynomial in n of order 0
      -real(56087LL<<33),reale(47221,0xfaefc0318df67LL),
      // C4[18], coeff of eps^25, polynomial in n of order 1
      -real(19981LL<<39),-real(10755LL<<35),reale(443886,0x9d340e9e9cd95LL),
      // C4[18], coeff of eps^24, polynomial in n of order 2
      real(84155LL<<39),real(380011LL<<38),-real(1249051LL<<35),
      reale(2219433,0x12044919103e9LL),
      // C4[18], coeff of eps^23, polynomial in n of order 3
      -real(2130987LL<<39),real(379583LL<<40),-real(70649LL<<39),
      -real(240567LL<<35),reale(2219433,0x12044919103e9LL),
      // C4[18], coeff of eps^22, polynomial in n of order 4
      real(4417441LL<<38),-real(7513869LL<<36),-real(1960735LL<<37),
      real(4812241LL<<36),-real(11409363LL<<33),
      reale(2219433,0x12044919103e9LL),
      // C4[18], coeff of eps^21, polynomial in n of order 5
      -real(28350547LL<<38),real(4603793LL<<40),-real(7176677LL<<38),
      real(573937LL<<39),real(220745LL<<38),-real(1482145LL<<34),
      reale(2219433,0x12044919103e9LL),
      // C4[18], coeff of eps^20, polynomial in n of order 6
      reale(26896,5159LL<<38),-reale(4987,8879LL<<37),-reale(12193,6323LL<<39),
      reale(18360,26775LL<<37),-reale(14825,8691LL<<38),
      reale(7390,26973LL<<37),-real(457982805LL<<34),
      reale(42169228,0x56516cdc34a4bLL),
      // C4[18], coeff of eps^19, polynomial in n of order 7
      reale(11070,12259LL<<38),-reale(13431,6105LL<<39),
      reale(13633,4633LL<<38),-reale(11288,2771LL<<40),reale(7306,11663LL<<38),
      -reale(3437,4851LL<<39),real(16896453LL<<38),-real(38239341LL<<34),
      reale(14056409,0x721b244966e19LL),
      // C4[18], coeff of eps^18, polynomial in n of order 8
      reale(3471,5433LL<<39),-reale(6682,62369LL<<36),reale(11244,10859LL<<37),
      -reale(16478,49907LL<<36),reale(20837,10809LL<<38),
      -reale(22258,26821LL<<36),reale(19078,20857LL<<37),
      -reale(11086,15383LL<<36),reale(2990,191861LL<<33),
      reale(42169228,0x56516cdc34a4bLL),
      // C4[19], coeff of eps^26, polynomial in n of order 0
      -real(113LL<<37),reale(16591,0x81ae2ec54d8dfLL),
      // C4[19], coeff of eps^25, polynomial in n of order 1
      real(94099LL<<40),-real(1178305LL<<35),reale(2339402,0x6cefc2abb72d3LL),
      // C4[19], coeff of eps^24, polynomial in n of order 2
      real(41263LL<<43),-real(6583LL<<42),-real(117501LL<<36),
      reale(2339402,0x6cefc2abb72d3LL),
      // C4[19], coeff of eps^23, polynomial in n of order 3
      -real(384159LL<<40),-real(130977LL<<41),real(286571LL<<40),
      -real(2657049LL<<35),reale(2339402,0x6cefc2abb72d3LL),
      // C4[19], coeff of eps^22, polynomial in n of order 4
      real(64121LL<<46),-real(23919LL<<46),real(6837LL<<45),real(901LL<<46),
      -real(170289LL<<37),reale(2339402,0x6cefc2abb72d3LL),
      // C4[19], coeff of eps^21, polynomial in n of order 5
      -real(955747LL<<39),-real(1386619LL<<41),real(7599723LL<<39),
      -real(2983211LL<<40),real(2945369LL<<39),-real(22232175LL<<34),
      reale(2339402,0x6cefc2abb72d3LL),
      // C4[19], coeff of eps^20, polynomial in n of order 6
      -real(2096679LL<<42),real(4148625LL<<41),-real(841269LL<<43),
      real(2143479LL<<41),-real(498253LL<<42),real(296429LL<<41),
      -real(2667861LL<<35),reale(2339402,0x6cefc2abb72d3LL),
      // C4[19], coeff of eps^19, polynomial in n of order 7
      -real(3026933LL<<39),real(2460315LL<<40),-real(7010575LL<<39),
      real(2166905LL<<41),-real(9101001LL<<39),real(3853577LL<<40),
      -real(4446435LL<<39),real(38239341LL<<34),
      reale(2339402,0x6cefc2abb72d3LL),
      // C4[20], coeff of eps^26, polynomial in n of order 0
      -real(34781LL<<40),reale(2459371,0xc7db3c3e5e1bdLL),
      // C4[20], coeff of eps^25, polynomial in n of order 1
      -real(4771LL<<42),-real(28479LL<<38),reale(2459371,0xc7db3c3e5e1bdLL),
      // C4[20], coeff of eps^24, polynomial in n of order 2
      -real(68467LL<<42),real(136501LL<<41),-real(310209LL<<38),
      reale(2459371,0xc7db3c3e5e1bdLL),
      // C4[20], coeff of eps^23, polynomial in n of order 3
      -real(327189LL<<42),real(20533LL<<43),real(14681LL<<42),
      -real(78387LL<<38),reale(2459371,0xc7db3c3e5e1bdLL),
      // C4[20], coeff of eps^22, polynomial in n of order 4
      -real(179129LL<<44),real(910389LL<<42),-real(348793LL<<43),
      real(341479LL<<42),-real(321657LL<<40),reale(2459371,0xc7db3c3e5e1bdLL),
      // C4[20], coeff of eps^21, polynomial in n of order 5
      real(1952379LL<<42),-real(388557LL<<44),real(975677LL<<42),
      -real(224349LL<<43),real(132447LL<<42),-real(296429LL<<38),
      reale(2459371,0xc7db3c3e5e1bdLL),
      // C4[20], coeff of eps^20, polynomial in n of order 6
      real(1242423LL<<41),-real(3451175LL<<40),real(1045213LL<<42),
      -real(4322097LL<<40),real(1810109LL<<41),-real(2075003LL<<40),
      real(4446435LL<<37),reale(2459371,0xc7db3c3e5e1bdLL),
      // C4[21], coeff of eps^26, polynomial in n of order 0
      -real(199LL<<39),reale(37381,0xc16e795c129fbLL),
      // C4[21], coeff of eps^25, polynomial in n of order 1
      real(65027LL<<42),-real(290455LL<<38),reale(2579341,0x22c6b5d1050a7LL),
      // C4[21], coeff of eps^24, polynomial in n of order 2
      real(1883LL<<46),real(1837LL<<45),-real(18073LL<<40),
      reale(2579341,0x22c6b5d1050a7LL),
      // C4[21], coeff of eps^23, polynomial in n of order 3
      real(871509LL<<42),-real(326909LL<<43),real(317735LL<<42),
      -real(1195627LL<<38),reale(2579341,0x22c6b5d1050a7LL),
      // C4[21], coeff of eps^22, polynomial in n of order 4
      -real(29971LL<<46),real(74261LL<<44),-real(16907LL<<45),real(9911LL<<44),
      -real(44149LL<<39),reale(859780,0x60ece745ac58dLL),
      // C4[21], coeff of eps^21, polynomial in n of order 5
      -real(848003LL<<42),real(252109LL<<44),-real(1027829LL<<42),
      real(426173LL<<43),-real(485639LL<<42),real(2075003LL<<38),
      reale(2579341,0x22c6b5d1050a7LL),
      // C4[22], coeff of eps^26, polynomial in n of order 0
      -real(2963LL<<40),reale(117361,0x5360ca6881e97LL),
      // C4[22], coeff of eps^25, polynomial in n of order 1
      real(79LL<<45),-real(363LL<<41),reale(117361,0x5360ca6881e97LL),
      // C4[22], coeff of eps^24, polynomial in n of order 2
      -real(76751LL<<45),real(74129LL<<44),-real(139337LL<<41),
      reale(2699310,0x7db22f63abf91LL),
      // C4[22], coeff of eps^23, polynomial in n of order 3
      real(102051LL<<45),-real(23023LL<<46),real(13409LL<<45),
      -real(29733LL<<41),reale(2699310,0x7db22f63abf91LL),
      // C4[22], coeff of eps^22, polynomial in n of order 4
      real(121647LL<<45),-real(489555LL<<43),real(201135LL<<44),
      -real(227953LL<<43),real(485639LL<<40),reale(2699310,0x7db22f63abf91LL),
      // C4[23], coeff of eps^26, polynomial in n of order 0
      -real(1LL<<45),reale(5837,0x4b04b152e489LL),
      // C4[23], coeff of eps^25, polynomial in n of order 1
      real(377LL<<47),-real(5665LL<<41),reale(122577,0x627628bccbf3dLL),
      // C4[23], coeff of eps^24, polynomial in n of order 2
      -real(57LL<<50),real(33LL<<49),-real(583LL<<42),
      reale(122577,0x627628bccbf3dLL),
      // C4[23], coeff of eps^23, polynomial in n of order 3
      -real(1269LL<<47),real(517LL<<48),-real(583LL<<47),real(9911LL<<41),
      reale(122577,0x627628bccbf3dLL),
      // C4[24], coeff of eps^26, polynomial in n of order 0
      -real(83LL<<47),reale(127793,0x718b871115fe3LL),
      // C4[24], coeff of eps^25, polynomial in n of order 1
      real(5LL<<50),-real(11LL<<46),reale(42597,0xd083d7b05caa1LL),
      // C4[24], coeff of eps^24, polynomial in n of order 2
      real(245LL<<49),-real(275LL<<48),real(583LL<<45),
      reale(127793,0x718b871115fe3LL),
      // C4[25], coeff of eps^26, polynomial in n of order 0
      -real(1LL<<47),reale(8867,0x4cd786c27dde7LL),
      // C4[25], coeff of eps^25, polynomial in n of order 1
      -real(13LL<<50),real(55LL<<46),reale(26601,0xe6869447799b5LL),
      // C4[26], coeff of eps^26, polynomial in n of order 0
      real(1LL<<48),reale(2126,0x8c0e9e949456fLL),
    };  // count = 4032
#elif GEOGRAPHICLIB_GEODESICEXACT_ORDER == 30
    static const real coeff[] = {
      // C4[0], coeff of eps^29, polynomial in n of order 0
      3361,real(109067695),
      // C4[0], coeff of eps^28, polynomial in n of order 1
      real(121722048),real(30168404),real(0x269c465a0c9LL),
      // C4[0], coeff of eps^27, polynomial in n of order 2
      real(21708121824LL),-real(10786479696LL),real(8048130587LL),
      real(0xbfa33c13e963LL),
      // C4[0], coeff of eps^26, polynomial in n of order 3
      real(0x738319564e0LL),-real(0x4c2475635c0LL),real(0x25d0be52da0LL),
      real(643173496654LL),real(0xa0f21774b90225LL),
      // C4[0], coeff of eps^25, polynomial in n of order 4
      real(0x7a99ea0a52f40LL),-real(0x5a5f53e2c3b50LL),real(0x3b83d2c0c8da0LL),
      -real(0x1d8a81cb5cc70LL),real(0x1605bd50459c1LL),
      real(0x6fb2ae4757107d03LL),
      // C4[0], coeff of eps^24, polynomial in n of order 5
      real(0x2507d929b7f89580LL),-real(0x1ce7bf02c3715a00LL),
      real(0x15463c23456c8680LL),-real(0xdfecff0050dfd00LL),
      real(0x6f141ba97196780LL),real(0x1b71ab9c78b8b48LL),
      reale(1520879,0x957266bcf90f9LL),
      // C4[0], coeff of eps^23, polynomial in n of order 6
      reale(5214,0xb54b8c26f5620LL),-reale(4202,0x4ae5f5bcbf950LL),
      reale(3272,0xab988a50dfac0LL),-reale(2404,0x84ae60c9e7b30LL),
      real(0x62be65b26227b760LL),-real(0x30f2645200be8b10LL),
      real(0x2472ebc3f09ad327LL),reale(9429453,0x6b5ee3606e93bLL),
      // C4[0], coeff of eps^22, polynomial in n of order 7
      reale(213221,0x21fe88963f0e0LL),-reale(174746,0x12fe03af82e40LL),
      reale(140344,0xd3dfad978d4a0LL),-reale(109009,0x13ee03d15f180LL),
      reale(79932,0x9fff01479b460LL),-reale(52447,0x53ea945b584c0LL),
      reale(25976,0xa5a6ee990f820LL),reale(6403,0x87dc4a069efc6LL),
      reale(273454149,0x29bfc1ec86bafLL),
      // C4[0], coeff of eps^21, polynomial in n of order 8
      reale(1513769,0x9572babb99080LL),-reale(1247902,0x66609b16e1250LL),
      reale(1017692,0x228016ac84e60LL),-reale(814136,0x86ec313455df0LL),
      reale(630421,0xa88f591713840LL),-reale(461205,0x487f023b60f90LL),
      reale(302134,0x36942691aea20LL),-reale(149503,0x5a1d9af94cb30LL),
      reale(111169,0xb14ab93d4ba6dLL),reale(1367270745,0xd0bec99ea1a6bLL),
      // C4[0], coeff of eps^20, polynomial in n of order 9
      reale(2196138,0xe1b60fe1808c0LL),-reale(1802572,0x3b4b1c2a34200LL),
      reale(1475191,0x47b8ccbe8340LL),-reale(1196055,0x2e2a401c46980LL),
      reale(952413,0x117e9e1fb75c0LL),-reale(734856,0x2e19f1e7be100LL),
      reale(536171,0x8daa599335040LL),-reale(350594,0xa58d466a3880LL),
      reale(173293,0x7b19cdc9682c0LL),reale(42591,0xb005bdeb82d74LL),
      reale(1367270745,0xd0bec99ea1a6bLL),
      // C4[0], coeff of eps^19, polynomial in n of order 10
      reale(9954363,0x5ecc5371ca720LL),-reale(8035921,0x7cc90565e0670LL),
      reale(6522783,0x32e1ec30d1a80LL),-reale(5291286,0x4172ef2beb090LL),
      reale(4260231,0x65c388ed45de0LL),-reale(3373847,0x4da61e8c704b0LL),
      reale(2592185,0xcd194d02dbd40LL),-reale(1885401,0xa08c9a20ef6d0LL),
      reale(1230164,0x4c527bc6a84a0LL),-reale(607279,0x24d6e51bd7af0LL),
      reale(450701,0xae98337b7d081LL),reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^18, polynomial in n of order 11
      reale(16160603,0x85a3ec5761ce0LL),-reale(12587219,0x97b7f7c505ac0LL),
      reale(9979192,0xa0e43863a93a0LL),-reale(7988280,0xcfaf566027f00LL),
      reale(6410314,0xbffc30c12660LL),-reale(5117692,0xfd9318db4c340LL),
      reale(4026292,0x94c482b815d20LL),-reale(3077917,0x9c480ad851f80LL),
      reale(2230377,0x99db799d8bfe0LL),-reale(1451530,0xb0005d9658bc0LL),
      reale(715485,0xdbe6a2ef6d6a0LL),reale(175141,0x3547b8669b9beLL),
      reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^17, polynomial in n of order 12
      reale(30091817,0x8745c27487540LL),-reale(21716256,0x7a4bb1495e170LL),
      reale(16366670,0xd4e8bc19a0660LL),-reale(12670374,0x9eda0f5df2ed0LL),
      reale(9963727,0x5ae4f6d3c8380LL),-reale(7887824,0x191034733ae30LL),
      reale(6231873,0x96448488ef0a0LL),-reale(4863678,0x67c3c74b1b90LL),
      reale(3695513,0x2e7ae0f4851c0LL),-reale(2665992,0xe6864878c32f0LL),
      reale(1729741,0xf881cba41aae0LL),-reale(851104,0x888fd5b7ab050LL),
      reale(629987,0x9ea5a19626943LL),reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^16, polynomial in n of order 13
      reale(79181861,0x46beef62ca900LL),-reale(45969492,0x85a19d8425400LL),
      reale(30736937,0x10d9a95bb4f00LL),-reale(22084618,0xaf3a6659fa600LL),
      reale(16548053,0x58583f22e9500LL),-reale(12711232,0x3d7f1b1be3800LL),
      reale(9889259,0xbbf5d84b2bb00LL),-reale(7711253,0x36b17889dca00LL),
      reale(5958759,0x73d1ebe040100LL),-reale(4493987,0xfa374abbe1c00LL),
      reale(3224517,0x29027e04ea700LL),-reale(2084431,0x8d77e42beee00LL),
      reale(1023433,0xbf113370eed00LL),reale(249103,0x93cdbdabe0fb0LL),
      reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^15, polynomial in n of order 14
      reale(100415733,0x1c7e0d98777e0LL),-reale(220472579,0x196c2a7ff77f0LL),
      reale(81497972,0xcf48e14d7b2c0LL),-reale(47157604,0xb4c79beff0c90LL),
      reale(31400333,0x3ade51fc905a0LL),-reale(22437640,0x62c8445afeb30LL),
      reale(16688020,0xb49b2cc64ec80LL),-reale(12687475,0x35a524f08d7d0LL),
      reale(9727302,0xc96eb1166e360LL),-reale(7422875,0x3574dc9ff9670LL),
      reale(5546536,0x3897621326640LL),-reale(3953280,0x7a61d237aeb10LL),
      reale(2544043,0x942757fc8f120LL),-reale(1245848,0x5f59e2e2499b0LL),
      reale(918672,0xb7e149f3f515dLL),reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^14, polynomial in n of order 15
      -reale(410150575,0x33edeefdadd60LL),reale(389451478,0x4a8eb37cf8e40LL),
      reale(102537774,0xdf54e754057e0LL),-reale(228145792,0x9928ef6984980LL),
      reale(84014235,0x8c476a1354120LL),-reale(48417903,0x9486b64af140LL),
      reale(32072368,0xac5157de0d660LL),-reale(22757026,0x6fd3c1d71f100LL),
      reale(16760216,0x75de552320fa0LL),-reale(12564203,0xce657c7ead0c0LL),
      reale(9433140,0xee7b325fde4e0LL),-reale(6966096,0xc0a9d97231880LL),
      reale(4923714,0x7fe1a8c934e20LL),-reale(3150864,0xcacdc5bf45040LL),
      reale(1538058,0xc6e75548f4360LL),reale(371250,0x9b28ca926da22LL),
      reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^13, polynomial in n of order 16
      reale(10071346,0xbead2787bab00LL),reale(77935892,0xc8037e807a610LL),
      -reale(424974584,0x95c58aa2abc60LL),reale(405632040,0xf37804095de30LL),
      reale(104709205,0x2c34dddf07040LL),-reale(236671973,0xc06ad427a5bb0LL),
      reale(86756233,0x36f6256b264e0LL),-reale(49748360,0xa42ca4c379390LL),
      reale(32735340,0x1aa6eba145580LL),-reale(23012513,0x41e6e60af5570LL),
      reale(16722020,0xa0e65eb557620LL),-reale(12285046,0x712c138942d50LL),
      reale(8933912,0x44131ea6cfac0LL),-reale(6247309,0xac4879043a730LL),
      reale(3969671,0x8774cc7c1760LL),-reale(1929932,0x2a739696c4f10LL),
      reale(1414943,0x9f9bcb791811fLL),reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^12, polynomial in n of order 17
      reale(1301009,0x7885767b34dc0LL),reale(3139452,0x6299dbe8eac00LL),
      reale(10399899,0xe9c2f692aa40LL),reale(80694987,0xafcfc919b1e80LL),
      -reale(441529449,0x34f14f083e140LL),reale(423985433,0x2e9be95704100LL),
      reale(106892519,0x9a909730adb40LL),-reale(246219322,0x3cc21ecefbc80LL),
      reale(89751674,0x8e9ea1f760fc0LL),-reale(51139306,0x4d1fa35b2aa00LL),
      reale(33357165,0x391836578ec40LL),-reale(23152852,0x670df382e5780LL),
      reale(16502135,0xfb453b1baa0c0LL),-reale(11755175,0x732a395d89500LL),
      reale(8105218,0xa64658fb65d40LL),-reale(5103238,0xc9c658d3f3280LL),
      reale(2468214,0x7d6aacb2351c0LL),reale(588064,0xecbdce72e5104LL),
      reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^11, polynomial in n of order 18
      reale(365173,0x141eb92882aa0LL),reale(660579,0x721db1cc80890LL),
      reale(1339643,0x6f3cff39e7d00LL),reale(3240370,0xc29100e665970LL),
      reale(10762711,0xac38fa6376f60LL),reale(83769430,0x6edf90fa38050LL),
      -reale(460180081,0xa7a2c15d05240LL),reale(445039582,0xb96af8d66e930LL),
      reale(109020126,0x840edc5d1e420LL),-reale(257005247,0x2ec795996fff0LL),
      reale(93028106,0x54adfb574be80LL),-reale(52565819,0x1d828e2b6cf10LL),
      reale(33879206,0x109475f98e8e0LL),-reale(23088279,0x158dbde3c1830LL),
      reale(15975944,0x7a6ca24c70f40LL),-reale(10806612,0x3c0d699b76f50LL),
      reale(6721635,0xd5a36326ddda0LL),-reale(3228909,0xe44dc20d06870LL),
      reale(2345355,0x81bdf10588059LL),reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^10, polynomial in n of order 19
      reale(142358,0x43f28ef2bce60LL),reale(224104,0xc49bf70fb8540LL),
      reale(374789,0x29edb81ed2220LL),reale(679606,0x56dce126b3a00LL),
      reale(1381751,0x3315a15e701e0LL),reale(3351469,0xe4cb186e3aec0LL),
      reale(11166107,0x295c18ed1d5a0LL),reale(87224183,0xbf27e3cc5cb80LL),
      -reale(481408924,0xf800e4fbbfaa0LL),reale(469519077,0x9e18ca33e7840LL),
      reale(110970854,0x606788cedf920LL),-reale(269315695,0x90dadb20d6300LL),
      reale(96606791,0x8c213171618e0LL),-reale(53972000,0xd509f5454de40LL),
      reale(34191407,0x9021dc5d4cca0LL),-reale(22654105,0x9f8b9187f1180LL),
      reale(14912791,0x946e9b2907c60LL),-reale(9121084,0x6067cd3f714c0LL),
      reale(4341360,0x73b562399020LL),reale(1011849,0x75de66a5bdb46LL),
      reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^9, polynomial in n of order 20
      reale(66631,0x784cbdfb1b2c0LL),reale(96606,0x3419bb8e05f90LL),
      reale(145459,0xb79bffbfb42e0LL),reale(229589,0x824d22506cd30LL),
      reale(385010,0x35e34fd0f4f00LL),reale(700134,0x4df5413db48d0LL),
      reale(1427794,0x581b23c083b20LL),reale(3474469,0x224df4c0f7670LL),
      reale(11618119,0x6c8cba4306b40LL),reale(91144571,0x713d14f45fa10LL),
      -reale(505869523,0xd3d937aa3bca0LL),reale(498449385,0x686859af477b0LL),
      reale(112524504,0x2ca5b0e042780LL),-reale(283533725,0xba4eec11a6cb0LL),
      reale(100487121,0xc424152de7ba0LL),-reale(55236514,0x8c4dd4ee50f10LL),
      reale(34077723,0x322bbe9b9a3c0LL),-reale(21528502,0x2ca44d130cb70LL),
      reale(12851809,0x7f1d30d5603e0LL),-reale(6038295,0xecbfc0da7fdd0LL),
      reale(4313665,0xa0fbedf62e95bLL),reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^8, polynomial in n of order 21
      reale(34939,0x4781a8598a880LL),reale(47986,0x870a153a0ba00LL),
      reale(67643,0xf93c5a3d5fb80LL),reale(98366,0xdef5527b5d100LL),
      reale(148567,0x565e4f7b51e80LL),reale(235242,0x766e64b79c800LL),
      reale(395796,0x5614c84bc3180LL),reale(722239,0xc9f1a6fcbf00LL),
      reale(1478257,0xd3352c2795480LL),reale(3611438,0xfdbc40cced600LL),
      reale(12129091,0x5ec9e3d72a780LL),reale(95645231,0xe79e249b02d00LL),
      -reale(534473300,0x6333290e9b580LL),reale(533336700,0xd7635e240e400LL),
      reale(113268651,0x31e09daaa5d80LL),-reale(300181610,0x6cd38634ee500LL),
      reale(104606327,0x6a6e0bd3d0080LL),-reale(56090968,0xcfc000b8f0e00LL),
      reale(33084425,0x428f85e945380LL),-reale(19025074,0x3fea5ea1f7700LL),
      reale(8768855,0x59c11511e7680LL),reale(1959911,0x57aea52b92dd8LL),
      reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^7, polynomial in n of order 22
      reale(19712,0xac93bc6991f60LL),reale(26064,0x47e63bb6f7b10LL),
      reale(35129,0x85349dd791940LL),reale(48412,0xcf2b50a5e4170LL),
      reale(68486,0xf23457a2e7b20LL),reale(99959,0x1aee9379bdd0LL),
      reale(151547,0xc976e86422100LL),reale(240911,0x67a8290f88c30LL),
      reale(407002,0x79f859786e6e0LL),reale(745880,0xf6e3b80f24890LL),
      reale(1533569,0xcfffb4a9fa8c0LL),reale(3764807,0xab1a08cbd8ef0LL),
      reale(12712489,0x4098eb8542a0LL),reale(100884327,0x9a754746dfb50LL),
      -reale(568536969,0xbcc82f5b36f80LL),reale(576497219,0x10ca042b229b0LL),
      reale(112392819,0xaecaa4a6c6e60LL),-reale(319979712,0xfe05e4aae49f0LL),
      reale(108728942,0x9b1cd9ac3b840LL),-reale(55904982,0xfebe8a174c390LL),
      reale(30158727,0xd0df7149f4a20LL),-reale(13482566,0x2ca2af46da730LL),
      reale(9304222,0x6328f1d67a7f5LL),reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^6, polynomial in n of order 23
      reale(11639,0x4298ebe4bc020LL),reale(14966,0xe9089607c0a40LL),
      reale(19534,0x1996a62965260LL),reale(25928,0xdcaffa7bfcb80LL),
      reale(35089,0x59fa64f7d88a0LL),reale(48563,0x32ed377221cc0LL),
      reale(69004,0xe5c9403173ae0LL),reale(101181,0xf483b00105600LL),
      reale(154143,0xf39432e434120LL),reale(246274,0xfc90899a3cf40LL),
      reale(418255,0xdad9486cf7360LL),reale(770731,0xbf0321b55e080LL),
      reale(1593877,0xd61fe95ba9a0LL),reale(3937200,0x3820413b3e1c0LL),
      reale(13385919,0xf48ca237dbbe0LL),reale(107086956,0x9d1b10f932b00LL),
      -reale(610048075,0x6c1b2715a7de0LL),reale(631706048,0xcac1d46451440LL),
      reale(108187733,0xaf9fd1440d460LL),-reale(343908890,0x37b3c0b50a80LL),
      reale(112109635,0x3a73d439f8aa0LL),-reale(53028119,0x15d1799f5d940LL),
      reale(22454404,0x49a70d2177ce0LL),reale(4553016,0x22f700960daaaLL),
      reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^5, polynomial in n of order 24
      reale(7030,0x634f92bbfec80LL),reale(8852,0x183ea9c784b10LL),
      reale(11280,0x864427e0ea420LL),reale(14569,0x4ed71f4155e30LL),
      reale(19103,0x13b2c1ad2ffc0LL),reale(25480,0x35983eb20bf50LL),
      reale(34659,0x18ad59c5f9360LL),reale(48227,0x95f2c0574270LL),
      reale(68917,0x8c5b3ac32f300LL),reale(101660,0x272f49f96bb90LL),
      reale(155850,0xbc628b339b2a0LL),reale(250657,0x122490d07feb0LL),
      reale(428675,0x21f5a97506640LL),reale(795748,0x8d9dd2ee8dfd0LL),
      reale(1658420,0x22b44d2c5a1e0LL),reale(4130702,0x814b60cb632f0LL),
      reale(14171990,0xb8691b29bf980LL),reale(114585240,0x7599d8275cc10LL),
      -reale(662180135,0x55c1167b3fee0LL),reale(705602404,0xf6219ee07f30LL),
      reale(96655880,0xe42cfbbc64cc0LL),-reale(373149978,0xd8d5a94d3dfb0LL),
      reale(112272021,0x704341a757060LL),-reale(42251989,0xbf5a94cca7c90LL),
      reale(26498553,0xea37274059c77LL),reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^4, polynomial in n of order 25
      reale(4244,0x3972351df5940LL),reale(5257,0xaa8f87b5d5600LL),
      reale(6578,0xed6cb3b3fa2c0LL),reale(8324,0xb4008d853180LL),
      reale(10662,0x703b07259b440LL),reale(13846,0x8f2f6ca125d00LL),
      reale(18261,0x3a455b4269dc0LL),reale(24508,0x5045fb81ae880LL),
      reale(33557,0x1b3e945f36f40LL),reale(47022,0x9499ec44e400LL),
      reale(67699,0x7a940285938c0LL),reale(100662,0x403646e1e5f80LL),
      reale(155637,0xf20897fb50a40LL),reale(252593,0x7106d86756b00LL),
      reale(436178,0xe720d891ff3c0LL),reale(818051,0x1d79595b01680LL),
      reale(1723706,0xc365c92e70540LL),reale(4344105,0xb055b91247200LL),
      reale(15096896,0xe96c54f834ec0LL),reale(123888911,0x435c586708d80LL),
      -reale(730395130,0x8d07d85ee1fc0LL),reale(811137162,0xd7ccf03d27900LL),
      reale(66848989,0xdd39a234bc9c0LL),-reale(407950245,0xd67367b7fbb80LL),
      reale(99073631,0x21cb91dfe1b40LL),reale(14205410,0x589c3f44ce7acLL),
      reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^3, polynomial in n of order 26
      reale(2481,0x8d2c27b46b620LL),reale(3034,0xe44720f3fdf90LL),
      reale(3743,0xf82fc54a92780LL),reale(4662,0xb922ac44f6b70LL),
      reale(5867,0xae02c805f08e0LL),reale(7469,0x40a687e9b4d50LL),
      reale(9629,0xbb2099bca6640LL),reale(12592,0xa0727e14e5130LL),
      reale(16731,0xdc4cfea134ba0LL),reale(22636,0xbf84f9dc44310LL),
      reale(31263,0xfe99294d5c500LL),reale(44220,0x78f2e666feef0LL),
      reale(64313,0xe77c1f84fde60LL),reale(96684,0x43c9282e120d0LL),
      reale(151281,0x84eb0984fa3c0LL),reale(248729,0xa2c4a502aa4b0LL),
      reale(435615,0xd80deb212120LL),reale(829647,0x194fc60e84690LL),
      reale(1777619,0x17dfea7bc6280LL),reale(4562307,0x417bb8824d270LL),
      reale(16175470,0xd3a7db47373e0LL),reale(135804489,0xbb999e2601450LL),
      -reale(825156505,0xa8162cc9f9ec0LL),reale(977623624,0xd8c5ee7f4d830LL),
      -reale(20397512,0x4ab8f862cc960LL),-reale(435632583,0xf2b7943e115f0LL),
      reale(143237887,0xa8277df5ccab1LL),reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^2, polynomial in n of order 27
      real(0x52cac993243497e0LL),real(0x6437dfaee57b9d40LL),
      real(0x7a3f9cad4d2f48a0LL),reale(2405,0xee01eec3f2b00LL),
      reale(2986,0x65a22988df560LL),reale(3743,0xe8ba104bd58c0LL),
      reale(4745,0x82561551e620LL),reale(6086,0xa7581d3ddee80LL),
      reale(7912,0x8561dfdd262e0LL),reale(10440,0x7aa2aab74b440LL),
      reale(14008,0x9b1a2c148b3a0LL),reale(19155,0xcd3b8407d7200LL),
      reale(26767,0x9792b4f9c2060LL),reale(38350,0xb50c17257efc0LL),
      reale(56574,0xaf828f4edf120LL),reale(86399,0xb1bc40483f580LL),
      reale(137581,0x7d29442656de0LL),reale(230687,0xc9059cc5d4b40LL),
      reale(413025,0xcba5d91bbdea0LL),reale(806439,0xbad85d457b900LL),
      reale(1777226,0xdb254a1088b60LL),reale(4709200,0x187f6563b06c0LL),
      reale(17312174,0x4c53d944cbc20LL),reale(151524377,0x682a2ddefc80LL),
      -reale(970338799,0x73aba5c04720LL),reale(1287957204,0xb756685e76240LL),
      -reale(416692036,0xd1e73fe253660LL),-reale(78129756,0xe75b5bfa6fa32LL),
      reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[0], coeff of eps^1, polynomial in n of order 28
      real(0xb4c355cd41c92c0LL),real(0xd8fea3a41cc7830LL),
      real(0x1064f0c6b9a6ad20LL),real(0x13f7a88902ef1b10LL),
      real(0x1884a414973fcb80LL),real(0x1e5fa2ae5243d7f0LL),
      real(0x25fe0bb384ddd9e0LL),real(0x3006f6e3e0e25ad0LL),
      real(0x3d6c2c13c34ec440LL),real(0x4f91f34825bd4fb0LL),
      real(0x688ffb74f98676a0LL),reale(2233,0xdec33bb086290LL),
      reale(3036,0xe53843c2cdd00LL),reale(4213,0xb13e1137e3f70LL),
      reale(5984,0xaa1cca8abe360LL),reale(8732,0xb9880d6c69250LL),
      reale(13152,0x1eadcfcfd75c0LL),reale(20566,0x4e1752c3c0730LL),
      reale(33653,0xf4262a5798020LL),reale(58247,0x3a420e3524a10LL),
      reale(108257,0x7934f39e3ee80LL),reale(221025,0xaccc1c0dc06f0LL),
      reale(514222,0xffbb852faace0LL),reale(1456965,0x29e8a4070e9d0LL),
      reale(5827860,0xa7a2901c3a740LL),reale(56821641,0x6270fd1339eb0LL),
      -reale(416692036,0xd1e73fe253660LL),reale(625038055,0x3adadfd37d190LL),
      -reale(273454149,0x29bfc1ec86bafLL),reale(1367270745,0xd0bec99ea1a6bLL),
      // C4[0], coeff of eps^0, polynomial in n of order 29
      reale(42171,0xbca3d5a569b4LL),reale(46862,0xd0a41cdef9cf0LL),
      reale(52277,0xa2d5316ac1b2cLL),reale(58560,0x6f94d669a7a28LL),
      reale(65892,0x788629d238da4LL),reale(74502,0x6b99bdf690d60LL),
      reale(84681,0x87b277eadbb1cLL),reale(96804,0x8c76c6701c898LL),
      reale(111359,0x1427f62cd3d94LL),reale(128987,0x59921e2221dd0LL),
      reale(150546,0xaa0136eb20f0cLL),reale(177198,0x7742592373f08LL),
      reale(210542,0x4360b9bd64984LL),reale(252821,0x8a8c09196de40LL),
      reale(307248,0x66986780ae6fcLL),reale(378530,0x79d0ac77ed78LL),
      reale(473750,0x5114d83948174LL),reale(603901,0x80acdb5cb5eb0LL),
      reale(786661,0x2afc1dbf812ecLL),reale(1051686,0xda8ab314e3e8LL),
      reale(1451326,0xc0ede2017b564LL),reale(2083956,0x5d3b51a63af20LL),
      reale(3149615,0xde5c8fc3f62dcLL),reale(5099378,0x12ae3e18b3258LL),
      reale(9106032,0x45ee012c1b554LL),reale(18940547,0x20d0545bbdf90LL),
      reale(52086504,0x9a3ce7fc4a6ccLL),reale(312519027,0x9d6d6fe9be8c8LL),
      -reale(1093816596,0xa6ff07b21aebcLL),
      reale(2734541491LL,0xa17d933d434d6LL),
      reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[1], coeff of eps^29, polynomial in n of order 0
      917561,real(273868982145LL),
      // C4[1], coeff of eps^28, polynomial in n of order 1
      -real(125915776),real(90505212),real(0x73d4d30e25bLL),
      // C4[1], coeff of eps^27, polynomial in n of order 2
      -real(0x2f7e4f2fca0LL),real(0x161b06db8f0LL),real(379339642199LL),
      real(0x145a25f15d59339LL),
      // C4[1], coeff of eps^26, polynomial in n of order 3
      -real(0x780f9f651c0LL),real(0x49cd6538080LL),-real(0x275396e6f40LL),
      real(0x1c1406225eaLL),real(0x1e2d6465e2b066fLL),
      // C4[1], coeff of eps^25, polynomial in n of order 4
      -real(0x226e68a74f6c2c0LL),real(0x178fbd94c6e4130LL),
      -real(0x10bafa7048ffb60LL),real(0x7b204e43552d10LL),
      real(0x1ebd785c76c649LL),reale(369943,0xaebaf6655156dLL),
      // C4[1], coeff of eps^24, polynomial in n of order 5
      -real(0x26adfa4c2bcf8500LL),real(0x1be7e116f09bc400LL),
      -real(0x1641521374362300LL),real(0xd7dd4a2b1831200LL),
      -real(0x7449d087ac65100LL),real(0x525502d56a2a1d8LL),
      reale(4562638,0xc0573436eb2ebLL),
      // C4[1], coeff of eps^23, polynomial in n of order 6
      -reale(27299,0x1e7fae46f2ae0LL),reale(20250,0xb050f61211530LL),
      -reale(17170,0x1ccacfb407b40LL),reale(11560,0x5557506ac7a50LL),
      -reale(8300,0x1ee1dfec0f3a0LL),reale(3760,0xc5da39149a170LL),
      real(0x3aaaad07e2dbe15fLL),reale(141441801,0x4a8f52a67aa75LL),
      // C4[1], coeff of eps^22, polynomial in n of order 7
      -reale(223720,0xada70de871dc0LL),reale(168212,0x95f7a36b8e780LL),
      -reale(147708,0x4639d71413140LL),reale(104570,0x398040c96dd00LL),
      -reale(84304,0x27ca2fe2f28c0LL),reale(50205,0xd862a9f308280LL),
      -reale(27426,0xbe7e08935dc40LL),reale(19210,0x9794de13dcf52LL),
      reale(820362447,0x7d3f45c59430dLL),
      // C4[1], coeff of eps^21, polynomial in n of order 8
      -reale(1591044,0x45108afb80980LL),reale(1200725,0xfaaefe8d2aff0LL),
      -reale(1074110,0x244b18cc1fd20LL),reale(779463,0x6e55e2794e4d0LL),
      -reale(667443,0x7f273db50d4c0LL),reale(440073,0xbd38cdf5ffbb0LL),
      -reale(320490,0xb0902bc064460LL),reale(142410,0x1eb038cc00090LL),
      reale(35531,0x5cce3f7afbb81LL),reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[1], coeff of eps^20, polynomial in n of order 9
      -reale(6932123,0xff59c6bb56f80LL),reale(5207764,0x9d4c81592dc00LL),
      -reale(4682178,0xdef9cf054a880LL),reale(3431350,0xdcd7f0ab97d00LL),
      -reale(3036244,0xeb9781cfe3980LL),reale(2097463,0x35c6f48ae00LL),
      -reale(1714507,0xab45478b85280LL),reale(997568,0xe75b4df283f00LL),
      -reale(555001,0x356f72a492380LL),reale(383325,0x3033ad4799914LL),
      reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^19, polynomial in n of order 10
      -reale(10475274,0x80e3f984eb560LL),reale(7761418,0x6cb2d37d31d50LL),
      -reale(6912729,0x2574b8548f80LL),reale(5061056,0xbff13b9f8e7b0LL),
      -reale(4542234,0x9c8561f8559a0LL),reale(3202970,0x45874de1c0010LL),
      -reale(2776395,0x2331e9957c0LL),reale(1780809,0x24244086de270LL),
      -reale(1321308,0xb7d4404aacde0LL),reale(572110,0xf0d923e3d0ad0LL),
      reale(142666,0x15ad08c690505LL),reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^18, polynomial in n of order 11
      -reale(16991539,0x3bfa3a952a5c0LL),reale(12232630,0xc216625651e80LL),
      -reale(10582386,0xca84c044c7740LL),reale(7659664,0x22fef68736200LL),
      -reale(6852368,0xbf4b993050cc0LL),reale(4854746,0x78ae9dfa88580LL),
      -reale(4332124,0x5850c11d91e40LL),reale(2896859,0x8330e6242d100LL),
      -reale(2410777,0x3c4e4b27563c0LL),reale(1359574,0x6f5bc7e308c80LL),
      -reale(775169,0xf705a84369540LL),reale(525423,0x9fd72933d2d3aLL),
      reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^17, polynomial in n of order 12
      -reale(31605635,0x9b2a6245129c0LL),reale(21349095,0xec111ef51efd0LL),
      -reale(17343382,0xc6b59d854f620LL),reale(12224940,0xad54b9902f0LL),
      -reale(10665275,0xcb2c9d1586680LL),reale(7495419,0x2bbe593f97c10LL),
      -reale(6731026,0x5bd11498926e0LL),reale(4567553,0xbb95797dfef30LL),
      -reale(4019270,0xe17fb3dce340LL),reale(2483542,0x18261977df050LL),
      -reale(1889445,0x252a3b83f47a0LL),reale(789608,0x3727b34041370LL),
      reale(196748,0x5030b26b63d7fLL),reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^16, polynomial in n of order 13
      -reale(83651327,0x7df35b769ce00LL),reale(46183264,0x6a662d0fec800LL),
      -reale(32523895,0xbf44a3e60200LL),reale(21575930,0xbd1dba7599c00LL),
      -reale(17706525,0xdbcb8c6749600LL),reale(12151631,0x7c587583d3000LL),
      -reale(10707728,0xa79806e6f4a00LL),reale(7245171,0x8aa6d7e27c400LL),
      -reale(6517082,0x9ff2c462fde00LL),reale(4168671,0x7a21919979800LL),
      -reale(3551918,0x26047c5101200LL),reale(1918361,0x786d4fd8aec00LL),
      -reale(1131511,0x7e7a26769a600LL),reale(747310,0xbb693903a2f10LL),
      reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^15, polynomial in n of order 14
      -reale(63372442,0x2cb5338504ea0LL),reale(236021120,0xed659df2db350LL),
      -reale(86667901,0x5273be9be40LL),reale(47209611,0xc1161d91d1e30LL),
      -reale(33537857,0x3d1f3cdba35e0LL),reale(21739691,0xd5c3b2c9df710LL),
      -reale(18074666,0x2123c601d8980LL),reale(11984705,0x3d2e52a8729f0LL),
      -reale(10682808,0x1cfcfab158d20LL),reale(6875060,0xeec2e9924a2d0LL),
      -reale(6158904,0xf3892aedc14c0LL),reale(3612073,0x775a08e9d4db0LL),
      -reale(2844696,0x4fdad4b74f460LL),reale(1130419,0xe52285ff91690LL),
      reale(281319,0xf8ed6ce679421LL),reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^14, polynomial in n of order 15
      reale(377918798,0xab0ca9f0672c0LL),-reale(418618018,0x8099eba53f80LL),
      -reale(60854873,0x3eafa33f453c0LL),reale(245263030,0xf5560cf897d00LL),
      -reale(90083330,0xb4182a1e90640LL),reale(48226005,0xa87e22e4ae980LL),
      -reale(34666917,0x2b03feac26cc0LL),reale(21804113,0xa9bac4593e00LL),
      -reale(18434597,0x75e58711b4f40LL),reale(11683388,0x18da60c9eb280LL),
      -reale(10544255,0x717858fde75c0LL),reale(6335167,0xce8110cc57f00LL),
      -reale(5568830,0x1a6ca9ba6a840LL),reale(2826076,0xf4ab3cac7db80LL),
      -reale(1750284,0x2ff80145eaec0LL),reale(1113751,0xd17a5fb748e66LL),
      reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^13, polynomial in n of order 16
      -reale(7676111,0x5b2a6c5f6c100LL),-reale(64415807,0x4cf1fd08a9430LL),
      reale(389009273,0x614b445047d20LL),-reale(437396877,0xd309fa5941090LL),
      -reale(57368388,0x6af986a1a0c0LL),reale(255600151,0x61702d3245910LL),
      -reale(94005962,0x2924b0b2256a0LL),reale(49188288,0xa4967a4d0acb0LL),
      -reale(35935634,0xccf0586b2e080LL),reale(21713831,0x3869a07cfee50LL),
      -reale(18759173,0xcf3c8197a7a60LL),reale(11187408,0x277eed08021f0LL),
      -reale(10209411,0xbc33094486040LL),reale(5549613,0x5f33e35304b90LL),
      -reale(4590963,0x90f6e6e49ce20LL),reale(1692490,0x5de933ef26f30LL),
      reale(420297,0x50d0b3d8c1d9bLL),reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^12, polynomial in n of order 17
      -reale(852919,0x6a82cfa963080LL),-reale(2188759,0x20ca5d762f800LL),
      -reale(7786929,0x3421dcca91f80LL),-reale(65787035,0x1d560be049100LL),
      reale(401061675,0x8c48395cfc980LL),-reale(458713135,0x22175c326fa00LL),
      -reale(52544362,0x54a9b8a28c580LL),reale(267237346,0x9f71e62ba7d00LL),
      -reale(98592445,0x567d144d01c80LL),reale(50019657,0x7efcd81e48400LL),
      -reale(37374118,0xabf7952238b80LL),reale(21383288,0xfc61768bbcb00LL),
      -reale(18992011,0x5234632e06280LL),reale(10406178,0xe1fef86250200LL),
      -reale(9523344,0xe57e66503f180LL),reale(4398013,0x8a16c0de4d900LL),
      -reale(2932033,0xa738784cb8880LL),reale(1764194,0xc6396b58af30cLL),
      reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^11, polynomial in n of order 18
      -reale(210362,0x76b369d3025e0LL),-reale(399459,0x1eaf9acef0ab0LL),
      -reale(856141,0xe229f972ba700LL),-reale(2206922,0xef935c87bb50LL),
      -reale(7896496,0x6b0bc697c0820LL),-reale(67217074,0x2cc6331df1df0LL),
      reale(414202467,0x2b5605d0252c0LL),-reale(483149583,0xa02db175d690LL),
      -reale(45836711,0xc18042256fa60LL),reale(280420397,0xa9af8baa076d0LL),
      -reale(104078404,0x7a91f5b525380LL),reale(50585814,0x9d940e3bb2630LL),
      -reale(39015494,0x6a69555b81ca0LL),reale(20678727,0x5f0f1f3a9390LL),
      -reale(19012332,0x416957968b9c0LL),reale(9200947,0xc21b589061af0LL),
      -reale(8178296,0xad1e8ab768ee0LL),reale(2676456,0xd6956da2a1850LL),
      reale(661843,0xede00571b821dLL),reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^10, polynomial in n of order 19
      -reale(73282,0x88acf774cdcc0LL),-reale(119856,0xfafc4232d6980LL),
      -reale(209310,0xc95dad3d9d040LL),-reale(398728,0xc3246fdb30c00LL),
      -reale(857927,0x8ca89fdf097c0LL),-reale(2222415,0x7f22a8f79ee80LL),
      -reale(8002412,0xa401cae100b40LL),-reale(68698832,0xcf05dd2d1e900LL),
      reale(428572510,0x4af905b8fd40LL),-reale(511480829,0xaa7af93dad380LL),
      -reale(36412636,0xa51695c145640LL),reale(295430858,0x62539c3ab7a00LL),
      -reale(110834541,0xf7ac6a286ddc0LL),reale(50648730,0xf42d6a1912780LL),
      -reale(40882711,0xc825af61d7140LL),reale(19389515,0xc578a6be65d00LL),
      -reale(18548541,0x30b0433e6e8c0LL),reale(7353872,0xa4f0c77ab4280LL),
      -reale(5517208,0xc642445621c40LL),reale(3035548,0x619b33f1391d2LL),
      reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^9, polynomial in n of order 20
      -reale(31116,0x5ced59f2a6a40LL),-reale(46466,0x39ef1648a3c30LL),
      -reale(72339,0x13bec712995a0LL),-reale(118591,0xe96704ee23c10LL),
      -reale(207681,0xf3272ddf69500LL),-reale(396975,0x5586a3fda15f0LL),
      -reale(857776,0x96a9e394d3460LL),-reale(2234014,0x9c760527155d0LL),
      -reale(8101033,0x1f3b77f93fc0LL),-reale(70217181,0xc7476a97287b0LL),
      reale(444320933,0x84d59896b7ce0LL),-reale(544755366,0x60ab42e093790LL),
      -reale(22958170,0x5fc77e584ca80LL),reale(312550991,0xea91e4bc80e90LL),
      -reale(119474190,0x655c7a979e1e0LL),reale(49778595,0x69cfb591beb0LL),
      -reale(42938053,0xad555dfab9540LL),reale(17185991,0x9567a8e814cd0LL),
      -reale(16947236,0xc941a0517b0a0LL),reale(4507394,0xb6bfddcb2cf0LL),
      reale(1103154,0xee71952935057LL),reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^8, polynomial in n of order 21
      -reale(15013,0x669ca85dbff00LL),-reale(21081,0x7f4d799198400LL),
      -reale(30470,0xbdb587d74d900LL),-reale(45587,0xe4badb51b1a00LL),
      -reale(71124,0x646ea35b6300LL),-reale(116891,0x8adb62aa4d000LL),
      -reale(205315,0x1aa2ab2ec7d00LL),-reale(393884,0x4b8d8eda78600LL),
      -reale(855000,0x2faa553050700LL),-reale(2239966,0xb31164c141c00LL),
      -reale(8186764,0x97347e701e100LL),-reale(71742883,0x7f111739b7200LL),
      reale(461586973,0x9a516d5401500LL),-reale(584418823,0xe1245bd6e6800LL),
      -reale(3315305,0x14110f9c0500LL),reale(331936814,0x28269ca022200LL),
      -reale(131069117,0x7ee7ad0730f00LL),reale(47184778,0x227a729454c00LL),
      -reale(44897669,0x9cd1b2a1e900LL),reale(13574545,0xcd96a182a3600LL),
      -reale(12485695,0x45db16a057300LL),reale(5879734,0x70bef82b8988LL),
      reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^7, polynomial in n of order 22
      -reale(7900,0x638c66d8a8320LL),-reale(10613,0xf2ac3092c9cb0LL),
      -reale(14565,0xe107ae27501c0LL),-reale(20489,0xead89ce414d0LL),
      -reale(29670,0x849ce08edf860LL),-reale(44482,0xeb1f022729ef0LL),
      -reale(69562,0xbdfcfee35b00LL),-reale(114632,0x975e8fa16f10LL),
      -reale(201989,0x9411d71111da0LL),-reale(389021,0x33d7ff034b930LL),
      -reale(848628,0xc0285ec233440LL),-reale(2237713,0xb97d9ca55b150LL),
      -reale(8250880,0x9132887d792e0LL),-reale(73221392,0xf1ffe05c8b70LL),
      reale(480452831,0x383b5471fd280LL),-reale(632496874,0xca3591eba7b90LL),
      reale(26233104,0x13df159bb07e0LL),reale(353203487,0x101c2c33c4a50LL),
      -reale(147596513,0x7a337ff05e6c0LL),reale(41406718,0x88562e0e69230LL),
      -reale(45513246,0x22b5bfcbced60LL),reale(7934370,0xa8c8e9d8c2810LL),
      reale(1869414,0xdc5c61854a479LL),reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^6, polynomial in n of order 23
      -reale(4406,0xf939ae5c97c40LL),-reale(5729,0xf863eba5bf80LL),
      -reale(7570,0xa927e082c4c0LL),-reale(10189,0xdc3d2b5930900LL),
      -reale(14011,0xfd72406188940LL),-reale(19751,0x4ee9330f94280LL),
      -reale(28665,0xa6c18d00fb1c0LL),-reale(43078,0xe8ed052a45400LL),
      -reale(67543,0xd4150add2640LL),-reale(111634,0xb28e55bb02580LL),
      -reale(197389,0xccdd68505cec0LL),-reale(381765,0x22e00b9b89f00LL),
      -reale(837258,0xa000eefe9340LL),-reale(2223425,0xd3d15b309a880LL),
      -reale(8279438,0xc28db224c5bc0LL),-reale(74551261,0xb7816e54f2a00LL),
      reale(500824278,0x3891b999befc0LL),-reale(691847154,0x918a2dd450b80LL),
      reale(72461747,0xa045596356740LL),reale(374046829,0x41b777218cb00LL),
      -reale(172833056,0x62b9485f4dd40LL),reale(29915148,0x80284d25e7180LL),
      -reale(39423763,0x40d338467c5c0LL),reale(13659048,0x68e501c228ffeLL),
      reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^5, polynomial in n of order 24
      -reale(2545,0x1363104362d80LL),-reale(3226,0xe67b1424a4830LL),
      -reale(4144,0x8c711302fa660LL),-reale(5400,0xc1bfe2853af90LL),
      -reale(7153,0xb2c26c1682b40LL),-reale(9653,0x9e8ef4e7cf0f0LL),
      -reale(13308,0xeb09aee491820LL),-reale(18810,0x561040fe22850LL),
      -reale(27375,0xc35e0fb3fc900LL),-reale(41260,0x7d7f41fc271b0LL),
      -reale(64893,0xc7a96414399e0LL),-reale(107622,0xe02e2157de910LL),
      -reale(191035,0x6ce8a0a1be6c0LL),-reale(371181,0x96988a373aa70LL),
      -reale(818768,0xa91a46aa60ba0LL),-reale(2191167,0x9fde37effd1d0LL),
      -reale(8249435,0xe27cdc35b6480LL),-reale(75540143,0x55cc77d97b30LL),
      reale(522119910,0xf5aa540a8b2a0LL),-reale(766397212,0x64559a510c290LL),
      reale(148547296,0x8152775e2ddc0LL),reale(385247751,0x81b301a133c10LL),
      -reale(213402544,0x90fce845e3f20LL),reale(10198756,0x255c7c31664b0LL),
      reale(1365904,0xd74a19c69db33LL),reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^4, polynomial in n of order 25
      -real(0x5cd20bbc3c672180LL),-real(0x73720b2d98187c00LL),
      -reale(2321,0xc4eb857568680LL),-reale(2952,0xb2617088c8f00LL),
      -reale(3804,0x417bd8fa2e380LL),-reale(4973,0x5ec86f601d200LL),
      -reale(6609,0x998272f30a880LL),-reale(8950,0x197c7ab46b500LL),
      -reale(12382,0xcc481e2a44580LL),-reale(17565,0x5f7861969a800LL),
      -reale(25660,0x4a6f330e22a80LL),-reale(38825,0xe447100991b00LL),
      -reale(61313,0x47573aa0ec780LL),-reale(102123,0xa55bb6037e00LL),
      -reale(182121,0xfb4d0590e8c80LL),-reale(355742,0x340be91b74100LL),
      -reale(789743,0xf318e4285e980LL),-reale(2131260,0x2c59b0f82d400LL),
      -reale(8121193,0x3f9cc7c594e80LL),-reale(75808472,0x814742dd4a700LL),
      reale(542406027,0xe15955752d480LL),-reale(860719085,0xb088c959b2a00LL),
      reale(281794203,0x6d691a09a0f80LL),reale(349671639,0x4a19c69db3300LL),
      -reale(268081590,0x1f35e51280d80LL),reale(42616231,0x9d4bdce6b704LL),
      reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^3, polynomial in n of order 26
      -real(0x34f88b61ee2c2e60LL),-real(0x40e8b73250ad02b0LL),
      -real(0x50402824a1190680LL),-real(0x643133a56bf6de50LL),
      -real(0x7e70b50d7e53aea0LL),-reale(2583,0x89ee9103c6bf0LL),
      -reale(3343,0x2d56b6f20aac0LL),-reale(4390,0x9150bee746f90LL),
      -reale(5862,0xecb9ee1767ee0LL),-reale(7978,0x9b4551158ad30LL),
      -reale(11096,0x13774a5e7af00LL),-reale(15825,0x3f23db737e8d0LL),
      -reale(23248,0xf45a340cbf20LL),-reale(35380,0xaf4478627e670LL),
      -reale(56209,0x8a81f32e3340LL),-reale(94205,0x2f98ae2576a10LL),
      -reale(169093,0xeae4ad4ee8f60LL),-reale(332577,0xf0ed8664037b0LL),
      -reale(743995,0x906300fb45780LL),-reale(2026493,0x9c6e844791350LL),
      -reale(7821602,0x7531c16940fa0LL),-reale(74557824,0x1ed43b2e7c0f0LL),
      reale(555703654,0x34418f385c440LL),-reale(974709694,0x84f4a67130490LL),
      reale(527421389,0x42f7f1faaa020LL),reale(94702735,0xa411a5cab5dd0LL),
      -reale(117194635,0x5b0909f7a774bLL),
      reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^2, polynomial in n of order 27
      -real(0x1bd57a8f504dd3c0LL),-real(0x21b6ff10b9172180LL),
      -real(0x292825cda3a88940LL),-real(0x32aacbfadedfca00LL),
      -real(0x3ef38a62fa0322c0LL),-real(0x4f013a1cfd80d280LL),
      -real(0x64414a4729c69840LL),-reale(2060,0x90ead26a03300LL),
      -reale(2683,0x237c6d92be1c0LL),-reale(3547,0x3d9a05c33e380LL),
      -reale(4770,0x6ec9da59bf740LL),-reale(6541,0x1657e411dc00LL),
      -reale(9170,0x1a8b4944fd0c0LL),-reale(13190,0xb069410801480LL),
      -reale(19554,0x9e393a3b06640LL),-reale(30047,0xba30505448500LL),
      -reale(48224,0x707d4f4f6afc0LL),-reale(81689,0xf05ca40b52580LL),
      -reale(148265,0xab90de58ba540LL),-reale(294962,0x64373b047ee00LL),
      -reale(667587,0xc0c688fa83ec0LL),-reale(1840377,0xc842d822d680LL),
      -reale(7199121,0xfc41489b57440LL),-reale(69934327,0xdb9ec152bd700LL),
      reale(541991040,0xe60e5a413c240LL),-reale(1060670639,0x2d9274118e780LL),
      reale(833384073,0xa3ce7fc4a6cc0LL),-reale(234389270,0xb61213ef4ee96LL),
      reale(12305436712LL,0x56b51693aedc3LL),
      // C4[1], coeff of eps^1, polynomial in n of order 28
      -real(0xb4c355cd41c92c0LL),-real(0xd8fea3a41cc7830LL),
      -real(0x1064f0c6b9a6ad20LL),-real(0x13f7a88902ef1b10LL),
      -real(0x1884a414973fcb80LL),-real(0x1e5fa2ae5243d7f0LL),
      -real(0x25fe0bb384ddd9e0LL),-real(0x3006f6e3e0e25ad0LL),
      -real(0x3d6c2c13c34ec440LL),-real(0x4f91f34825bd4fb0LL),
      -real(0x688ffb74f98676a0LL),-reale(2233,0xdec33bb086290LL),
      -reale(3036,0xe53843c2cdd00LL),-reale(4213,0xb13e1137e3f70LL),
      -reale(5984,0xaa1cca8abe360LL),-reale(8732,0xb9880d6c69250LL),
      -reale(13152,0x1eadcfcfd75c0LL),-reale(20566,0x4e1752c3c0730LL),
      -reale(33653,0xf4262a5798020LL),-reale(58247,0x3a420e3524a10LL),
      -reale(108257,0x7934f39e3ee80LL),-reale(221025,0xaccc1c0dc06f0LL),
      -reale(514222,0xffbb852faace0LL),-reale(1456965,0x29e8a4070e9d0LL),
      -reale(5827860,0xa7a2901c3a740LL),-reale(56821641,0x6270fd1339eb0LL),
      reale(416692036,0xd1e73fe253660LL),-reale(625038055,0x3adadfd37d190LL),
      reale(273454149,0x29bfc1ec86bafLL),
      reale(12305436712LL,0x56b51693aedc3LL),
      // C4[2], coeff of eps^29, polynomial in n of order 0
      185528,real(30429886905LL),
      // C4[2], coeff of eps^28, polynomial in n of order 1
      real(17366491968LL),real(4404238552LL),real(0x74e318fa9c07fLL),
      // C4[2], coeff of eps^27, polynomial in n of order 2
      real(412763643136LL),-real(248137794944LL),real(164642704408LL),
      real(0x4d882f0532d9e9LL),
      // C4[2], coeff of eps^26, polynomial in n of order 3
      real(0x11462b92d913a0LL),-real(0xdd4620ebadc40LL),
      real(0x5974730e46be0LL),real(0x16bcec57851ccLL),
      reale(33547,0x1cf91962af003LL),
      // C4[2], coeff of eps^25, polynomial in n of order 4
      real(0xc83679b433c00LL),-real(0xb29b6d58dfb00LL),real(0x5f4e3bdd4de00LL),
      -real(0x3affd9960e900LL),real(0x2665fb625f490LL),
      reale(15809,0x8f200ee7e2a7dLL),
      // C4[2], coeff of eps^24, polynomial in n of order 5
      real(0x67b92a8524a18e80LL),-real(0x609d7d3ca356ae00LL),
      real(0x39db180d1b52d580LL),-real(0x2fa1e9183dec9700LL),
      real(0x1294d8f2627edc80LL),real(0x4bc94ddbc9bad70LL),
      reale(22813193,0xc1b4051297e97LL),
      // C4[2], coeff of eps^23, polynomial in n of order 6
      reale(24830,0x3d0fb879bb600LL),-reale(23212,0xa100635ccdb00LL),
      reale(14957,0x147cd156ba400LL),-reale(13653,0x51ea4b9c89d00LL),
      reale(7024,0x2535370909200LL),-reale(4511,0x3af63b60c9f00LL),
      reale(2865,0xf50f5adcce1f0LL),reale(235736335,0x7c44346acc6c3LL),
      // C4[2], coeff of eps^22, polynomial in n of order 7
      reale(1046092,0x25a6222f26060LL),-reale(949436,0x14a3a722f1840LL),
      reale(652845,0xb96689ab42720LL),-reale(615919,0x6f1345ab50580LL),
      reale(356624,0x982d38f2a9de0LL),-reale(303839,0x22c37d5c832c0LL),
      reale(113262,0x286189b57e4a0LL),reale(28978,0x12ae8b059bc84LL),
      reale(6836353729LL,0x13b9f01928417LL),
      // C4[2], coeff of eps^21, polynomial in n of order 8
      reale(4643688,0x71b79cbf7cc00LL),-reale(3959056,0x83e38a4f9d180LL),
      reale(2926140,0x6f81ce5fc3900LL),-reale(2722736,0xdd03df5282c80LL),
      reale(1710940,0xc70403130e600LL),-reale(1602990,0x9ebb76967a780LL),
      reale(787738,0x6bf60987b1300LL),-reale(530212,0xcde2a88ab0280LL),
      reale(326645,0xab9033855e368LL),reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^20, polynomial in n of order 9
      reale(2366152,0x4fc26559c91c0LL),-reale(1830925,0x4d73259824200LL),
      reale(1477489,0x62c9a90a52a40LL),-reale(1299560,0xe7bf798235180LL),
      reale(885946,0x5cb0a99f5e2c0LL),-reale(843740,0x47153eb842100LL),
      reale(469359,0x79db9d7cfb40LL),-reale(417111,0x1a4c5e2477080LL),
      reale(146559,0x51b0aa3dcb3c0LL),reale(37677,0x6dd5ee66abd48LL),
      reale(6836353729LL,0x13b9f01928417LL),
      // C4[2], coeff of eps^19, polynomial in n of order 10
      reale(11390177,0xa8f910291300LL),-reale(7729638,0x6f23cf47c2480LL),
      reale(6929266,0x5fb765e065c00LL),-reale(5514735,0x5eb0876136380LL),
      reale(4148166,0x27d6c40aa500LL),-reale(3788609,0xfef33001c8280LL),
      reale(2322601,0x1de03c2bc2e00LL),-reale(2237878,0x77b7642b94180LL),
      reale(1037457,0x571c66f013700LL),-reale(742165,0x8c39e6d5b6080LL),
      reale(439349,0xf7cfa6e796fc8LL),reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^18, polynomial in n of order 11
      reale(19643005,0x3eb0d373a0e0LL),-reale(11359402,0x98e8f09139c0LL),
      reale(11381255,0xacc1b03fd73a0LL),-reale(7834592,0x92741bdd3b00LL),
      reale(6664656,0xa317edb25b660LL),-reale(5516050,0x3ff87cc43bc40LL),
      reale(3774293,0xd5e83edc68920LL),-reale(3594547,0xbec9f61701d80LL),
      reale(1908400,0x61c5f793c0be0LL),-reale(1786093,0xfaf3f7a19bec0LL),
      reale(579905,0x9d50696085ea0LL),reale(150042,0xa9efa9004c604LL),
      reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^17, polynomial in n of order 12
      reale(38321815,0x1e48683dc9800LL),-reale(18616913,0x727791f8dfa00LL),
      reale(20113440,0xb841223d75400LL),-reale(11495937,0x9838f29931e00LL),
      reale(11261630,0x21fd3747b1000LL),-reale(7960716,0x75135ee9c200LL),
      reale(6275150,0xa8a2fa972cc00LL),-reale(5471565,0x945df446e600LL),
      reale(3293426,0x6eab44c698800LL),-reale(3257897,0x559df659f8a00LL),
      reale(1401057,0x756ea738a4400LL),-reale(1086629,0xf49cb94a8ae00LL),
      reale(610116,0x479bdc6c290e0LL),reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^16, polynomial in n of order 13
      reale(102781113,0x98fe5a9192500LL),-reale(40336104,0xccc089a851400LL),
      reale(40165652,0x6e617f3b73300LL),-reale(18616625,0x95536d5576600LL),
      reale(20514709,0xd39b96f5ec100LL),-reale(11691503,0x7c1154bb0b800LL),
      reale(10980290,0x40d1adbe6cf00LL),-reale(8104717,0x4a433bfb60a00LL),
      reale(5726151,0xc3b2b2965d00LL),-reale(5331323,0xa4559d80c5c00LL),
      reale(2689333,0x7cf2f82446b00LL),-reale(2678624,0x7904ff2b8ae00LL),
      reale(779755,0xfacbca777f900LL),reale(203539,0xb4670b88476e0LL),
      reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^15, polynomial in n of order 14
      -reale(23295494,0x8be82e34e6400LL),-reale(256522224,0x1264f586eb600LL),
      reale(109420782,0x9692235ce1800LL),-reale(40005401,0x76f47ac799a00LL),
      reale(42210732,0x9175627089400LL),-reale(18637789,0x360d04338fe00LL),
      reale(20777547,0x32d7f69c1000LL),-reale(11978808,0x3c6fce691e200LL),
      reale(10467739,0x890cbd2438c00LL),-reale(8246695,0x5d95a89294600LL),
      reale(4981450,0x2e83f5dba0800LL),-reale(4997884,0x48d2490e42a00LL),
      reale(1949724,0xd6b9d613a8400LL),-reale(1687002,0x42840cd678e00LL),
      reale(881316,0x5154c853b06e0LL),reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^14, polynomial in n of order 15
      -reale(315852553,0x127aa1fb9560LL),reale(452067016,0x32f06289dc340LL),
      -reale(36389203,0xc905d2dd0bc20LL),-reale(265701999,0x414c3c9652f80LL),
      reale(117462481,0xb44ff33f8ed20LL),-reale(39375172,0xb9e521c5c6240LL),
      reale(44443567,0x98c20ae94660LL),-reale(18737379,0x9088d09ce7500LL),
      reale(20789662,0x74772cb6e2fa0LL),-reale(12399165,0xc39cbc16e07c0LL),
      reale(9634015,0x48be8ec7788e0LL),-reale(8326007,0x8f1246dddba80LL),
      reale(4012687,0x8a9763f933220LL),-reale(4283805,0xe15bd5742d40LL),
      reale(1064918,0x3e0322e890b60LL),reale(281445,0x189dacfa2913cLL),
      reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^13, polynomial in n of order 16
      reale(4607575,0xc9d7900c88800LL),reale(44527228,0x61b96ac1eb380LL),
      -reale(320302478,0xa276d3450e900LL),reale(471382647,0x4d0623cc86a80LL),
      -reale(52535715,0x404f1a5b09a00LL),-reale(275262322,0xf3348bb543e80LL),
      reale(127364360,0xbf0504ec13500LL),-reale(38376532,0x74833ebc78780LL),
      reale(46801690,0x6a3245e5c4400LL),-reale(19021914,0x3bda110f1b080LL),
      reale(20372666,0xf7fc04d85300LL),-reale(12992077,0x825700022f980LL),
      reale(8374681,0xba502a56d2200LL),-reale(8187369,0x8d48a8bba280LL),
      reale(2818780,0x7113503f27100LL),-reale(2834494,0xf2038f04beb80LL),
      reale(1337917,0xc906f381aecf8LL),reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^12, polynomial in n of order 17
      reale(388658,0x19c7c6f8ea2c0LL),reale(1117971,0xaadcbdb38ac00LL),
      reale(4519560,0xaee28ee393540LL),reale(44278119,0xe09b9f50af680LL),
      -reale(324493551,0x5c00bae29840LL),reale(492697628,0x7d1cc3fd18100LL),
      -reale(72657626,0xb42806bf185c0LL),-reale(284925253,0x57cc84a557480LL),
      reale(139770748,0x33e950dc3acc0LL),-reale(36961790,0xef70c005baa00LL),
      reale(49119876,0xa052562f03f40LL),-reale(19681131,0xbaa50226adf80LL),
      reale(19252422,0xc3af9265b71c0LL),-reale(13755373,0x2f0960c0cd500LL),
      reale(6600104,0x6565773f88440LL),-reale(7462805,0xbfb982e534a80LL),
      reale(1452711,0x6b2cd84feb6c0LL),reale(390635,0x965de9321fbe8LL),
      reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^11, polynomial in n of order 18
      reale(73868,0xf53613318fd00LL),reale(155158,0x6bea1fc037e80LL),
      reale(370865,0xe686995a3a800LL),reale(1077531,0xb6b00d00e5180LL),
      reale(4409046,0x1d5f244685300LL),reale(43860006,0xf94485a638480LL),
      -reale(328226208,0x254b380304200LL),reale(516242826,0x48cfde1d3d780LL),
      -reale(98028430,0xc7227901d5700LL),-reale(294125055,0xf41dd5cbff580LL),
      reale(155591277,0xc58331ae9d400LL),-reale(35168366,0x6c3820d072280LL),
      reale(51023141,0xfcae9f00dff00LL),-reale(21033813,0x6b0840ce0ef80LL),
      reale(17035669,0xa0ab037f7ea00LL),-reale(14520825,0x209891efc9c80LL),
      reale(4321952,0xda1143d705500LL),-reale(5322397,0x9ed9b44796980LL),
      reale(2165443,0xa5af00ad58358LL),reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^10, polynomial in n of order 19
      reale(19809,0x63304b335a660LL),reale(35566,0xcb4164f348e40LL),
      reale(68577,0xe86c972757e20LL),reale(145245,0xbc9cc7446e200LL),
      reale(350489,0x7e29a3d4285e0LL),reale(1029750,0x45087f82835c0LL),
      reale(4270842,0x2203011585da0LL),reale(43220702,0xa65b618eca980LL),
      -reale(331199124,0xa89ccd5235aa0LL),reale(542217711,0x200e3727c5d40LL),
      -reale(130429686,0x3b8b1d50d02e0LL),-reale(301749371,0x2c4d836f88f00LL),
      reale(176097282,0x8ddfe73d104e0LL),-reale(33280999,0x8c12e2a85fb40LL),
      reale(51717673,0x23cc103525ca0LL),-reale(23558374,0x76fe0e70fc780LL),
      reale(13250268,0x69c1c450ca460LL),-reale(14595460,0xd8a80a3d5d3c0LL),
      reale(1848614,0x7d3564e37c20LL),reale(506231,0x2a6100a6a6db4LL),
      reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^9, polynomial in n of order 20
      reale(6397,0xfcd62c9faa400LL),reale(10440,0x3fc8ff8e75700LL),
      reale(17841,0xb7bede1dba00LL),reale(32272,0x7935213063d00LL),
      reale(62742,0x8933a9bfd5000LL),reale(134128,0x223daf23d6300LL),
      reale(327129,0xfca43cca0e600LL),reale(973230,0x31dda9e44900LL),
      reale(4098328,0x3528b970ffc00LL),reale(42289297,0xe5d54d5326f00LL),
      -reale(332951092,0xecfda756dee00LL),reale(570709002,0x2878cf4ff5500LL),
      -reale(172380399,0x5788b53115800LL),-reale(305626020,0x9c65fcc7d8500LL),
      reale(202987914,0xbd0aab0ad3e00LL),-reale(32233434,0x3f0406dec9f00LL),
      reale(49604551,0xc747777555400LL),-reale(27757216,0x323bffb167900LL),
      reale(7652705,0x1c15203ae6a00LL),-reale(11782806,0x2b7827f239300LL),
      reale(3811565,0x362856b8e6d30LL),reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^8, polynomial in n of order 21
      reale(2297,0xe5959dcaf9680LL),reale(3515,0xaf44e93439a00LL),
      reale(5557,0xf844363205d80LL),reale(9134,0x3148872cf3100LL),
      reale(15730,0x1f27208afe480LL),reale(28695,0xbe2e993314800LL),
      reale(56314,0x2c7b05479ab80LL),reale(121661,0x287926e675f00LL),
      reale(300328,0xfc8a376113280LL),reale(906274,0xf1fb199eef600LL),
      reale(3883000,0x5f528c391f980LL),reale(40968060,0xe6e08c5558d00LL),
      -reale(332763533,0x8282a4a507f80LL),reale(601507851,0xf6ba284c8a400LL),
      -reale(227453313,0x642fd223ab880LL),-reale(301473974,0xbe5976c5a4500LL),
      reale(238209921,0x57c5b91e6ce80LL),-reale(34582562,0x41ecac4f5ae00LL),
      reale(41696071,0xee870caef9580LL),-reale(33183269,0xa456f79c1700LL),
      reale(1407347,0x27b05f0931c80LL),reale(329283,0x26010fabff570LL),
      reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^7, polynomial in n of order 22
      real(0x367dbe5da7953e00LL),real(0x4f9a921ac6fb1900LL),
      real(0x773454548df74400LL),reale(2938,0xbc18faed4af00LL),
      reale(4681,0x407a350a64a00LL),reale(7756,0xa0ed83ee90500LL),
      reale(13477,0x2fbfd87edd000LL),reale(24826,0x9ea174e739b00LL),
      reale(49249,0xd3391f1d95600LL),reale(107696,0xcac2013cff100LL),
      reale(269571,0xe064d3a745c00LL),reale(826840,0x70825da398700LL),
      reale(3613882,0x7ef0aa40a6200LL),reale(39120270,0xc5673698bdd00LL),
      -reale(329492011,0x53f65ac991800LL),reale(633695353,0xfeb5c44027300LL),
      -reale(300630213,0xecf09fbea9200LL),-reale(280700646,0xcee0a2073700LL),
      reale(282664342,0x7b726e8a17400LL),-reale(46720160,0x11dfe8c55a100LL),
      reale(23527957,0x90f427ad67a00LL),-reale(33848503,0x5eac35f0d4b00LL),
      reale(7456233,0x7c1f0b332cab0LL),reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^6, polynomial in n of order 23
      real(0x14f52a063dc5fc20LL),real(0x1d93a1e9ceb48740LL),
      real(0x2a911c303b723a60LL),real(0x3ea26bba66a54980LL),
      real(0x5e84fad71b3608a0LL),reale(2349,0x85d3117e94bc0LL),
      reale(3776,0x1c9d51cf2c6e0LL),reale(6317,0x5193932d16e00LL),
      reale(11091,0xc7716ff97d520LL),reale(20667,0xe33c2c4a29040LL),
      reale(41523,0x1a30a42ae9360LL),reale(92100,0xbd0a1f1419280LL),
      reale(234309,0x70b77706661a0LL),reale(732507,0x72fafb4df54c0LL),
      reale(3276808,0xe462aef209fe0LL),reale(36551902,0x4c4d10a4b700LL),
      -reale(321265885,0x720bf168351e0LL),reale(664675522,0x65892c55e9940LL),
      -reale(398339257,0x2b82ef41c13a0LL),-reale(225754486,0xf240500d62480LL),
      reale(330356701,0xbb7252695baa0LL),-reale(82401980,0x37f104ae0a240LL),
      -reale(4970822,0x52bf5cccc8720LL),-reale(3278171,0x9e4b710fe0e14LL),
      reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^5, polynomial in n of order 24
      real(0x7d5242068d47400LL),real(0xac3832c9e621080LL),
      real(0xf0840d5e59cf500LL),real(0x155fabefd3362980LL),
      real(0x1f01ffac4c30b600LL),real(0x2e0489bbd6aca280LL),
      real(0x461560bdbc05f700LL),real(0x6df6210d29c3bb80LL),
      reale(2857,0xf2e1b87d2f800LL),reale(4836,0xd8d8f4249b480LL),
      reale(8600,0x17271d36df900LL),reale(16248,0x163bc1ffccd80LL),
      reale(33146,0xc23750bad3a00LL),reale(74792,0x260310eab4680LL),
      reale(194024,0xef2cdae46fb00LL),reale(620545,0xfcf47db535f80LL),
      reale(2853712,0x7228ad7b17c00LL),reale(32984640,0x1c4ce82435880LL),
      -reale(304937768,0x83ef272fd0300LL),reale(687819348,0xf9e0f9c397180LL),
      -reale(526420007,0xa1ce2482e4200LL),-reale(101220737,0xb065c6f7c1580LL),
      reale(344186593,0xf79ee4a13ff00LL),-reale(151524377,0x682a2ddefc80LL),
      reale(15298134,0x380aba4a19708LL),reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^4, polynomial in n of order 25
      real(0x2b077c634ede840LL),real(0x39e80232e455600LL),
      real(0x4f004399e9803c0LL),real(0x6d6a8dd96e7d980LL),
      real(0x9a16639c690ff40LL),real(0xdd0eb6a29ee1d00LL),
      real(0x143ca2e567649ac0LL),real(0x1e583a687f6ce080LL),
      real(0x2ebb5ae27bca9640LL),real(0x4a366ef6d0a8e400LL),
      real(0x7a244f6987aeb1c0LL),reale(3355,0xff6a995ee780LL),
      reale(6059,0x95d9afc38ad40LL),reale(11647,0x91c4ac30bab00LL),
      reale(24220,0xbe377a4d448c0LL),reale(55835,0xd9394a033ee80LL),
      reale(148417,0x27a782b394440LL),reale(488256,0xe5126fdac7200LL),
      reale(2322515,0xb040a0735fc0LL),reale(28019858,0x3d9464fe1f580LL),
      -reale(275064197,0x290d46715a4c0LL),reale(686424553,0x6984a82213900LL),
      -reale(677745912,0x9f6fb36960940LL),reale(151524377,0x682a2ddefc80LL),
      reale(169007958,0xfd6a53329f240LL),-reale(85232462,0x13a97b9cd6e08LL),
      reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^3, polynomial in n of order 26
      real(0xc4c78b5f73e700LL),real(0x1046756e5efb980LL),
      real(0x15cbc98d9fba400LL),real(0x1d9279681ffce80LL),
      real(0x28b2f34344c6100LL),real(0x38e6214caec8380LL),
      real(0x50f0f0d0c655e00LL),real(0x7563dc0de2d1880LL),
      real(0xadfad5eb325db00LL),real(0x1083ab8775a8cd80LL),
      real(0x19c9d8efc1ad1800LL),real(0x29945e7f0056e280LL),
      real(0x4594bf2102ba5500LL),real(0x79a9d12705de9780LL),
      reale(3587,0xb2b264e0cd200LL),reale(7053,0x1d58043372c80LL),
      reale(15040,0x44c8073c3cf00LL),reale(35667,0x702872e47e180LL),
      reale(97902,0x6929355be8c00LL),reale(334186,0x1d1de4e87f680LL),
      reale(1659947,0xed2beccfc4900LL),reale(21110207,0x53559189eab80LL),
      -reale(222144335,0x8c70c0703ba00LL),reale(617753229,0x694fabb034080LL),
      -reale(769277606,0x6fd24e8e23d00LL),reale(454573131,0x1387e899cf580LL),
      -reale(104173009,0x3479cff894d98LL),
      reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[2], coeff of eps^2, polynomial in n of order 27
      real(0x24546bc28a93e0LL),real(0x2f6c4d745b8e40LL),
      real(0x3e90f252c210a0LL),real(0x5380c389acd700LL),
      real(0x70da9adde57d60LL),real(0x9aa08aca5a9fc0LL),
      real(0xd7127fe199fa20LL),real(0x130248120008880LL),
      real(0x1b6103e1c56a6e0LL),real(0x283fa247b6e3140LL),
      real(0x3c89da46fe8a3a0LL),real(0x5d71643158b3a00LL),
      real(0x948b363af771060LL),real(0xf445a32263b42c0LL),
      real(0x1a1d56e9fe070d20LL),real(0x2ecb290f0241eb80LL),
      real(0x58a5da95527fb9e0LL),reale(2876,0x680343126d440LL),
      reale(6354,0x3e35c062e36a0LL),reale(15689,0x7d2910c199d00LL),
      reale(45107,0x47d6102c9a360LL),reale(162386,0x35cf6d6d5e5c0LL),
      reale(857038,0x54e3334f72020LL),reale(11655721,0x4f45203874e80LL),
      -reale(131126864,0xbbc9aa7b23320LL),reale(378810942,0x9046972ad7740LL),
      -reale(416692036,0xd1e73fe253660LL),reale(156259513,0xceb6b7f4df464LL),
      reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[3], coeff of eps^29, polynomial in n of order 0
      594728,real(456448303575LL),
      // C4[3], coeff of eps^28, polynomial in n of order 1
      -real(3245452288LL),real(1965206256),real(0x17609e98859b3LL),
      // C4[3], coeff of eps^27, polynomial in n of order 2
      -real(0x15f49b7dd3600LL),real(0x7876e24c6900LL),real(0x1f5dd75c0b28LL),
      reale(4837,0x68f14547adebLL),
      // C4[3], coeff of eps^26, polynomial in n of order 3
      -real(0x33418e8004000LL),real(0x17b00d59dc000LL),
      -real(0x11669ade1c000LL),real(0xa37322475bc0LL),
      reale(6709,0x6c31d1e089667LL),
      // C4[3], coeff of eps^25, polynomial in n of order 4
      -real(0xc3e38d2fc36800LL),real(0x6a604d6faf7a00LL),
      -real(0x650b3de948f400LL),real(0x20a6596010be00LL),
      real(0x88f534a1fae70LL),reale(275086,0x53fa9cf60167fLL),
      // C4[3], coeff of eps^24, polynomial in n of order 5
      -real(0xdd5f9d233a5800LL),real(0x8b724926c9e000LL),
      -real(0x8af41510346800LL),real(0x3d05686ce77000LL),
      -real(0x2f9901c72df800LL),real(0x1ae74f29ea4ce0LL),
      reale(223345,0xf3eec944ed143LL),
      // C4[3], coeff of eps^23, polynomial in n of order 6
      -reale(81630,0xcf55ff9c68c00LL),reale(60811,0x59dd5ef6a6e00LL),
      -reale(57592,0x6457f059a8800LL),reale(30387,0x2572e53b9c200LL),
      -reale(30167,0xe11b4690d8400LL),reale(9044,0xd72699d03d600LL),
      reale(2392,0x21f43a8f7f830LL),reale(990092609,0x9eb428d5a933LL),
      // C4[3], coeff of eps^22, polynomial in n of order 7
      -reale(3070961,0xf14af9164000LL),reale(2767073,0x4d2d51bbc4000LL),
      -reale(2322170,0xf623e90f3c000LL),reale(1476552,0x4ed8bf53f8000LL),
      -reale(1490469,0x7e13eaba44000LL),reale(616004,0x8b84c9ea6c000LL),
      -reale(517487,0xf3178ed39c000LL),reale(279040,0x23dc4dd774ec0LL),
      reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^21, polynomial in n of order 8
      -reale(3998482,0x374a7520d6800LL),reale(4351696,0x89a9dbf785900LL),
      -reale(3077852,0x4b8dc9fbd6e00LL),reale(2436308,0x9b47462d3fb00LL),
      -reale(2230379,0xda399323b400LL),reale(1147885,0x7a5199072bd00LL),
      -reale(1196012,0x91bb473d37a00LL),reale(325643,0x5e75ef9e35f00LL),
      reale(87110,0x728c765d95698LL),reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^20, polynomial in n of order 9
      -reale(5536106,0x41a6dc97e5400LL),reale(6819318,0x7020ae33aa000LL),
      -reale(3996497,0x7d04a5d65ec00LL),reale(4026336,0x4a526eb153800LL),
      -reale(3081046,0x922df73cac400LL),reale(2027203,0x8c3cc70035000LL),
      -reale(2046086,0x4cc9bc51b5c00LL),reale(787253,0x8fa9057e6800LL),
      -reale(725367,0x21dd9ffc63400LL),reale(368582,0x69a43eb914890LL),
      reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^19, polynomial in n of order 10
      -reale(8942538,0x3b8622ae62a00LL),reale(10481872,0x1e7c948175300LL),
      -reale(5381394,0x830498d800800LL),reale(6645195,0x535f47efddd00LL),
      -reale(4043713,0x9ba9cf138e600LL),reale(3563786,0x6253b3df24700LL),
      -reale(3045580,0xe2f1f7a110400LL),reale(1548984,0x4828fbf665100LL),
      -reale(1694435,0x63dcfc138a200LL),reale(406057,0xe76a74dc3bb00LL),
      reale(110280,0xa64ca1bbeb438LL),reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^18, polynomial in n of order 11
      -reale(18204995,0x3f490d6ed8000LL),reale(15367333,0xa666c37198000LL),
      -reale(8424707,0xb9613a5da8000LL),reale(10765521,3190860555LL<<17),
      -reale(5300295,0xd300940f58000LL),reale(6273886,0xba1b2aa228000LL),
      -reale(4137511,0x6a32b5bc28000LL),reale(2951915,0x3ffeb65fb0000LL),
      -reale(2898950,0x38c8743c58000LL),reale(1027617,0x2c3889c5b8000LL),
      -reale(1062542,0x7c8a4a4828000LL),reale(500325,0x147f19cd83980LL),
      reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^17, polynomial in n of order 12
      -reale(46659673,0x7940546261000LL),reale(20576887,0xb72d09f420c00LL),
      -reale(17371112,0xc460beb873800LL),reale(16552256,0x8d133b2d84400LL),
      -reale(7883306,0x3c181b1016000LL),reale(10867815,0x95ba8c80bfc00LL),
      -reale(5343012,0x31a34980f8800LL),reale(5640245,0x12558783a3400LL),
      -reale(4241979,0x47a64b12cb000LL),reale(2204426,0xf7d60f21fec00LL),
      -reale(2506924,0x6e46ed413d800LL),reale(503732,0xa322eb69a2400LL),
      reale(139663,0x777cb98300b20LL),reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^16, polynomial in n of order 13
      -reale(156865464,0x9b4a437ced000LL),reale(26751997,0x84cabd1d8c000LL),
      -reale(47510066,0xf418e3e50b000LL),reale(22667291,0xeea5410a3a000LL),
      -reale(16175537,0xc4ceea20b9000LL),reale(17818506,0xfb6c54d608000LL),
      -reale(7402653,0x2459922697000LL),reale(10650742,0xeb52d29456000LL),
      -reale(5558253,0xfdda6aad45000LL),reale(4690304,0xc3737ed884000LL),
      -reale(4248624,0xb4bb4dab63000LL),reale(1382140,0xc755b095f2000LL),
      -reale(1646389,0x4c787b5791000LL),reale(701746,0xdc0286e009640LL),
      reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^15, polynomial in n of order 14
      reale(158569992,0x763cf17d39800LL),reale(242045827,0xf358b9d531400LL),
      -reale(171801710,0xfbdaa54751000LL),reale(26564510,0xe59a1e6b54c00LL),
      -reale(47715397,0x8fdbdb93bb800LL),reale(25503418,0x124aa89300400LL),
      -reale(14593564,0x65519680b6000LL),reale(19028249,0x27fd86c303c00LL),
      -reale(7127523,0x40a42052f0800LL),reale(9926805,0x1876eddc2f400LL),
      -reale(5956098,0xfb7e2f3f1b000LL),reale(3422018,0xde3cf0f552c00LL),
      -reale(3909386,0x4ce6da2de5800LL),reale(606166,0xec68c0e73e400LL),
      reale(172919,0x9ad62b665b520LL),reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^14, polynomial in n of order 15
      reale(234628808,0x48818da828000LL),-reale(452308383,0x26baa88038000LL),
      reale(184630907,0xde7b734758000LL),reale(240946965,0x4db221ae90000LL),
      -reale(189474421,0xed4c1e36d8000LL),reale(27214973,0x55324802d8000LL),
      -reale(46882338,0xe5fcdfdca8000LL),reale(29262846,2319362995LL<<17),
      -reale(12682237,0x3cee53d458000LL),reale(19904432,0x70537f02e8000LL),
      -reale(7274198,0xbf917ba828000LL),reale(8480909,0x438c3da230000LL),
      -reale(6415713,0xc95c9b8258000LL),reale(1960896,0x685dc04df8000LL),
      -reale(2745254,0xf883406d28000LL),reale(1023946,0x4eef421f04580LL),
      reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^13, polynomial in n of order 16
      -reale(2272755,0x57fd708a77000LL),-reale(26091168,0x1366cec7d9d00LL),
      reale(231976719,0xafe6927fcde00LL),-reale(464894868,0x24c5c39795700LL),
      reale(215184123,0xaf8273d716c00LL),reale(236438336,0xab29f0bfd4f00LL),
      -reale(210344218,0x367ffa8b78600LL),reale(29454299,0x2f129bee9500LL),
      -reale(44460297,0xf9cfdfb8bb800LL),reale(34058265,0xda8305b9abb00LL),
      -reale(10677799,0x93543d448ea00LL),reale(19950418,0xbb16c712a0100LL),
      -reale(8097327,0xc3857f1ecdc00LL),reale(6164437,0x8a1d8a85ca700LL),
      -reale(6487914,0xa92c56ec54e00LL),reale(653539,0x4a58f163aed00LL),
      reale(193289,0xc4fa7fb371708LL),reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^12, polynomial in n of order 17
      -reale(136365,0x73a1fcfe6ac00LL),-reale(450638,0xd074750f34000LL),
      -reale(2128024,0x54e7feac4d400LL),-reale(24952088,0x92a9c1fc91800LL),
      reale(228113259,0x85d44607e4400LL),-reale(477191195,0x7e69e50f07000LL),
      reale(251096618,0x1896eb4cd1c00LL),reale(226763725,0xac7cda7d93800LL),
      -reale(234776156,0x14cc4b0edcc00LL),reale(34557325,0x4230b4bd66000LL),
      -reale(39741101,0x3a85821c7f400LL),reale(39764072,0x42dd69fc98800LL),
      -reale(9161206,0x9c1a792d6dc00LL),reale(18380268,0xf302f56753000LL),
      -reale(9708385,0x581708d300400LL),reale(3148914,0x8380fab1bd800LL),
      -reale(5050904,0x8a565e3e8ec00LL),reale(1566765,0x6fd98617e9df0LL),
      reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^11, polynomial in n of order 18
      -reale(18810,0x4977f6cdda600LL),-reale(44617,0xf507aa2256700LL),
      -reale(121680,0x26c8d0378b000LL),-reale(408670,0xadcc6d8f87900LL),
      -reale(1967116,0xd731d207dba00LL),-reale(23614778,0x5c1a1fadbeb00LL),
      reale(222693980,0x695506ba87c00LL),-reale(488598159,0xe2ab67bc47d00LL),
      reale(293333811,0x10f016a3f3200LL),reale(209273530,0x4db1c2b811100LL),
      -reale(262769616,0x9b49f60945800LL),reale(44647130,0x3acb33bfff00LL),
      -reale(31983858,0x227f1389ce200LL),reale(45626356,0x9e16c6ccb8d00LL),
      -reale(9276161,0xf8fb16a652c00LL),reale(14205372,0x289c377eefb00LL),
      -reale(11490116,0xc948e407f600LL),reale(414830,0x163387d5d8900LL),
      reale(117690,0xc756ec17c4aa8LL),reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^10, polynomial in n of order 19
      -reale(3667,0x8ba48fb7ec000LL),-reale(7355,0xde5d961edc000LL),
      -reale(15963,0x138d280434000LL),-reale(38393,53315683LL<<17),
      -reale(106358,0x1cca460dcc000LL),-reale(363723,0x77fed5aee4000LL),
      -reale(1788619,0xb46088e414000LL),-reale(22045766,0x7d53064fc8000LL),
      reale(215267089,0x7c4e47994000LL),-reale(498143540,0xc077eb386c000LL),
      reale(342855614,0x4b25e0bbcc000LL),reale(179961617,0x7ca6ea4dd0000LL),
      -reale(293329289,0xb4e43f9ccc000LL),reale(63137066,0xbcee02f98c000LL),
      -reale(20920174,0xdceb909f94000LL),reale(49479848,0x7088e98168000LL),
      -reale(12768344,0x1ee1d8cbec000LL),reale(6948560,0xd8f6969c04000LL),
      -reale(10643749,0x466c677134000LL),reale(2529930,0x161dcdf222440LL),
      reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^9, polynomial in n of order 20
      -real(0x354d49acec3dd800LL),-real(0x606a7d34c50a0200LL),
      -reale(2939,0xdc47a7c209c00LL),-reale(5971,0x671f2d9dad600LL),
      -reale(13140,0xcdf9f327fe000LL),-reale(32101,0x6baea5bb9ea00LL),
      -reale(90511,0x408ba9a232400LL),-reale(315893,0xc97e5e852be00LL),
      -reale(1591343,0xfce30d8d1e800LL),-reale(20207205,0x8b4272e60d200LL),
      reale(205238828,0x21c1cf60c5400LL),-reale(504251582,0xb2b181bcfa600LL),
      reale(400330413,0xa384192d01000LL),reale(132810886,0x4094526254600LL),
      -reale(323039224,0xd5680dd0e3400LL),reale(95085342,0xbfbbc74d27200LL),
      -reale(8279837,0x6ce790195f800LL),reale(46514941,0x8e0e73ffc5e00LL),
      -reale(20732718,0x38ef4b2eebc00LL),-reale(922541,0xf2a1d94487600LL),
      -reale(491669,0x5bd07d195db30LL),reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^8, polynomial in n of order 21
      -real(0xd828cefda55a800LL),-real(0x16c6eac98e7b6000LL),
      -real(0x27e1e798049c9800LL),-real(0x490330552dbbf000LL),
      -reale(2255,0x88ea2b8740800LL),-reale(4647,0x88c66c31f8000LL),
      -reale(10390,0xd13f35560f800LL),-reale(25836,0xfcd55e2db1000LL),
      -reale(74324,0xc0bfff0e86800LL),-reale(265480,0xf5ce67923a000LL),
      -reale(1374647,0xa0b10ca8f5800LL),-reale(18058373,0x723761b2e3000LL),
      reale(191831943,0xc85920c253800LL),-reale(504361484,0x6e935002fc000LL),
      reale(465423127,0xbaa71ebb04800LL),reale(59036306,0xf120275a2b000LL),
      -reale(342905949,0x5a93131732800LL),reale(146354899,0x9f9c2b8142000LL),
      -reale(1641748,0x1e8ba62ca1800LL),reale(28969072,0x51c8dabef9000LL),
      -reale(27136540,0x3d9359d98800LL),reale(4249105,0xd55e5a0325120LL),
      reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^7, polynomial in n of order 22
      -real(0x38123cee860f400LL),-real(0x59d375c04e8be00LL),
      -real(0x942bf86bd4c1800LL),-real(0xfcbda8858afb200LL),
      -real(0x1c02af2dc3443c00LL),-real(0x33fc822f8d2b6600LL),
      -real(0x65e35fc07de4e000LL),-reale(3414,0xc7eb297eb5a00LL),
      -reale(7775,0x1c0e884298400LL),-reale(19731,0x6a31912ef0e00LL),
      -reale(58089,0x9471e600da800LL),-reale(213111,0x15a6331c60200LL),
      -reale(1139019,0x77ee6ce2ccc00LL),-reale(15560104,0x33d66a0afb600LL),
      reale(174045800,0x2f0a20e9d9000LL),-reale(494300177,0xd9e4761bbaa00LL),
      reale(535087920,0xe9f8f195ec00LL),-reale(53102016,0x93f6bbbe95e00LL),
      -reale(331738553,0x77bff637f3800LL),reale(216985631,0x987f3afb7ae00LL),
      -reale(21074121,0x8043eaffd5c00LL),-reale(4185955,0xa3ff769180600LL),
      -reale(4713710,0xd2e19a34f30b0LL),reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^6, polynomial in n of order 23
      -real(0xe0ca252d14c000LL),-real(0x15a70af15f24000LL),
      -real(0x222b3f817554000LL),-real(0x375f97b48cd8000LL),
      -real(0x5c7b9631f8ac000LL),-real(0x9fe2527c7fcc000LL),
      -real(0x11face3d5ef34000LL),-real(0x21e77d8dabde0000LL),
      -real(0x439dcbf7fdccc000LL),-reale(2310,0x1731d0ccf4000LL),
      -reale(5373,0x35ee2c1554000LL),-reale(13965,0xf39edc32e8000LL),
      -reale(42247,0xa0aa0b1cac000LL),-reale(159930,0xa2319a759c000LL),
      -reale(887131,0xc123fa86b4000LL),-reale(12685735,0x6243721af0000LL),
      reale(150650948,0x968da6a8b4000LL),-reale(467294064,0x1610ada8c4000LL),
      reale(599544322,0x5feb9b1dac000LL),-reale(214883240,0x150075a4f8000LL),
      -reale(244806233,0x53bd4b2bac000LL),reale(272520146,0x88b0e96a94000LL),
      -reale(87760725,0x27ae1fc734000LL),reale(5827860,0xa7a2901c3a740LL),
      reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^5, polynomial in n of order 24
      -real(0x32b69e04189800LL),-real(0x4bd39320660300LL),
      -real(0x73a508e7ef1600LL),-real(0xb44a7ec206b900LL),
      -real(0x1200d9d52c6d400LL),-real(0x1d916a5ad4bcf00LL),
      -real(0x321a3f994641200LL),-real(0x57fce6d660f8500LL),
      -real(0xa10c564a22b1000LL),-real(0x1356fa3ebba41b00LL),
      -real(0x275fd13435900e00LL),-real(0x5604e2d76283d100LL),
      -reale(3283,0xdf8f52c874c00LL),-reale(8783,0x8ddc09700e700LL),
      -reale(27451,0x143e179f50a00LL),-reale(107903,0xe48c7d6f59d00LL),
      -reale(625732,0xe2abef41d8800LL),-reale(9446536,0xacc19c0743300LL),
      reale(120325828,0x5507fb0eafa00LL),-reale(412649247,0xc3fe82376e900LL),
      reale(633089704,0xd19d26ed03c00LL),-reale(418090362,0x84d33548fff00LL),
      -reale(13712613,0x4e3334f720200LL),reale(163180098,0x55c7c31664b00LL),
      -reale(61921019,0x751f3b2bed108LL),
      reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[3], coeff of eps^4, polynomial in n of order 25
      -real(0x30fab48eb2c00LL),-real(0x4779db0cde000LL),
      -real(0x6a1a5308c1400LL),-real(0xa07c7893bf800LL),
      -real(0xf7d15b087bc00LL),-real(0x1878e181999000LL),
      -real(0x27ab652bf7a400LL),-real(0x422ed0b6682800LL),
      -real(0x721448fff54c00LL),-real(0xcc1e5699294000LL),
      -real(0x17d5829db9a3400LL),-real(0x2ed74923dde5800LL),
      -real(0x61c84aba5ffdc00LL),-real(0xdbaa1b53c88f000LL),
      -real(0x21cc8beefe3fc400LL),-real(0x5da8efb832aa8800LL),
      -reale(4876,0x5d83861736c00LL),-reale(20082,0x8bb9af0c4a000LL),
      -reale(123005,0x97d1502b45400LL),-reale(1983151,0x65e045fd8b800LL),
      reale(27425226,0x9c6669ee40400LL),-reale(105081920,0xe8c662ae85000LL),
      reale(191976586,0x46cce583c1c00LL),-reale(186491540,0xf45203874e800LL),
      reale(93245770,0x7a2901c3a7400LL),-reale(18940547,0x20d0545bbdf90LL),
      reale(9570895220LL,0xb53783566b8edLL),
      // C4[3], coeff of eps^3, polynomial in n of order 26
      -real(0x10330cb256200LL),-real(0x172cb16211100LL),
      -real(0x21a8187537800LL),-real(0x31b06260f1f00LL),
      -real(0x4ab014ab28e00LL),-real(0x7280309c9cd00LL),
      -real(0xb366eef7be400LL),-real(0x11ff8a58b05b00LL),
      -real(0x1dae666558ba00LL),-real(0x327547ac4a0900LL),
      -real(0x58c9207d125000LL),-real(0xa2826b77361700LL),
      -real(0x137557a5841e600LL),-real(0x275355b4b1bc500LL),
      -real(0x54b37d85300bc00LL),-real(0xc517d06239a5300LL),
      -real(0x1f8f2f623d981200LL),-real(0x5b85a3034c390100LL),
      -reale(5020,0xa2ee6bc312800LL),-reale(21965,0x48d3177570f00LL),
      -reale(144343,0x4c469a2853e00LL),-reale(2526007,0xb6d389c1bbd00LL),
      reale(38395317,0x415c2de726c00LL),-reale(163180098,0x55c7c31664b00LL),
      reale(326360196,0xab8f862cc9600LL),-reale(303048754,0xd0545bbdf900LL),
      reale(104173009,0x3479cff894d98LL),
      reale(28712685662LL,0x1fa68a0342ac7LL),
      // C4[4], coeff of eps^29, polynomial in n of order 0
      4519424,real(0x13ed3512585LL),
      // C4[4], coeff of eps^28, polynomial in n of order 1
      real(322327509504LL),real(86419033792LL),real(0x12e7203d54087bdLL),
      // C4[4], coeff of eps^27, polynomial in n of order 2
      real(0xdf868e997000LL),-real(0xc54488fde800LL),real(0x67996a8dfb80LL),
      reale(6219,0x86ed0fee71e5LL),
      // C4[4], coeff of eps^26, polynomial in n of order 3
      real(0x1e30d5f17398800LL),-real(0x20335f44c005000LL),
      real(0x8656a9da59d800LL),real(0x246f3281df3200LL),
      reale(1871928,0xea4bbbb5bea41LL),
      // C4[4], coeff of eps^25, polynomial in n of order 4
      real(0x640278dc982000LL),-real(0x64de2b5e388800LL),
      real(0x266cf1cb211000LL),-real(0x24af02897bd800LL),
      real(0x125236c4932c80LL),reale(225070,0xa1cd0c0f186c5LL),
      // C4[4], coeff of eps^24, polynomial in n of order 5
      real(0x183393315f62f400LL),-real(0x147c8a635ba4f000LL),
      real(0xaadb07a361e2c00LL),-real(0xbd0a07cdca37800LL),
      real(0x2c490db64a86400LL),real(0xc3000bbe3e2580LL),
      reale(8327613,0x62a2be2e87a79LL),
      // C4[4], coeff of eps^23, polynomial in n of order 6
      reale(7399,0xe4703b1ceb000LL),-reale(4925,0x718bf750ef800LL),
      reale(3656,0xc01290e152000LL),-reale(3594,0x9ae0aefbbc800LL),
      real(0x5080258211e79000LL),-real(0x5458466826cf9800LL),
      real(0x27a09e95cf36b080LL),reale(97921247,0xc3bd6c206251LL),
      // C4[4], coeff of eps^22, polynomial in n of order 7
      reale(4319137,0xe5044c1364800LL),-reale(2259378,0xc043aee633000LL),
      reale(2431286,0xcceb783bf5800LL),-reale(1865690,0x884902c9a2000LL),
      reale(996566,0x94ae3b7946800LL),-reale(1135368,0x2cb1c30811000LL),
      reale(231629,0x92b25177d7800LL),reale(64961,0x89605803fda00LL),
      reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^21, polynomial in n of order 8
      reale(6174501,0x53f34a829c000LL),-reale(2885765,0xddf01a0f35800LL),
      reale(4089976,0x588848e445000LL),-reale(2309244,0x73683320c8800LL),
      reale(1950621,0xac1b944ace000LL),-reale(1810054,0xa24c07eb4b800LL),
      reale(609590,0x74daa18497000LL),-reale(712107,0x16cff78e5e800LL),
      reale(310317,0x16957f6a36b80LL),reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^20, polynomial in n of order 9
      reale(7763095,0xd98a0c3214600LL),-reale(4551997,0xf65d38a54d000LL),
      reale(6348004,0x7dcc619ba1a00LL),-reale(2777846,0x11091dc381c00LL),
      reale(3645151,0x5af876afd6e00LL),-reale(2403756,0x12692c3266800LL),
      reale(1377366,0xde24866584200LL),-reale(1585712,0xf2192bea6b400LL),
      reale(268682,0xb0f056b079600LL),reale(77255,0xca5a822ebf740LL),
      reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^19, polynomial in n of order 10
      reale(8073134,0x8bff962f2e000LL),-reale(9331256,0xe8e10405e1000LL),
      reale(8608510,0x42ad0321d8000LL),-reale(3959617,0x4c778c1e2f000LL),
      reale(6283090,0x55033b3d82000LL),-reale(2832307,0xbbdb17809d000LL),
      reale(2955095,0x929c8347ec000LL),-reale(2459067,0xd43d49c36b000LL),
      reale(787004,0x9cc4866d6000LL),-reale(1039103,0x6b1983acd9000LL),
      reale(412222,0xf695367aa1b00LL),reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^18, polynomial in n of order 11
      reale(8586281,0xffd2991fd000LL),-reale(20926106,0xdd733d721a000LL),
      reale(9282973,0x193483c94f000LL),-reale(8121077,0x9b55004148000LL),
      reale(9430655,0x90c0e29221000LL),-reale(3512067,0x80c2ac76000LL),
      reale(5840995,0x1886eb4173000LL),-reale(3061324,0xab1a78b4a4000LL),
      reale(2049544,0x4067911445000LL),-reale(2292525,0x617c054ad2000LL),
      reale(297833,0x966e637f97000LL),reale(88539,0x9a2e50b8c6400LL),
      reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^17, polynomial in n of order 12
      reale(32196457,0xd679f8ae1c000LL),-reale(40594018,0x37167c5ef5000LL),
      reale(8052650,0x2eda271162000LL),-reale(20325613,0xcd34eeff17000LL),
      reale(11030346,0x5827875768000LL),-reale(6662972,0x9685f0fc59000LL),
      reale(10015916,0xfa65faac6e000LL),-reale(3377057,0x1ef6021e7b000LL),
      reale(4892320,0x94cb79bcb4000LL),-reale(3369439,0x93437f1d3d000LL),
      reale(1068721,0xdee482d47a000LL),-reale(1596884,0xcb3e26805f000LL),
      reale(562334,0xcf5270735f500LL),reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^16, polynomial in n of order 13
      reale(239019678,0x7928c61a8b800LL),-reale(41200119,0x147c0b11e000LL),
      reale(27063572,0xac3757be98800LL),-reale(45155983,0xc412cf1f79000LL),
      reale(8354845,0xf8b6ea7445800LL),-reale(18750027,0x4e7377c014000LL),
      reale(13292220,0xfed958edd2800LL),-reale(5165101,0x26aa3105af000LL),
      reale(10025000,0x43fec217f800LL),-reale(3715677,0xed5a4430a000LL),
      reale(3405288,0xc16fe1018c800LL),-reale(3440521,0x6cb0e4f2e5000LL),
      reale(291108,0x30be23439800LL),reale(90314,0xe93f4121c6900LL),
      reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^15, polynomial in n of order 14
      -reale(301344600,0x1f7a69f35a000LL),-reale(137666269,0x81776c9d9b000LL),
      reale(257500426,0xa27a71193c000LL),-reale(52745704,0xa8e59f44d000LL),
      reale(20527629,0x3707e00852000LL),-reale(49389175,0x1679a6a55f000LL),
      reale(10057417,0xa546ce8428000LL),-reale(15960633,0x79a78f6a91000LL),
      reale(15828795,0x3b7a7e96fe000LL),-reale(4041479,0x5385608da3000LL),
      reale(9015452,0x8a056dcb14000LL),-reale(4531739,0xb18fd7c855000LL),
      reale(1608583,0x5c81da4aaa000LL),-reale(2620079,0xb9c03a2467000LL),
      reale(790676,0xf12036cb88d00LL),reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^14, polynomial in n of order 15
      -reale(152316078,0x9ee9710b1f000LL),reale(396132268,0xf6300698d2000LL),
      -reale(331944543,0x2a26efc8bd000LL),-reale(111967823,0x409ccb544c000LL),
      reale(276102802,0x8592b62d25000LL),-reale(69409637,0x2e4659b6a000LL),
      reale(12806364,0xaa4a38387000LL),-reale(52382533,0xaa3aad6588000LL),
      reale(13858261,0x7d9fda6f69000LL),-reale(11925525,0x17f68feba6000LL),
      reale(17994828,0x2633a57dcb000LL),-reale(3926621,0x9c334da6c4000LL),
      reale(6610729,0xa84ec063ad000LL),-reale(5341800,0xcfe0c57fe2000LL),
      reale(171304,0xc92dc0ce0f000LL),reale(53498,0x8a12fdd94c400LL),
      reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^13, polynomial in n of order 16
      reale(945329,0x3e694a5630000LL),reale(13046260,0xd11553dc81000LL),
      -reale(145063327,0x6c5bbd04f6000LL),reale(395288944,0x9758cc3483000LL),
      -reale(364989750,0x4da45c465c000LL),-reale(77659847,0x7f601a5fdb000LL),
      reale(293261136,0xdb46a6c9be000LL),-reale(92956699,0x68d702f4d9000LL),
      reale(4748491,0xd717292318000LL),-reale(52641236,0xde7217eeb7000LL),
      reale(20401071,0xa831b35d72000LL),-reale(7165143,0xe2daef21b5000LL),
      reale(18530179,0x70f1fa908c000LL),-reale(5449998,0x995f61f213000LL),
      reale(2985284,0xf423c13426000LL),-reale(4674955,0x4c99b17411000LL),
      reale(1148405,0xaa811667d8300LL),reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^12, polynomial in n of order 17
      reale(39064,0xc457745427a00LL),reale(149707,0xe179ab818a000LL),
      reale(834482,0xb3de3faf4c600LL),reale(11844090,0x43801d34c0c00LL),
      -reale(136492367,0x606ac4f4b6e00LL),reale(391413380,0x8b1b355567800LL),
      -reale(399991879,0xf56c51d232200LL),-reale(32313943,0x670cb1cd91c00LL),
      reale(306137820,0x47c0d4df8aa00LL),-reale(125355715,0x12c37db13b000LL),
      -reale(1549012,0x61de67b1d0a00LL),-reale(48002827,0x1ef791fca4400LL),
      reale(29707099,0x80264b6e6c200LL),-reale(3304868,0xd90dacdedd800LL),
      reale(15595740,0x1c41b85df0e00LL),-reale(8339676,0x731c5b6cf6c00LL),
      -reale(264319,0x3253133a92600LL),-reale(128183,0x1fd72f4c70540LL),
      reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^11, polynomial in n of order 18
      reale(3796,0xb8b80a685d000LL),reale(10243,0xe5415b1644800LL),
      reale(32134,0x75fe9c2f28000LL),reale(125896,0x13cc0b67cb800LL),
      reale(720062,0x2eb5ef2cf3000LL),reale(10542664,0x8e7784ebe2800LL),
      -reale(126401502,0xa942d02d22000LL),reale(383396973,0xa914c081a9800LL),
      -reale(435856143,0x9e18e4ddf7000LL),reale(26921352,0xa17bcee040800LL),
      reale(309790567,0x432113bb94000LL),-reale(168177156,0xf5a6b5d938800LL),
      -reale(1732899,0x7848d10f61000LL),-reale(36033193,0x6ff05a93a1800LL),
      reale(39850986,0x4a7ce5d24a000LL),-reale(3520516,0x12d4d9afda800LL),
      reale(7904559,0x47211641b5000LL),-reale(9293198,0x11e52b76c3800LL),
      reale(1712350,0xd1c47193d5a80LL),reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^10, polynomial in n of order 19
      real(0x20b0c3dbe662b800LL),real(0x49a4ee6b654d5000LL),
      reale(2895,0xbb9a481b3e800LL),reale(7963,0xd6290c9168000LL),
      reale(25525,0x742091bd91800LL),reale(102493,0xec03f49fb000LL),
      reale(603292,0x6fe940faa4800LL),reale(9144553,0x3f081030e000LL),
      -reale(114581171,0x9502f66408800LL),reale(369767644,0x159b783921000LL),
      -reale(470438620,0x42537ac0f5800LL),reale(102998223,0x33db2118b4000LL),
      reale(295924658,0xfd504b0d5d800LL),-reale(220875824,0xd68590c9b9000LL),
      reale(12088406,0x3b87c77470800LL),-reale(15966308,0xf7cc70b9a6000LL),
      reale(44660638,0xbb68d3ddc3800LL),-reale(11155854,0x316b572a93000LL),
      -reale(1400757,0x91d7719929800LL),-reale(909990,0x5b4dcbdcd9200LL),
      reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^9, polynomial in n of order 20
      real(0x55091490e3fe000LL),real(0xab3101736f26800LL),
      real(0x16d77945c4e3b000LL),real(0x345d2a91137d7800LL),
      reale(2099,0xc55d2c398000LL),reale(5898,0x424192198800LL),
      reale(19366,0xa6f5f449f5000LL),reale(79943,0x847cdfac49800LL),
      reale(486014,0x6a1dc16732000LL),reale(7659629,0x94cc8fca800LL),
      -reale(100839015,0x651046eed1000LL),reale(348607247,0x22ddc22bfb800LL),
      -reale(499815073,0x4df2756234000LL),reale(197958555,0x77a0b2f8bc800LL),
      reale(251323198,0x2663cfb2e9000LL),-reale(276534810,0xe292670a12800LL),
      reale(51555588,0x6a67a23666000LL),reale(5587968,0x5e92831b6e800LL),
      reale(32523682,0xed2ae23e23000LL),-reale(21111776,0x46401336e0800LL),
      reale(2489921,0xe3c1e337a6d80LL),reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^8, polynomial in n of order 21
      real(0xeb8379f6b27c00LL),real(0x1b6c4de1f1d7000LL),
      real(0x355a1dadc956400LL),real(0x6d308de46411800LL),
      real(0xed54313f63d4c00LL),real(0x22ae87428a2ac000LL),
      real(0x58ce5dd980bc3400LL),reale(4090,0xd3c824bc46800LL),
      reale(13806,0x44b4a8a441c00LL),reale(58809,0x7ab991df81000LL),
      reale(370898,0xe410033e70400LL),reale(6109620,0x6402b9f6fb800LL),
      -reale(85053139,0x4bf446ca91400LL),reale(317515928,0x1b63894556000LL),
      -reale(517123103,0xa7a388b5a2c00LL),reale(310296682,0xe98bc80130800LL),
      reale(156996715,0xaa3cf3c05bc00LL),-reale(312601560,0xdd28200ed5000LL),
      reale(125126811,0xf01e02788a400LL),reale(4091818,0xb5091207e5800LL),
      -reale(866059,0xc9a79cf1f7400LL),-reale(4943757,0xf4721fe538b80LL),
      reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^7, polynomial in n of order 22
      real(0x2814d49c0c5000LL),real(0x468b0d3a3db800LL),
      real(0x80724d98876000LL),real(0xf31dbc49b20800LL),
      real(0x1e12cb4a6a67000LL),real(0x3eb5a58b5455800LL),
      real(0x8b1eef20fbf8000LL),real(0x14cb29a266eda800LL),
      real(0x36974c82ca289000LL),reale(2585,0xefae20720f800LL),
      reale(9007,0x1d6baf437a000LL),reale(39779,0x24ec74fd54800LL),
      reale(261696,0x442f64f42b000LL),reale(4534975,0xa5b17f809800LL),
      -reale(67279179,0x4d9bf05604000LL),reale(273758534,0xd27122c18e800LL),
      -reale(510920394,0x40d515b3000LL),reale(428723861,0x53ee2b6143800LL),
      -reale(7330129,0x37be948582000LL),-reale(275708250,0xae16364977800LL),
      reale(204390109,0xe684af0fef000LL),-reale(52540960,0x7463315742800LL),
      reale(2056891,0xfeee14beab380LL),reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^6, polynomial in n of order 23
      real(0x628e4f4bb7800LL),real(0xa60e374943000LL),real(0x11fae77940e800LL),
      real(0x2022ddc061a000LL),real(0x3b7f2e2d7a5800LL),
      real(0x72aa26ca9f1000LL),real(0xe77392a11fc800LL),
      real(0x1ed1e51d0348000LL),real(0x460248a5fa93800LL),
      real(0xabd9e84dc89f000LL),real(0x1d078c2cd5cea800LL),
      real(0x58c9fda5cf076000LL),reale(5134,0xa77137081800LL),
      reale(23653,0x63d76094d000LL),reale(163469,0x772f4630d8800LL),
      reale(3004667,0x8d384291a4000LL),-reale(47956830,0xd53f134a90800LL),
      reale(214953528,0xfe0a5a4ffb000LL),-reale(463620631,0xbff95a7639800LL),
      reale(519033396,0x411553aad2000LL),-reale(237300381,0xd565fafaa2800LL),
      -reale(84296486,0x10fabff57000LL),reale(142611178,0x607af3a3b4800LL),
      -reale(46622885,0x3d1480e1d3a00LL),reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^5, polynomial in n of order 24
      real(0xc0b5b2cac000LL),real(0x139ac5d2ed800LL),real(0x20abe97223000LL),
      real(0x37e2f8cba0800LL),real(0x6269b1d1ba000LL),real(0xb3074a8a43800LL),
      real(0x151de1e3911000LL),real(0x298e5ccaa76800LL),
      real(0x55d208375c8000LL),real(0xbb7ea958fd9800LL),
      real(0x1b5e1854857f000LL),real(0x4547c4b8360c800LL),
      real(0xc1cdc899e5d6000LL),real(0x2682d6f5e00af800LL),
      reale(2326,0xf44888e46d000LL),reale(11275,0x7d4afe8b62800LL),
      reale(82638,0x859516eee4000LL),reale(1628359,0xc1653179c5800LL),
      -reale(28286265,0xc31f9b1d25000LL),reale(141205400,0x2bb5164778800LL),
      -reale(353352393,0x632221a20e000LL),reale(504046796,0x730ece181b800LL),
      -reale(416863444,0x7c7b16f237000LL),reale(186491540,0xf45203874e800LL),
      -reale(34967163,0xedcf60a95eb80LL),reale(36916310137LL,0x41f43bb0c949LL),
      // C4[4], coeff of eps^4, polynomial in n of order 25
      real(0xe07098dae00LL),real(0x16338b625000LL),real(0x23dda179f200LL),
      real(0x3b41a69cf400LL),real(0x645a89a6b600LL),real(0xaeabe0e09800LL),
      real(0x1397028dcfa00LL),real(0x246014e923c00LL),real(0x4633de275be00LL),
      real(0x8d95c8a56e000LL),real(0x12c670f9ba0200LL),
      real(0x2a433484738400LL),real(0x6608a70542c600LL),
      real(0x10c10ac322d2800LL),real(0x30ddb4b92590a00LL),
      real(0xa2e30513d28cc00LL),real(0x289386109855ce00LL),
      reale(3347,0x17499d2cb7000LL),reale(26358,0x5763b5c021200LL),
      reale(564821,0x99c65b39a1400LL),-reale(10825747,0x58af29d092a00LL),
      reale(60624185,0x23d4ea299b800LL),-reale(172778927,0xa61ece902e600LL),
      reale(279737311,0x6e7b054af5c00LL),-reale(233114426,0x3166846922200LL),
      reale(75762188,0x8341516ef7e40LL),reale(36916310137LL,0x41f43bb0c949LL),
      // C4[5], coeff of eps^29, polynomial in n of order 0
      3108352,real(0x4338129a0b3LL),
      // C4[5], coeff of eps^28, polynomial in n of order 1
      -real(4961047LL<<17),real(304969986048LL),real(0x171a7cbcbc0a5e7LL),
      // C4[5], coeff of eps^27, polynomial in n of order 2
      -real(0xb7a8cf8589000LL),real(0x25cdf8a9f5800LL),real(0xaa8ee05df480LL),
      reale(53207,0x4825dfa147919LL),
      // C4[5], coeff of eps^26, polynomial in n of order 3
      -real(0x4519d2e6066000LL),real(0x17b1d503134000LL),
      -real(0x1b53dc2d3c2000LL),real(0xc104a529c3b00LL),
      reale(207992,0x1a086a30a3679LL),
      // C4[5], coeff of eps^25, polynomial in n of order 4
      -real(0xe48436400f9e000LL),real(0x825cbe3b5113800LL),
      -real(0x9657faac8f9f000LL),real(0x1ac735d19d16800LL),
      real(0x7b639e59c13780LL),reale(8527676,0x2b5901ca2b961LL),
      // C4[5], coeff of eps^24, polynomial in n of order 5
      -real(0x13b86e0d5c5dc000LL),real(0x135f9b0385fb0000LL),
      -real(0x10df1064c3304000LL),real(0x58b0ae17a818000LL),
      -real(0x70d05036b8ec000LL),real(0x2e5299a0b610e00LL),
      reale(10178194,0x2338af8e3405bLL),
      // C4[5], coeff of eps^23, polynomial in n of order 6
      -reale(126383,0x5f6b81564f000LL),reale(192332,0x2215a4d90d800LL),
      -reale(113392,0x893928fcaa000LL),reale(71665,0x3fb557978e800LL),
      -reale(81791,0xa6f9503f45000LL),reale(12036,0x1a6fad5adf800LL),
      reale(3561,0x9aef6f2cefa80LL),reale(3470764200LL,0xea81d86b4b937LL),
      // C4[5], coeff of eps^22, polynomial in n of order 7
      -reale(191647,0x188f775ada000LL),reale(308186,0x45ee8f2434000LL),
      -reale(124928,0xd21a49314e000LL),reale(153616,0xaed0e35eb8000LL),
      -reale(118466,0xc4b6a2a9a2000LL),reale(38029,0x77ad4b77bc000LL),
      -reale(53612,0x41f60b8316000LL),reale(20169,0xecfa5f7fa8900LL),
      reale(3470764200LL,0xea81d86b4b937LL),
      // C4[5], coeff of eps^21, polynomial in n of order 8
      -reale(5169843,0xc81db86efc000LL),reale(5341939,0xe957aa505800LL),
      -reale(2049228,0x2e9753666d000LL),reale(3734678,0xdcd2e44998800LL),
      -reale(1762099,0xebebc251fe000LL),reale(1337844,0xa441c7cbb800LL),
      -reale(1455577,0x7e18adc04f000LL),reale(163809,0xd9aab3cbce800LL),
      reale(50215,0x8f7a6f7ead780LL),reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^20, polynomial in n of order 9
      -reale(11201228,0x9af12fea90000LL),reale(5330620,7096189457LL<<19),
      -reale(4084126,0xa473ecba70000LL),reale(5776338,0xc1238f4360000LL),
      -reale(1850318,0x7e36514750000LL),reale(3091001,2788978033LL<<18),
      -reale(1978996,0x9854b5b30000LL),reale(651396,0xde4e2e0920000LL),
      -reale(1009381,0x5e1878c010000LL),reale(341219,0x67868049b6800LL),
      reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^19, polynomial in n of order 10
      -reale(19364139,0xf3aad6c27e000LL),reale(3661269,0x231a8ee911000LL),
      -reale(10171658,0x9bc1444518000LL),reale(6650152,0x1449aa44ff000LL),
      -reale(2982446,0xb2f133d6b2000LL),reale(5796709,0x225c7b8fcd000LL),
      -reale(2004712,0xb33d0f538c000LL),reale(2087887,0x2718a4e53b000LL),
      -reale(2041244,0xb9c4a8d7e6000LL),reale(150337,0x64e8ec0109000LL),
      reale(48205,0x4eea8f2f13300LL),reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^18, polynomial in n of order 11
      -reale(17821498,0x43ce2fe394000LL),reale(8113989,0x34042cf6f8000LL),
      -reale(21055211,0x1d823792dc000LL),reale(4458324,0xaba1762760000LL),
      -reale(8384573,0x54084121e4000LL),reale(8079221,0xcbb99849c8000LL),
      -reale(2172398,0x503335ed2c000LL),reale(5129813,0x3b8a4c21b0000LL),
      -reale(2481567,0xadec795134000LL),reale(934125,9279934035LL<<15),
      -reale(1531704,0x9cc504aa7c000LL),reale(453383,0xd34e451346a00LL),
      reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^17, polynomial in n of order 12
      reale(4095301,0x789aeb9e64000LL),reale(49542396,0x46ab457e8d000LL),
      -reale(24303219,0x1ccf0dd62000LL),reale(4679495,0x21a30e03df000LL),
      -reale(21666597,0xecbbb1868000LL),reale(6429258,0x6611bb6911000LL),
      -reale(5963806,0x7f45fe6c6e000LL),reale(9141324,0xab5773fc63000LL),
      -reale(2043796,0x5ca6f33334000LL),reale(3626747,0xd85dd12c15000LL),
      -reale(2919955,0xba0fdf867a000LL),reale(85758,0x333e03c667000LL),
      reale(28339,0x9119c9ad54d00LL),reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^16, polynomial in n of order 13
      -reale(273240474,0x43c43c74c8000LL),reale(133674826,0x952bfc30e0000LL),
      reale(7048142,0x68e4684408000LL),reale(44883009,0xdb6a70b90000LL),
      -reale(32370151,0x153b9e91a8000LL),reale(2006331,0xa0ac245340000LL),
      -reale(20459012,0x9d1a27ed8000LL),reale(9634139,0x6e1e5ebef0000LL),
      -reale(3415127,0x8d101d0c88000LL),reale(9090639,8214448173LL<<17),
      -reale(2849328,0xea461fc3b8000LL),reale(1554483,7516134885LL<<16),
      -reale(2460922,0x6540542d68000LL),reale(615586,0x6f27f96118400LL),
      reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^15, polynomial in n of order 14
      reale(385255297,0xc522d651da000LL),-reale(58599463,0x810289e63d000LL),
      -reale(271784816,0x96bdc01bbc000LL),reale(164665597,0xfc4f4e3665000LL),
      reale(6169937,0xa7ea1cfd2e000LL),reale(36278794,0xf1d4bf77a7000LL),
      -reale(41327996,0x5935502f28000LL),reale(1406713,0xae66a659c9000LL),
      -reale(16753028,0x6b0d0fac7e000LL),reale(13550589,0x7d5a3390b000LL),
      -reale(1765295,0x851b6e8694000LL),reale(7142364,0xca525091ad000LL),
      -reale(4183412,0x818c59892a000LL),-reale(96164,0xa4307ac011000LL),
      -reale(44020,0x281c2d0515b00LL),reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^14, polynomial in n of order 15
      reale(85300002,0xc7e70a9f1c000LL),-reale(294351273,0xafb8edef98000LL),
      reale(403760509,0xda2cbc2e94000LL),-reale(107444454,0x9ae8f34870000LL),
      -reale(261509454,0x4bda846b4000LL),reale(200593259,0xcaf344c1b8000LL),
      -reale(1492598,0x1c0b3e713c000LL),reale(23203659,0x98196f9e60000LL),
      -reale(49434335,0xf8209c0184000LL),reale(4620325,0x4eb0e8bd08000LL),
      -reale(10475101,0x343acca80c000LL),reale(16597245,8542632147LL<<16),
      -reale(2356576,0x3bbee61554000LL),reale(3249396,0x1edbdd7e58000LL),
      -reale(4240477,0x930e83f9dc000LL),reale(851256,0x2b979a0197a00LL),
      reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^13, polynomial in n of order 16
      -reale(334885,0xc6bdc7fcb0000LL),-reale(5563880,0xa3a405a9f1000LL),
      reale(77196254,0x955c2ca786000LL),-reale(280592470,0x60fd2cd013000LL),
      reale(419465490,0x135ebd637c000LL),-reale(164134806,0xd03e535795000LL),
      -reale(238238642,0xf95f61c30e000LL),reale(239782224,0x6d53e5d49000LL),
      -reale(20068072,0x4afa414658000LL),reale(6399560,0x53e56b4c47000LL),
      -reale(53380994,0xb54d3160a2000LL),reale(13179100,0x7f23319325000LL),
      -reale(3190623,0x71f1454c2c000LL),reale(15946535,0x7112262fa3000LL),
      -reale(5597132,0xd891768336000LL),-reale(517466,0x3872db407f000LL),
      -reale(280398,0x37b65ce5ca500LL),reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^12, polynomial in n of order 17
      -reale(9362,0x69735ac9d0000LL),-reale(41698,3327447843LL<<20),
      -reale(274851,0x56e2bdf830000LL),-reale(4724425,0xa83b5c01a0000LL),
      reale(68370240,0x5baadc4870000LL),-reale(262946254,0xff686b9240000LL),
      reale(430395020,0x66a0aab610000LL),-reale(228360148,0x64a23696e0000LL),
      -reale(196492193,0xc6f6cbf150000LL),reale(277855749,243039325LL<<19),
      -reale(54565881,0x3f0390efb0000LL),-reale(10430670,3478671393LL<<17),
      -reale(48232829,0x9769bd8710000LL),reale(26504611,0xd8be140f40000LL),
      reale(733724,0x9fb250690000LL),reale(8992810,0x9e09f3a6a0000LL),
      -reale(7946224,0xca1f6288d0000LL),reale(1176502,0x79934ee544800LL),
      reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^11, polynomial in n of order 18
      -real(0x274a66713f785000LL),-real(0x78cbe0a9df914800LL),
      -reale(6986,0x5cd0ed6f68000LL),-reale(31980,0xbaca6835fb800LL),
      -reale(217574,0x7dc41d384b000LL),-reale(3882916,0x2edd7dacd2800LL),
      reale(58859398,0xdc7c0f67f2000LL),-reale(240755855,0x78dc5ddf79800LL),
      reale(433769587,0x318800cb6f000LL),-reale(298315443,0xab75c9fd0800LL),
      -reale(129660149,0x66ef2473b4000LL),reale(305615878,0x94b6a51048800LL),
      -reale(109156237,0x593300db57000LL),-reale(18007247,0x43b21e10e800LL),
      -reale(29424146,0x61ad17715a000LL),reale(38156138,0xf0096c8a4a800LL),
      -reale(4683041,0xee399b1b9d000LL),-reale(1149725,0xbf46657f8c800LL),
      -reale(1106736,0x8c2ceac93e180LL),reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^10, polynomial in n of order 19
      -real(0x3bd4906e474e000LL),-real(0x97941b80ce3c000LL),
      -real(0x1a66716bc5afa000LL),-real(0x532298a0bc3e0000LL),
      -reale(4939,0xda9250746000LL),-reale(23308,0x7863f72384000LL),
      -reale(164254,0x558c90eef2000LL),-reale(3056120,0xcef6e5fe8000LL),
      reale(48766418,0xafc6204b42000LL),-reale(213414260,0xdc9b1ebcc000LL),
      reale(425806905,0x15318e0496000LL),-reale(369415923,0x757d6c39f0000LL),
      -reale(31178847,0x2c748765b6000LL),reale(306118804,0x213b4942ec000LL),
      -reale(181898310,0x263b289662000LL),reale(568685,0x4686791808000LL),
      -reale(309548,0x34bb55302e000LL),reale(32975540,0x34fcc4d2a4000LL),
      -reale(16246779,0x8dca2dd5da000LL),reale(1477949,0xdae92a7065f00LL),
      reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^9, polynomial in n of order 20
      -real(0x69d018a3b9e000LL),-real(0xed437c3919a800LL),
      -real(0x237e48279feb000LL),-real(0x5bea2151a0b3800LL),
      -real(0x10666acb6ec18000LL),-real(0x350c7e1643d3c800LL),
      -reale(3247,0xe2be74bf45000LL),-reale(15860,0x268da19a55800LL),
      -reale(116263,0x5e4790b892000LL),-reale(2266502,0x8314b6fb1e800LL),
      reale(38294967,0xecf46ee8e1000LL),-reale(180538484,0x555f9ed2b7800LL),
      reale(401643505,0x9c33fda5f4000LL),-reale(432258273,0xf8da98e440800LL),
      reale(101814780,0x5dd5e11f87000LL),reale(252370005,0x80f91f9d26800LL),
      -reale(252307179,0x99e21a8986000LL),reale(63455824,0x191a53ee5d800LL),
      reale(12621880,0x95e41abad000LL),reale(2033357,0xc3307b9c44800LL),
      -reale(4727243,0x20838a8bae80LL),reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^8, polynomial in n of order 21
      -real(0xc09a6adbf4000LL),-real(0x18cab6e3030000LL),
      -real(0x359d0ace62c000LL),-real(0x7ab7d9cc438000LL),
      -real(0x12c67ab580a4000LL),-real(856171152199LL<<18),
      -real(0x9233f1c13ddc000LL),-real(0x1e779de654b48000LL),
      -real(0x789f22a00b054000LL),-reale(9796,7021023797LL<<16),
      -reale(75089,0xae07706a8c000LL),-reale(1543001,0x638fcd4c58000LL),
      reale(27798321,0x1e96e700fc000LL),-reale(142306959,0xd3ad6eb8e0000LL),
      reale(355697955,0xce7f78ffc4000LL),-reale(469861249,0x5989105b68000LL),
      reale(259457720,0x1370b4ff4c000LL),reale(112194489,0x36d40ed990000LL),
      -reale(260872269,0xf8005192ec000LL),reale(151422395,0x58f7b5f388000LL),
      -reale(32332898,0xbdc6e34964000LL),reale(433029,0xe4d3ce78fba00LL),
      reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^7, polynomial in n of order 22
      -real(0x1441fa2f35000LL),-real(0x272c726527800LL),
      -real(0x4ebdd7b856000LL),-real(0xa564301b74800LL),
      -real(0x16d6333bd37000LL),-real(0x3580dec1951800LL),
      -real(0x865ae53c178000LL),-real(0x16ec61d7f65e800LL),
      -real(0x455fa2e228b9000LL),-real(0xef77f4cbfa3b800LL),
      -real(0x3d9c6e708569a000LL),-reale(5230,0x8a511fbc88800LL),
      -reale(42196,0xcfdba8cebb000LL),-reale(920786,0xf57a80c4e5800LL),
      reale(17837247,0x2fc56aab44000LL),-reale(100064916,0x5e72032af2800LL),
      reale(283253574,0xc37962f3c3000LL),-reale(455567530,0xe21e28364f800LL),
      reale(400948026,0xf028b16722000LL),-reale(118913774,0x549816fe9c800LL),
      -reale(112010399,0x36034a3e3f000LL),reale(121825743,0x78c43cf486800LL),
      -reale(36338425,0x426e19287b880LL),
      reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^6, polynomial in n of order 23
      -real(0x1b5badebe000LL),-real(0x326332ca4000LL),-real(0x5fd1bd93a000LL),
      -real(0xbcd8e5378000LL),-real(0x1837bef256000LL),
      -real(0x3404424ccc000LL),-real(0x75bf8cd1d2000LL),
      -real(38025986691LL<<17),-real(0x2dc96f11f6e000LL),
      -real(0x811a6e895f4000LL),-real(0x195036bc82ea000LL),
      -real(0x5af70d135548000LL),-real(0x187d57cdaa406000LL),
      -reale(2189,0x32d399c61c000LL),-reale(18742,0x385cb42a82000LL),
      -reale(438375,0xd6a8872030000LL),reale(9224813,0x89f7eb41e2000LL),
      -reale(57288808,0xfdc8999b44000LL),reale(184899999,0x331692f966000LL),
      -reale(357870966,0x3154fb6f18000LL),reale(431875147,0x7929b7544a000LL),
      -reale(318710001,0xe0f19bd36c000LL),reale(131641087,0xbb852faace000LL),
      -reale(23311442,0x9e8a4070e9d00LL),
      reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[5], coeff of eps^5, polynomial in n of order 24
      -real(92116035LL<<14),-real(0x26e7bc2d800LL),-real(0x46d3779b000LL),
      -real(0x84e1d0c0800LL),-real(0x101cbc30a000LL),-real(0x2073376e3800LL),
      -real(0x442adb8b9000LL),-real(0x963884ff6800LL),-real(0x15dbd71e08000LL),
      -real(0x363ebc6d59800LL),-real(0x9122bbd857000LL),
      -real(0x1a90a4ab06c800LL),-real(0x56f0a68cd06000LL),
      -real(0x147a29992a8f800LL),-real(0x5d1402e6c175000LL),
      -real(0x228e263277d22800LL),-reale(5078,0x584c613b04000LL),
      -reale(128863,0x92233985800LL),reale(2982258,0xd360aa0ed000LL),
      -reale(20710125,0x5bbe664118800LL),reale(76213261,0x519df32cfe000LL),
      -reale(171479837,0xf7a363253b800LL),reale(241341994,0x2d1ed763cf000LL),
      -reale(186491540,0xf45203874e800LL),reale(58278606,0x8c59a11a48880LL),
      reale(45119934611LL,0xe897fd72d67cbLL),
      // C4[6], coeff of eps^29, polynomial in n of order 0
      139264,real(63626127165LL),
      // C4[6], coeff of eps^28, polynomial in n of order 1
      real(247833LL<<16),real(4782743552LL),real(0x219ae3fb400f15LL),
      // C4[6], coeff of eps^27, polynomial in n of order 2
      real(420150473LL<<18),-real(0x876551ce0000LL),real(0x350bfa156000LL),
      reale(4837,0x68f14547adebLL),
      // C4[6], coeff of eps^26, polynomial in n of order 3
      real(0x297e6b0e9e1000LL),-real(0x2e90de909aa000LL),
      real(0x6148b0a84b000LL),real(0x1d77336bca600LL),
      reale(207992,0x1a086a30a3679LL),
      // C4[6], coeff of eps^25, polynomial in n of order 4
      real(0x10bc6a9e4ee30000LL),-real(0xc179e3d40c9c000LL),
      real(0x3edf483df118000LL),-real(0x5c91fff78634000LL),
      real(0x216fdab58654400LL),reale(10078162,0xbedd8dc0620e7LL),
      // C4[6], coeff of eps^24, polynomial in n of order 5
      reale(17715,0xdb1cfba26000LL),-reale(7689,0x9976d7f948000LL),
      reale(6474,0xb1047d5d4a000LL),-reale(6855,0xa6eeabbaa4000LL),
      real(0x2ac3e335ea26e000LL),real(0xd6d2e7c22e28400LL),
      reale(372892021,0x96057cce2c163LL),
      // C4[6], coeff of eps^23, polynomial in n of order 6
      reale(279883,0xa92c150938000LL),-reale(86797,0xd10c69f53c000LL),
      reale(160072,0xfd9d58a4d0000LL),-reale(96731,0xc2b3d16724000LL),
      reale(32938,0x46d62be868000LL),-reale(52162,0xc27e2d9b0c000LL),
      reale(17103,0x67a9fde667c00LL),reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[6], coeff of eps^22, polynomial in n of order 7
      reale(293467,0x7db7c77729000LL),-reale(146628,0x46fd92fe6000LL),
      reale(282074,0xcdca0f3f8b000LL),-reale(92435,0x174eb2c344000LL),
      reale(105774,0xf5edeb18ed000LL),-reale(100726,0x78839052a2000LL),
      reale(6619,0xde4489894f000LL),reale(2174,0xdeb0a21cf2e00LL),
      reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[6], coeff of eps^21, polynomial in n of order 8
      reale(183603,8337878185LL<<19),-reale(387951,0x8934978f10000LL),
      reale(363243,0x9b8677d760000LL),-reale(100927,0x6adc79e30000LL),
      reale(246790,7131746729LL<<18),-reale(115867,0xce56197550000LL),
      reale(45470,0x976a005d20000LL),-reale(74789,0x6bec0ac470000LL),
      reale(21823,0x7d1eb3d72b000LL),reale(4101812237LL,0x723c5cdbe4f41LL),
      // C4[6], coeff of eps^20, polynomial in n of order 9
      reale(2390210,0x71ea4526d8000LL),-reale(11473167,6397281565LL<<18),
      reale(3566140,0xe9fdb6daa8000LL),-reale(3459649,0xbdbfad5d70000LL),
      reale(5328875,0xe507b89678000LL),-reale(1202839,0xbeff1963a0000LL),
      reale(2208040,0x527339ea48000LL),-reale(1770989,0xb71cae09d0000LL),
      reale(48626,0x557ebf6618000LL),reale(16670,0x4a1716aa8d000LL),
      reale(53323559086LL,0xcd10b72aa064dLL),
      // C4[6], coeff of eps^19, polynomial in n of order 10
      reale(16170911,0xf66942f9a0000LL),-reale(15946100,0x87937e1ff0000LL),
      reale(1191966,5683381737LL<<19),-reale(10381645,0x67a9610710000LL),
      reale(5401104,0xec5f94af60000LL),-reale(1916345,0x9f2b7d6630000LL),
      reale(5166787,7293640425LL<<18),-reale(1681428,0xa094a5ad50000LL),
      reale(912008,0xad6a83a520000LL),-reale(1452992,0x3f13404c70000LL),
      reale(367621,0xca46f4fdbb000LL),reale(53323559086LL,0xcd10b72aa064dLL),
      // C4[6], coeff of eps^18, polynomial in n of order 11
      reale(51879505,0x1c6021da42000LL),-reale(3388727,0x452f2e2244000LL),
      reale(10993546,0x58785d1036000LL),-reale(19450323,0x2862de39d0000LL),
      reale(1456775,0xebc764482a000LL),-reale(7922511,0x8d8f4f815c000LL),
      reale(7390372,0xfe1ce59e1e000LL),-reale(1065019,0x2a2a06ce8000LL),
      reale(3871757,0x7ef447ee12000LL),-reale(2395461,0x8df44bf074000LL),
      -reale(40351,0xb597a7abfa000LL),-reale(17707,0xeba2dcf1c1400LL),
      reale(53323559086LL,0xcd10b72aa064dLL),
      // C4[6], coeff of eps^17, polynomial in n of order 12
      reale(18941665,0xd940803e20000LL),-reale(2462456,0xc647b5b638000LL),
      reale(55543449,0x9a9f25d270000LL),-reale(10182797,0xdffcb19ee8000LL),
      reale(4836527,0xb44e233ec0000LL),-reale(21402374,0x58dcab98000LL),
      reale(3817083,0xbef1c88b10000LL),-reale(4459099,0x992120d448000LL),
      reale(8502561,0xac3fb5bf60000LL),-reale(1525489,0x80b8b610f8000LL),
      reale(1649611,0x4cebe6e3b0000LL),-reale(2280763,0x4f507e59a8000LL),
      reale(482782,0x1ffc428c24800LL),reale(53323559086LL,0xcd10b72aa064dLL),
      // C4[6], coeff of eps^16, polynomial in n of order 13
      reale(169672066,0xfc4e53058c000LL),-reale(255936417,0xcd4166f930000LL),
      reale(43044311,0x58bada2414000LL),reale(10984552,0x79ecf34458000LL),
      reale(54615551,0xb3c2ab069c000LL),-reale(20672829,0x547b9ae620000LL),
      -reale(762958,0xc96d76adc000LL),-reale(20252510,0xad74c43098000LL),
      reale(8266131,0x9541dc37ac000LL),-reale(1263055,0x9458475310000LL),
      reale(7416125,0xebded0d634000LL),-reale(3121438,0x16f54c0588000LL),
      -reale(225538,0xf843322744000LL),-reale(111163,0x41ef8785bb800LL),
      reale(53323559086LL,0xcd10b72aa064dLL),
      // C4[6], coeff of eps^15, polynomial in n of order 14
      -reale(371272727,0xe93844d330000LL),reale(258600199,0x3ab9b44ef8000LL),
      reale(127447726,0xd7dad2fc20000LL),-reale(278220404,0x7730102b8000LL),
      reale(77869881,0xad9b189b70000LL),reale(21813766,0xb09d2ff98000LL),
      reale(46644312,9197745227LL<<18),-reale(33841430,0x25b28aa218000LL),
      -reale(3096455,0x6fa54a95f0000LL),-reale(14807144,0xa86ee6dfc8000LL),
      reale(13281582,0xf66e06a960000LL),-reale(452377,0x35cd9cb178000LL),
      reale(3621811,0x85d91d8b0000LL),-reale(3791781,0x3a80710f28000LL),
      reale(636887,0x5f8cc1d1bc800LL),reale(53323559086LL,0xcd10b72aa064dLL),
      // C4[6], coeff of eps^14, polynomial in n of order 15
      -reale(40751652,0x879256f716000LL),reale(182461023,0x62c00442f4000LL),
      -reale(366891419,0xe235688602000LL),reale(303920923,0x2a6218fe88000LL),
      reale(70640959,0xa70aa30512000LL),-reale(290919308,0xf0cc1f4de4000LL),
      reale(124435738,0x116d522626000LL),reale(24575054,0x49539549b0000LL),
      reale(29829722,0x6d4c4f193a000LL),-reale(46205497,0xcd680acebc000LL),
      reale(1253661,0x8798d15a4e000LL),-reale(5829398,0x329c172b28000LL),
      reale(15178042,0x87d0f72562000LL),-reale(3413258,0x604057df94000LL),
      -reale(544537,0x1343d1098a000LL),-reale(371792,0x5ec0380ab3400LL),
      reale(53323559086LL,0xcd10b72aa064dLL),
      // C4[6], coeff of eps^13, polynomial in n of order 16
      reale(100946,21976965LL<<20),reale(2010862,0x3c46708bb0000LL),
      -reale(34502092,0x6e09dbf3a0000LL),reale(163298206,0x527fb2e110000LL),
      -reale(355839921,948516465LL<<18),reale(347383598,0x3243b82e70000LL),
      -reale(2611762,0xae3f6124e0000LL),-reale(286060486,421499843LL<<16),
      reale(181022396,2339564421LL<<19),reale(11053843,0x8ea9e8f130000LL),
      reale(5354229,0xc704cb69e0000LL),-reale(50862137,0xf12aeaf970000LL),
      reale(14064844,5665935493LL<<18),reale(1748678,0x2e869553f0000LL),
      reale(9719088,0x671cfc38a0000LL),-reale(6714197,0x76aa8fd6b0000LL),
      reale(816805,0x9ce5b98e4f000LL),reale(53323559086LL,0xcd10b72aa064dLL),
      // C4[6], coeff of eps^12, polynomial in n of order 17
      real(0x75cff722d22b8000LL),reale(9742,5260669319LL<<19),
      reale(75734,0x79163f0448000LL),reale(1568684,0xd935dd4310000LL),
      -reale(28213944,0x88db35f228000LL),reale(141802366,0xe4716652a0000LL),
      -reale(336424367,0x7aaa4f7098000LL),reale(384795625,0xe2aff0230000LL),
      -reale(92516926,0xbd45322708000LL),-reale(252728877,4730701433LL<<18),
      reale(239978666,0xfd893c3a88000LL),-reale(28528394,5445461995LL<<16),
      -reale(18370370,0x5cd8a4fbe8000LL),-reale(38961300,0x78b7628f20000LL),
      reale(30014507,0xb37b1485a8000LL),-reale(654615,0xa96a2bf90000LL),
      -reale(667571,0x85c41bf0c8000LL),-reale(1181523,0x1c81baa857000LL),
      reale(53323559086LL,0xcd10b72aa064dLL),
      // C4[6], coeff of eps^11, polynomial in n of order 18
      real(0x55d873de6520000LL),real(0x12c7cfeef6810000LL),
      real(0x4e200e3f1e1LL<<20),reale(6671,0xd2467fb9f0000LL),
      reale(53806,3275978471LL<<17),reale(1163348,0xd1cfb7f3d0000LL),
      -reale(22032298,0xf3cc53d740000LL),reale(118198962,4397370971LL<<16),
      -reale(306929389,0x72efa76b60000LL),reale(409945031,0xba4df5f90000LL),
      -reale(195574008,5584443935LL<<19),-reale(178055138,0x4cd4f3ce90000LL),
      reale(282861404,0xd715020c60000LL),-reale(99637722,0xf11193d4b0000LL),
      -reale(20986520,0xfb661347c0000LL),-reale(8771627,7018708525LL<<16),
      reale(31360164,0xdb2c51c420000LL),-reale(12477955,8590832271LL<<16),
      reale(873590,0xbe0d3e9693000LL),reale(53323559086LL,0xcd10b72aa064dLL),
      // C4[6], coeff of eps^10, polynomial in n of order 19
      real(0x5808512b12b000LL),real(0xfaa729276e2000LL),
      real(0x3175560e4519000LL),real(0xb21b680b3a90000LL),
      real(0x2fcbc5fe71407000LL),reale(4229,0xf0de326e3e000LL),
      reale(35532,0x38e22907f5000LL),reale(805604,0x42db4fa3ec000LL),
      -reale(16150031,0xfe4d67d51d000LL),reale(93034137,0xf6628ead9a000LL),
      -reale(265995225,0x398943192f000LL),reale(414315266,0x970145dd48000LL),
      -reale(301204836,0xc549c7ba41000LL),-reale(51738066,0x4e1063bb0a000LL),
      reale(275650719,0x10481031ad000LL),-reale(187610845,0x85f00095c000LL),
      reale(25230256,0x4ada23b49b000LL),reale(13917204,0x3da6dc4452000LL),
      reale(4066715,0x8660f73889000LL),-reale(4361677,0xea98323d07e00LL),
      reale(53323559086LL,0xcd10b72aa064dLL),
      // C4[6], coeff of eps^9, polynomial in n of order 20
      real(0x65fa8c6bf0000LL),real(0xfe88642ae4000LL),real(0x2aa82304e58000LL),
      real(0x7ca8bddcccc000LL),real(434853972467LL<<18),
      real(0x5e16320d44b4000LL),real(0x1a2859bf40b28000LL),
      reale(2409,0x1b825da69c000LL),reale(21179,0xabe6860d90000LL),
      reale(506292,0x5b6e5f0684000LL),-reale(10810252,0xeee1886808000LL),
      reale(67327238,0xa18a80786c000LL),-reale(213364581,0xe79aac41a0000LL),
      reale(387619687,0x51e3ba1054000LL),-reale(387180015,0xd550406b38000LL),
      reale(121695298,0x2400c6e23c000LL),reale(172879787,0x9e57682f30000LL),
      -reale(230507460,0xb74e70fddc000LL),reale(112381926,0x4eee70a198000LL),
      -reale(20283371,0x42949e7bf4000LL),-reale(288686,0x988d3450a7c00LL),
      reale(53323559086LL,0xcd10b72aa064dLL),
      // C4[6], coeff of eps^8, polynomial in n of order 21
      real(0x72e86a7de000LL),real(8772831327LL<<15),real(0x273ffc1812000LL),
      real(0x64635c5cac000LL),real(0x11473cdd246000LL),
      real(0x33fd816c260000LL),real(0xae6e2137a7a000LL),
      real(0x29ff10928814000LL),real(0xc26a115cf4ae000LL),
      real(0x492994f20c1c8000LL),reale(10833,0x80f3c9e4e2000LL),
      reale(274842,0xd406a2037c000LL),-reale(6296293,0xca802ed0ea000LL),
      reale(42731189,0xb6f3d1e130000LL),-reale(151191524,0x41a7e788b6000LL),
      reale(320575109,0xae49526ee4000LL),-reale(416345568,0xb8c8445e82000LL),
      reale(298319523,0xb52957c098000LL),-reale(42956565,0x78799bae4e000LL),
      -reale(119892798,0x70342c95b4000LL),reale(103927174,0x8691916be6000LL),
      -reale(29157346,0x2fb5a3d22ec00LL),
      reale(53323559086LL,0xcd10b72aa064dLL),
      // C4[6], coeff of eps^7, polynomial in n of order 22
      real(74709635LL<<15),real(0x4ba47734000LL),real(0xa7b994d0000LL),
      real(0x1869c5c6c000LL),real(0x3c23e3d88000LL),real(0x9e1c8b7a4000LL),
      real(1882100649LL<<18),real(0x573ad5a4dc000LL),real(0x12f915ab6f8000LL),
      real(0x4c1f4084014000LL),real(0x170ced7cbfb0000LL),
      real(0x921b89aca54c000LL),real(0x599b4a7922068000LL),
      reale(38914,0x1efa73f084000LL),-reale(964915,0x51a6da0ae0000LL),
      reale(7200274,0x92a23dbc000LL),-reale(28652022,0x356dea628000LL),
      reale(70837833,0x39cdeca8f4000LL),-reale(114872161,0xfcdf3a9570000LL),
      reale(122704354,0xd9bfe74e2c000LL),-reale(83141739,0x9edadabcb8000LL),
      reale(32332898,0xbdc6e34964000LL),-reale(5485045,0x527ae1fc73400LL),
      reale(17774519695LL,0x99b03d0e3576fLL),
      // C4[6], coeff of eps^6, polynomial in n of order 23
      real(257316433920LL),real(517719121920LL),real(0xfb6e649000LL),
      real(0x221f7064000LL),real(0x4d84a37f000LL),real(0xb958155a000LL),
      real(0x1d5dd0db5000LL),real(0x4faa5a050000LL),real(0xea04686eb000LL),
      real(0x2f40e3db46000LL),real(0xab8623d121000LL),real(0x2d147c4903c000LL),
      real(0xe63ae874e57000LL),real(0x60cd21bcc932000LL),
      real(0x3f869e23e408d000LL),reale(29814,0xcc97221028000LL),
      -reale(808726,0x6d837bf63d000LL),reale(6700876,0x1daf27af1e000LL),
      -reale(30153942,0x8594329407000LL),reale(86154121,0x7da76bf014000LL),
      -reale(165128732,0xdb80e436d1000LL),reale(210163841,0xd18cc55d0a000LL),
      -reale(153581269,0x570b79c9b000LL),reale(46622885,0x3d1480e1d3a00LL),
      reale(53323559086LL,0xcd10b72aa064dLL),
      // C4[7], coeff of eps^29, polynomial in n of order 0
      real(13087612928LL),real(0x90e6983c364f3dLL),
      // C4[7], coeff of eps^28, polynomial in n of order 1
      -real(161707LL<<21),real(7239297LL<<14),real(0xcf8f801ee602cdLL),
      // C4[7], coeff of eps^27, polynomial in n of order 2
      -real(3500022825LL<<20),real(630513507LL<<19),real(0x6038c37fa000LL),
      reale(72555,0x626230f3330c5LL),
      // C4[7], coeff of eps^26, polynomial in n of order 3
      -real(92252949633LL<<21),real(16187170389LL<<22),
      -real(51975912235LL<<21),real(0x7c00d0f2b78000LL),
      reale(3119881,0x867e38d993117LL),
      // C4[7], coeff of eps^25, polynomial in n of order 4
      -real(0x64d0a86bae7c0000LL),real(0x7c07ce24c65f0000LL),
      -real(0x739ece76489e0000LL),real(0x6e7bce15f550000LL),
      real(0x24fc420030b8400LL),reale(127915142,0x8a371ad88dcafLL),
      // C4[7], coeff of eps^24, polynomial in n of order 5
      -reale(5990,0xbd2326cc40000LL),reale(14992,4018200301LL<<20),
      -reale(6873,8929851351LL<<18),reale(2782,8051012645LL<<19),
      -reale(4583,0xc89924b340000LL),real(0x52aed30dcf988800LL),
      reale(430260024,0xe82db7640b7c1LL),
      // C4[7], coeff of eps^23, polynomial in n of order 6
      -reale(169326,4206873009LL<<17),reale(261065,0x25b4e353d0000LL),
      -reale(59142,0xf0c50992c0000LL),reale(111182,4597550539LL<<16),
      -reale(88869,504433083LL<<17),reale(2313,0xe34bfe3f90000LL),
      real(0x32dc48b9e1d23400LL),reale(4732860273LL,0xf9f6e14c7e54bLL),
      // C4[7], coeff of eps^22, polynomial in n of order 7
      -reale(467157,1100000847LL<<20),reale(258178,755278933LL<<21),
      -reale(91474,559664221LL<<20),reale(248285,171426119LL<<22),
      -reale(82821,231309675LL<<20),reale(44668,65972935LL<<21),
      -reale(71456,2669582201LL<<20),reale(18220,0x9846e079d4000LL),
      reale(4732860273LL,0xf9f6e14c7e54bLL),
      // C4[7], coeff of eps^21, polynomial in n of order 8
      -reale(10858183,1145150433LL<<21),reale(1155453,0xa514064740000LL),
      -reale(4408275,1110140307LL<<19),reale(4494002,0xa8330ec1c0000LL),
      -reale(693747,3759921697LL<<20),reale(2336198,8880970129LL<<18),
      -reale(1499288,4981657777LL<<19),-reale(18466,6402610053LL<<18),
      -reale(7818,0x6ee4879b83000LL),reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[7], coeff of eps^20, polynomial in n of order 9
      -reale(7907170,4058896835LL<<20),reale(1601483,335338375LL<<23),
      -reale(11238504,3427529005LL<<20),reale(2745284,1787777405LL<<21),
      -reale(2325455,2252860775LL<<20),reale(4939939,712213223LL<<22),
      -reale(1021126,555773201LL<<20),reale(952760,1631005375LL<<21),
      -reale(1365312,965324491LL<<20),reale(299618,0x2f589c3f22000LL),
      reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[7], coeff of eps^19, polynomial in n of order 10
      reale(5811147,7891888051LL<<19),reale(19155879,6260648859LL<<18),
      -reale(13234724,832589145LL<<21),-reale(473729,0xaccee67ac0000LL),
      -reale(9690431,2460044795LL<<19),reale(5218195,5282091375LL<<18),
      -reale(699193,3313511321LL<<20),reale(4032431,0xb01d955a40000LL),
      -reale(1901524,5999844905LL<<19),-reale(111197,715304509LL<<18),
      -reale(51622,0xdda253af9f000LL),reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[7], coeff of eps^18, polynomial in n of order 11
      -reale(34477536,1085877825LL<<20),reale(44845230,817114545LL<<21),
      reale(4606432,1572161669LL<<20),reale(12496576,210421693LL<<23),
      -reale(18271471,1543698101LL<<20),-reale(128700,742574025LL<<21),
      -reale(6139017,689151983LL<<20),reale(7385046,100502461LL<<22),
      -reale(590509,2783893289LL<<20),reale(1800602,699157181LL<<21),
      -reale(2092277,3566080099LL<<20),reale(381025,0x99466ecd7c000LL),
      reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[7], coeff of eps^17, polynomial in n of order 12
      -reale(152644671,981125379LL<<19),-reale(24136152,0xd3514f38e0000LL),
      -reale(16909786,8097141141LL<<18),reale(53988238,0xc115854860000LL),
      -reale(2192558,3293732289LL<<20),reale(3853073,2819007469LL<<17),
      -reale(20689919,5309095411LL<<18),reale(3514368,0xf1b4463ee0000LL),
      -reale(1814216,3975618817LL<<19),reale(7354899,0xbd88356420000LL),
      -reale(2207882,191252177LL<<18),-reale(269543,3717910997LL<<17),
      -reale(156646,0x7bcb3b3a6a800LL),reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[7], coeff of eps^16, polynomial in n of order 13
      reale(52565396,753292423LL<<19),reale(252855342,568744119LL<<21),
      -reale(197183211,7281644191LL<<19),-reale(6678358,3552459447LL<<20),
      reale(4519131,7283469291LL<<19),reale(56648760,112164189LL<<22),
      -reale(15289276,2020707835LL<<19),-reale(3713103,1403767329LL<<20),
      -reale(17880720,7304289905LL<<19),reale(9494998,1497636157LL<<21),
      reale(492167,2907561065LL<<19),reale(3952538,4294903605LL<<20),
      -reale(3358139,5130468237LL<<19),reale(480004,0x1e727719e9000LL),
      reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[7], coeff of eps^15, polynomial in n of order 14
      reale(279617399,0xd9972cba40000LL),-reale(353187715,0x687b832220000LL),
      reale(118965967,4456434973LL<<19),reale(220096359,3595022681LL<<17),
      -reale(240814657,8170991797LL<<18),reale(28075084,0xec7a345460000LL),
      reale(24758769,1818605983LL<<20),reale(48013974,0xb5345431a0000LL),
      -reale(32373431,0xc7bac8f4c0000LL),-reale(5075135,8642954025LL<<17),
      -reale(9094832,6469786017LL<<19),reale(13639028,3685620545LL<<17),
      -reale(1773068,1431802737LL<<18),-reale(460476,0x51ab5a8ea0000LL),
      -reale(423738,0x5d98934922800LL),reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[7], coeff of eps^14, polynomial in n of order 15
      reale(16417106,2408387839LL<<20),-reale(93245803,1562234793LL<<21),
      reale(256985456,250552029LL<<20),-reale(365861944,857240429LL<<22),
      reale(190902238,1499270843LL<<20),reale(163412998,1423242741LL<<21),
      -reale(274443985,2668181351LL<<20),reale(82958237,163620913LL<<23),
      reale(33859016,1729347703LL<<20),reale(25275487,1495319443LL<<21),
      -reale(45844273,3794232747LL<<20),reale(4490176,231613489LL<<22),
      reale(1010900,690735667LL<<20),reale(10013483,1036831025LL<<21),
      -reale(5637707,2068106223LL<<20),reale(570308,0x8f0afe45ec000LL),
      reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[7], coeff of eps^13, polynomial in n of order 16
      -reale(25657,393048869LL<<22),-reale(608651,0xacbc40d5c0000LL),
      reale(12764052,2144856077LL<<19),-reale(76823449,3121867141LL<<18),
      reale(228672619,4131243473LL<<20),-reale(367062288,0xf756dca4c0000LL),
      reale(263470997,8569317111LL<<19),reale(78573490,0xffb10f8fc0000LL),
      -reale(283548774,1794660389LL<<21),reale(154702622,9227087281LL<<18),
      reale(16937276,1939608161LL<<19),-reale(6432822,7897704317LL<<18),
      -reale(43016670,946798949LL<<20),reale(22087851,0xaa7600dd40000LL),
      reale(1665577,8523064651LL<<19),-reale(163221,0xe0acad2e40000LL),
      -reale(1189371,0x766c2260a3000LL),reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[7], coeff of eps^12, polynomial in n of order 17
      -real(0x13bc5107d5fLL<<20),-real(506650109317LL<<24),
      -reale(17217,2185571073LL<<20),-reale(426469,557216187LL<<21),
      reale(9411503,1140836685LL<<20),-reale(60299258,66945391LL<<22),
      reale(194753933,1835852139LL<<20),-reale(352928157,2046106529LL<<21),
      reale(328231147,2405007161LL<<20),-reale(34561926,360605189LL<<23),
      -reale(248371006,3915897705LL<<20),reale(226668375,1401273273LL<<21),
      -reale(40114392,2920598683LL<<20),-reale(25898188,1028871717LL<<22),
      -reale(16043876,2538787453LL<<20),reale(28698456,1825641427LL<<21),
      -reale(9602688,2437057327LL<<20),reale(502063,0xa52218333a000LL),
      reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[7], coeff of eps^11, polynomial in n of order 18
      -real(81880241733LL<<19),-real(651169421489LL<<18),
      -real(194261131981LL<<22),-real(0x4616f301f1bc0000LL),
      -reale(10659,7786635659LL<<19),-reale(276843,0xf150eaf340000LL),
      reale(6459374,425055961LL<<20),-reale(44283297,2370521611LL<<18),
      reale(156003403,8328479919LL<<19),-reale(319848045,0xab86b09a40000LL),
      reale(372382116,1407449139LL<<21),-reale(166870261,0xbaeb2e09c0000LL),
      -reale(148815577,7753476247LL<<19),reale(260443738,1330203003LL<<18),
      -reale(131653575,428167437LL<<20),reale(2775725,691412797LL<<18),
      reale(12306214,6299226531LL<<19),reale(5355345,9401097695LL<<18),
      -reale(3966302,0xcbc08bfb17000LL),reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[7], coeff of eps^10, polynomial in n of order 19
      -real(1704454843LL<<20),-real(2722537665LL<<21),-real(19434970697LL<<20),
      -real(4989045369LL<<24),-real(394962411735LL<<20),
      -real(0x128b33efecfLL<<21),-reale(5903,789230693LL<<20),
      -reale(161527,569013611LL<<22),reale(4006338,1271698701LL<<20),
      -reale(29564239,1312346333LL<<21),reale(114267945,2347153791LL<<20),
      -reale(265827046,63320697LL<<23),reale(379233361,4202669809LL<<20),
      -reale(292689947,1148927723LL<<21),reale(19915451,3747715939LL<<20),
      reale(197711494,385979271LL<<22),-reale(197401730,911113003LL<<20),
      reale(83971818,387288839LL<<21),-reale(12852829,1345691321LL<<20),
      -reale(602476,0x5fc28370ac000LL),reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[7], coeff of eps^9, polynomial in n of order 20
      -real(304621785LL<<18),-real(0xc9814e4b0000LL),-real(5069418237LL<<17),
      -real(0x7c4fe70d90000LL),-real(7691534469LL<<20),
      -real(0x7a02179d470000LL),-real(0x274586580a60000LL),
      -real(0x10907db87bd50000LL),-reale(2773,9732262223LL<<18),
      -reale(80424,78339267LL<<16),reale(2134032,0xd8c3d9bae0000LL),
      -reale(17066460,0x8709888510000LL),reale(72842964,6932239995LL<<19),
      -reale(192914141,0x448548ebf0000LL),reale(332328916,0xa61d5e5020000LL),
      -reale(364348462,0x260e7984d0000LL),reale(215166704,0xfd6630ec0000LL),
      reale(5301792,6304582341LL<<16),-reale(118567350,0x6a550b6aa0000LL),
      reale(89166503,0x5c73fd2370000LL),-reale(23960987,0x75c7f62663400LL),
      reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[7], coeff of eps^8, polynomial in n of order 21
      -real(11869221LL<<18),-real(7450235LL<<20),-real(79397539LL<<18),
      -real(113271327LL<<19),-real(700448177LL<<18),-real(148973407LL<<22),
      -real(9118660335LL<<18),-real(20216702289LL<<19),
      -real(0xcadd965ff40000LL),-real(386512744317LL<<20),
      -real(0xf93c68aca7bLL<<18),-reale(30847,5279995331LL<<19),
      reale(882325,8584251383LL<<18),-reale(7706931,2116826591LL<<21),
      reale(36580048,2730390969LL<<18),-reale(110604386,3847062005LL<<19),
      reale(227103584,0x9e98f54ac0000LL),-reale(323034443,1752619391LL<<20),
      reale(314251676,0xb5ebcf2b40000LL),-reale(199218854,3061725287LL<<19),
      reale(73903768,9476063903LL<<18),-reale(12124837,0x72a953b85800LL),
      reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[7], coeff of eps^7, polynomial in n of order 22
      -real(575575LL<<17),-real(2681133LL<<16),-real(1637545LL<<18),
      -real(16890107LL<<16),-real(23159565LL<<17),-real(0x8210e690000LL),
      -real(27276821LL<<20),-real(0x5bebf1b70000LL),-real(3075032387LL<<17),
      -real(0x6a5f183250000LL),-real(40477467135LL<<18),
      -real(0x11b5c31caf30000LL),-real(0xd14cd352ff20000LL),
      -reale(6969,0xb17d189610000LL),reale(216834,7757873387LL<<19),
      -reale(2087035,0xf153506af0000LL),reale(11091105,0x4b9d7f7a20000LL),
      -reale(38290720,0xa99fbe31d0000LL),reale(91897729,0x9718fbaac0000LL),
      -reale(156643857,0x418d7e6eb0000LL),reale(184759421,0x6102c9a360000LL),
      -reale(129331594,0xf71b8d2590000LL),reale(38395317,0x415c2de726c00LL),
      reale(61527183561LL,0xb18970e26a4cfLL),
      // C4[8], coeff of eps^29, polynomial in n of order 0
      real(7241<<16),real(0x112c657acf71bLL),
      // C4[8], coeff of eps^28, polynomial in n of order 1
      real(1165359LL<<20),real(3168035LL<<17),real(0x21ffb4a731cf423fLL),
      // C4[8], coeff of eps^27, polynomial in n of order 2
      real(41827383LL<<21),-real(137865429LL<<20),real(631109843LL<<16),
      reale(4837,0x68f14547adebLL),
      // C4[8], coeff of eps^26, polynomial in n of order 3
      real(54350489115LL<<22),-real(21656377197LL<<23),real(1080358617LL<<22),
      real(0x5c4a2579a0000LL),reale(3535865,0xba8f0d3ad9e09LL),
      // C4[8], coeff of eps^25, polynomial in n of order 4
      reale(4480,63845967LL<<22),-real(0x5f0bc8cec07LL<<20),
      real(0x198015cca1fLL<<21),-real(0x51d1e6f78cdLL<<20),
      real(0x14fb331d33f30000LL),reale(144970494,0xe0e91e6ce4f71LL),
      // C4[8], coeff of eps^24, polynomial in n of order 5
      reale(226427,7535956641LL<<17),-reale(36730,6647829291LL<<19),
      reale(116830,5936429895LL<<17),-reale(76966,613785099LL<<18),
      -real(0x2a948e8d73a60000LL),-real(0x116572b5168a4000LL),
      reale(5363908310LL,0x81b165bd17b55LL),
      // C4[8], coeff of eps^23, polynomial in n of order 6
      reale(151394,3866446399LL<<20),-reale(105723,1435687723LL<<19),
      reale(240417,2090106533LL<<21),-reale(54672,3991575693LL<<19),
      reale(46185,3230210197LL<<20),-reale(67790,4028416911LL<<19),
      reale(15270,0xa469197488000LL),reale(5363908310LL,0x81b165bd17b55LL),
      // C4[8], coeff of eps^22, polynomial in n of order 7
      -reale(105618,1394014919LL<<21),-reale(5351753,377020849LL<<22),
      reale(3446650,1690522763LL<<21),-reale(453181,286167171LL<<23),
      reale(2431204,1637447437LL<<21),-reale(1239333,63204475LL<<22),
      -reale(60030,1665832481LL<<21),-reale(26716,0x6a2a5d69d0000LL),
      reale(69730808036LL,0x96022a9a34351LL),
      // C4[8], coeff of eps^21, polynomial in n of order 8
      reale(4362900,465328075LL<<22),-reale(10560212,802403079LL<<19),
      reale(656010,2976408017LL<<20),-reale(3068612,8162681445LL<<19),
      reale(4482659,1990068235LL<<21),-reale(516359,5969201251LL<<19),
      reale(1022585,502576667LL<<20),-reale(1273161,3447687361LL<<19),
      reale(245310,0x45a78ad538000LL),reale(69730808036LL,0x96022a9a34351LL),
      // C4[8], coeff of eps^20, polynomial in n of order 9
      reale(23624906,1629010283LL<<19),-reale(5851601,324958949LL<<22),
      reale(255419,6850290885LL<<19),-reale(10559838,1365338319LL<<20),
      reale(3058631,5351542623LL<<19),-reale(782822,266312293LL<<21),
      reale(4071490,3032871865LL<<19),-reale(1457911,2387656005LL<<20),
      -reale(144540,929274797LL<<19),-reale(76349,0x2c1c25d590000LL),
      reale(69730808036LL,0x96022a9a34351LL),
      // C4[8], coeff of eps^19, polynomial in n of order 10
      reale(23056909,2395766741LL<<20),reale(8427619,3212023717LL<<19),
      reale(19522568,367619617LL<<22),-reale(12637641,5752438869LL<<19),
      -reale(1859539,2126155853LL<<20),-reale(7720368,2358643951LL<<19),
      reale(5969641,447801057LL<<21),-reale(4464,2860310889LL<<19),
      reale(1954609,2968726289LL<<20),-reale(1904959,7710818563LL<<19),
      reale(302310,0x7de136fc28000LL),reale(69730808036LL,0x96022a9a34351LL),
      // C4[8], coeff of eps^18, polynomial in n of order 11
      -reale(34760584,1377594673LL<<21),-reale(45089279,700199389LL<<22),
      reale(38964787,642296389LL<<21),reale(8377867,242649263LL<<24),
      reale(10805340,270655563LL<<21),-reale(18348308,77894251LL<<22),
      -reale(8504,712824639LL<<21),-reale(2874058,104939633LL<<23),
      reale(7002991,271121287LL<<21),-reale(1456031,497148985LL<<22),
      -reale(262921,1640850307LL<<21),-reale(186713,0x66184da2b0000LL),
      reale(69730808036LL,0x96022a9a34351LL),
      // C4[8], coeff of eps^17, polynomial in n of order 12
      reale(266733950,1060079417LL<<21),-reale(92315127,311764467LL<<19),
      -reale(39784767,2743383633LL<<20),-reale(24124714,5368290721LL<<19),
      reale(52035773,16490707LL<<22),-reale(214469,3779317103LL<<19),
      -reale(32744,3406796695LL<<20),-reale(19012957,4710528797LL<<19),
      reale(5897141,767669011LL<<21),reale(758445,547828629LL<<19),
      reale(4185368,186448291LL<<20),-reale(2955613,5547446041LL<<19),
      reale(363691,0xed908404b8000LL),reale(69730808036LL,0x96022a9a34351LL),
      // C4[8], coeff of eps^16, polynomial in n of order 13
      -reale(254507630,0xe2b8b6bb40000LL),-reale(58148124,1471923579LL<<20),
      reale(270522720,3187458133LL<<18),-reale(149985652,7726894061LL<<19),
      -reale(27328603,0x99e6e9ea40000LL),reale(4011861,407374679LL<<21),
      reale(54943982,0xaf3dd22640000LL),-reale(17478454,3922351195LL<<19),
      -reale(6848089,0x9bbe86d940000LL),-reale(11885922,3297252137LL<<20),
      reale(11706960,678889437LL<<18),-reale(595378,2432546249LL<<19),
      -reale(329167,0xe066968840000LL),-reale(450081,0x595d162958000LL),
      reale(69730808036LL,0x96022a9a34351LL),
      // C4[8], coeff of eps^15, polynomial in n of order 14
      -reale(166710239,3480741959LL<<20),reale(313421255,2329933911LL<<19),
      -reale(299209385,1287661491LL<<21),reale(24383199,5307535921LL<<19),
      reale(248029318,3243559867LL<<20),-reale(210032461,8189928917LL<<19),
      reale(10907073,960500783LL<<22),reale(29948106,2038678405LL<<19),
      reale(40306034,2725271357LL<<20),-reale(36745958,6196825729LL<<19),
      -reale(1934460,123839633LL<<21),-reale(492518,4761069735LL<<19),
      reale(9949315,1255380799LL<<20),-reale(4720329,388065197LL<<19),
      reale(398374,0x9081f25c18000LL),reale(69730808036LL,0x96022a9a34351LL),
      // C4[8], coeff of eps^14, polynomial in n of order 15
      -reale(5499415,942753073LL<<21),reale(38811064,349279653LL<<22),
      -reale(140017234,1709558915LL<<21),reale(290760665,163783697LL<<23),
      -reale(332595868,714890693LL<<21),reale(118902683,730607999LL<<22),
      reale(189429058,237701737LL<<21),-reale(256456610,243945477LL<<24),
      reale(78365400,1246868327LL<<21),reale(35514427,78282137LL<<22),
      reale(8045132,96899221LL<<21),-reale(42695880,422981029LL<<23),
      reale(15212575,506177875LL<<21),reale(2863173,903918451LL<<22),
      reale(284029,1922203905LL<<21),-reale(1161079,0x6c18f2ad70000LL),
      reale(69730808036LL,0x96022a9a34351LL),
      // C4[8], coeff of eps^13, polynomial in n of order 16
      reale(5407,439728533LL<<23),reale(151556,693836399LL<<19),
      -reale(3836797,3870271773LL<<20),reale(28742693,8016450573LL<<19),
      -reale(111747677,1508361473LL<<21),reale(256964119,236840267LL<<19),
      -reale(347691811,711701031LL<<20),reale(216072654,2622689769LL<<19),
      reale(88295276,790755477LL<<22),-reale(263658780,5516898905LL<<19),
      reale(163753678,37603151LL<<20),-reale(1803857,6339257275LL<<19),
      -reale(22560155,108468139LL<<21),-reale(21197875,3801517693LL<<19),
      reale(25658955,3111371333LL<<20),-reale(7416706,6937931487LL<<19),
      reale(268690,0x9ce0757848000LL),reale(69730808036LL,0x96022a9a34351LL),
      // C4[8], coeff of eps^12, polynomial in n of order 17
      real(369814360487LL<<19),real(159053188703LL<<23),
      reale(3152,1136779065LL<<19),reale(92558,2295771257LL<<20),
      -reale(2473330,1734833909LL<<19),reale(19757571,360351901LL<<21),
      -reale(83162616,6619531363LL<<19),reale(212413714,2066066043LL<<20),
      -reale(337117487,5524436113LL<<19),reale(299293348,697772127LL<<22),
      -reale(51076102,4535292671LL<<19),-reale(200831521,1217539203LL<<20),
      reale(227963885,2651839699LL<<19),-reale(87452614,163103009LL<<21),
      -reale(9664164,7186873499LL<<19),reale(9739937,3920029055LL<<20),
      reale(6101400,4999938359LL<<19),-reale(3588081,0x84422b3e50000LL),
      reale(69730808036LL,0x96022a9a34351LL),
      // C4[8], coeff of eps^11, polynomial in n of order 18
      real(3576016431LL<<20),real(32208729499LL<<19),real(10983028711LL<<23),
      real(0x9286be006280000LL),real(0x65e9f47db41LL<<20),
      reale(50386,4528870031LL<<19),-reale(1428014,1685009291LL<<21),
      reale(12227031,6176103481LL<<19),-reale(56007028,2392678701LL<<20),
      reale(159476659,5817614083LL<<19),-reale(295263705,809939737LL<<22),
      reale(344605761,6427356205LL<<19),-reale(202719833,951923227LL<<20),
      -reale(50658828,5629491977LL<<19),reale(201884255,1512264231LL<<21),
      -reale(166564947,5368120671LL<<19),reale(63259557,2614639991LL<<20),
      -reale(8140125,1990873493LL<<19),-reale(722971,0xa61c9dba68000LL),
      reale(69730808036LL,0x96022a9a34351LL),
      // C4[8], coeff of eps^10, polynomial in n of order 19
      real(45596577LL<<21),real(81531441LL<<22),real(656187675LL<<21),
      real(191463201LL<<25),real(17391213765LL<<21),real(65094511967LL<<22),
      real(0x16272ee843fLL<<21),reale(23168,382193603LL<<23),
      -reale(700305,441535191LL<<21),reale(6465118,134564813LL<<22),
      -reale(32414063,913166045LL<<21),reale(103314135,145041825LL<<24),
      -reale(222332965,1267927603LL<<21),reale(326070132,55789563LL<<22),
      -reale(309964302,1355885369LL<<21),reale(149975409,308317249LL<<23),
      reale(35633361,576916401LL<<21),-reale(113104897,1027785623LL<<22),
      reale(77116975,1889590315LL<<21),-reale(20082545,0xcd53c80110000LL),
      reale(69730808036LL,0x96022a9a34351LL),
      // C4[8], coeff of eps^9, polynomial in n of order 20
      real(1123785LL<<21),real(13838643LL<<19),real(23159565LL<<20),
      real(171251217LL<<19),real(44667189LL<<23),real(3472549135LL<<19),
      real(10302054723LL<<20),real(162001999341LL<<19),
      real(500351698399LL<<21),reale(8102,6578411627LL<<19),
      -reale(262912,1458939143LL<<20),reale(2634746,8016047177LL<<19),
      -reale(14551212,731365323LL<<22),reale(52133305,8561410375LL<<19),
      -reale(129964645,2819080401LL<<20),reale(232230181,2215647013LL<<19),
      -reale(298362920,1016411147LL<<21),reale(269480987,8039357859LL<<19),
      -reale(161945649,1493828379LL<<20),reale(57837731,7816254593LL<<19),
      -reale(9237971,9476063903LL<<15),reale(69730808036LL,0x96022a9a34351LL),
      // C4[8], coeff of eps^8, polynomial in n of order 21
      real(292383LL<<17),real(202215LL<<19),real(2386137LL<<17),
      real(3789747LL<<18),real(26247507LL<<17),real(6294651LL<<21),
      real(437764365LL<<17),real(1112245757LL<<18),real(0x67551030e0000LL),
      real(28804895217LL<<19),real(0x2c0f1d988820000LL),
      real(0x66a336663d1c0000LL),-reale(57641,8501165381LL<<17),
      reale(631918,3696102011LL<<20),-reale(3870503,720372107LL<<17),
      reale(15639991,0xcc8b836440000LL),-reale(44892569,0xd7d7de220000LL),
      reale(94682509,2360318459LL<<19),-reale(147486216,0x5ecdb08ae0000LL),
      reale(163873573,0xbeaba7b6c0000LL),-reale(110855652,0xd3ce78fba0000LL),
      reale(32332898,0xbdc6e34964000LL),reale(69730808036LL,0x96022a9a34351LL),
      // C4[9], coeff of eps^29, polynomial in n of order 0
      real(16847<<16),real(0x3d2e2985830503LL),
      // C4[9], coeff of eps^28, polynomial in n of order 1
      -real(207753LL<<23),real(1712087LL<<18),real(0x438da32e1600335LL),
      // C4[9], coeff of eps^27, polynomial in n of order 2
      -real(3127493161LL<<21),-real(38277317LL<<20),-real(0xe4960490000LL),
      reale(161925,0x30e683ffe0741LL),
      // C4[9], coeff of eps^26, polynomial in n of order 3
      -real(9299582409LL<<22),real(3656674463LL<<23),-real(10918261107LL<<22),
      real(80278491423LL<<17),reale(1317283,0x4f8aa089603a9LL),
      // C4[9], coeff of eps^25, polynomial in n of order 4
      -real(711479186953LL<<22),reale(3279,1361598081LL<<20),
      -real(0x3749d192179LL<<21),-real(309897952117LL<<20),
      -real(0x1f18264b9990000LL),reale(162025847,0x379b22013c233LL),
      // C4[9], coeff of eps^24, polynomial in n of order 5
      -reale(133856,15001023LL<<25),reale(223946,23087107LL<<27),
      -reale(32028,12079289LL<<25),reale(48931,2142027LL<<26),
      -reale(63933,112742755LL<<25),reale(12842,2153614949LL<<20),
      reale(5994956347LL,0x96bea2db115fLL),
      // C4[9], coeff of eps^23, polynomial in n of order 6
      -reale(5988742,4056322469LL<<20),reale(2349145,7648181561LL<<19),
      -reale(426462,344885543LL<<21),reale(2475174,940948911LL<<19),
      -reale(999559,3441325239LL<<20),-reale(83146,4971496059LL<<19),
      -reale(41198,0x4f02423cb8000LL),reale(77934432511LL,0x7a7ae451fe1d3LL),
      // C4[9], coeff of eps^22, polynomial in n of order 7
      -reale(8631189,1052889985LL<<21),-reale(629492,634245703LL<<22),
      -reale(3874477,505696163LL<<21),reale(3866974,513650043LL<<23),
      -reale(159710,1408881461LL<<21),reale(1100061,714281683LL<<22),
      -reale(1180171,586380503LL<<21),reale(201643,0x9fcf910730000LL),
      reale(77934432511LL,0x7a7ae451fe1d3LL),
      // C4[9], coeff of eps^21, polynomial in n of order 8
      reale(341632,721850923LL<<22),reale(2597220,4632100393LL<<19),
      -reale(10372056,1528523471LL<<20),reale(1205419,4719051179LL<<19),
      -reale(1145316,921601685LL<<21),reale(3990959,6117017869LL<<19),
      -reale(1073059,3486842565LL<<20),-reale(153111,7776387185LL<<19),
      -reale(94130,0x280827bb28000LL),reale(77934432511LL,0x7a7ae451fe1d3LL),
      // C4[9], coeff of eps^20, polynomial in n of order 9
      reale(3783713,134627971LL<<22),reale(23115315,66415493LL<<25),
      -reale(6392067,518305043LL<<22),-reale(1733013,275013225LL<<23),
      -reale(8816564,833972409LL<<22),reale(4468878,247379557LL<<24),
      reale(300534,553592433LL<<22),reale(2085027,317816093LL<<23),
      -reale(1725044,456624309LL<<22),reale(240877,0x8d28f00d60000LL),
      reale(77934432511LL,0x7a7ae451fe1d3LL),
      // C4[9], coeff of eps^19, polynomial in n of order 10
      -reale(58776429,1067354331LL<<20),reale(18829393,7579267909LL<<19),
      reale(10681211,592856305LL<<22),reale(16946593,1116323851LL<<19),
      -reale(14277877,2775669533LL<<20),-reale(2149268,8027942223LL<<19),
      -reale(4056423,828321103LL<<21),reale(6443828,4480734455LL<<19),
      -reale(857619,751312863LL<<20),-reale(227988,4599783267LL<<19),
      -reale(205805,0x3340b739f8000LL),reale(77934432511LL,0x7a7ae451fe1d3LL),
      // C4[9], coeff of eps^18, polynomial in n of order 11
      -reale(8326980,196156635LL<<21),-reale(34821146,552451591LL<<22),
      -reale(46791029,557069513LL<<21),reale(38334852,177025053LL<<24),
      reale(8937287,838991609LL<<21),reale(5317827,1033371567LL<<22),
      -reale(18378967,400717301LL<<21),reale(2844411,12754877LL<<23),
      reale(593018,1659418637LL<<21),reale(4310821,76308389LL<<22),
      -reale(2591282,1217948513LL<<21),reale(276451,0x9f1a0fb950000LL),
      reale(77934432511LL,0x7a7ae451fe1d3LL),
      // C4[9], coeff of eps^17, polynomial in n of order 12
      -reale(182057178,279202431LL<<21),reale(246730983,7282837989LL<<19),
      -reale(61609386,3656132889LL<<20),-reale(45340440,795982425LL<<19),
      -reale(20534253,134894613LL<<22),reale(51951312,243959241LL<<19),
      -reale(4823611,581671439LL<<20),-reale(5624597,8387045493LL<<19),
      -reale(13789372,989768405LL<<21),reale(9667615,6509295661LL<<19),
      reale(215496,1395596667LL<<20),-reale(184969,6596889361LL<<19),
      -reale(459826,0xaf07be5d28000LL),reale(77934432511LL,0x7a7ae451fe1d3LL),
      // C4[9], coeff of eps^16, polynomial in n of order 13
      reale(316814308,524172905LL<<23),-reale(186563878,63588247LL<<25),
      -reale(110274143,526845649LL<<23),reale(263023219,83035351LL<<24),
      -reale(130904637,509865531LL<<23),-reale(30397924,20206077LL<<26),
      reale(13683269,123318603LL<<23),reale(48158450,141529345LL<<24),
      -reale(26437768,420249375LL<<23),-reale(5662772,129791197LL<<25),
      -reale(2185901,350246489LL<<23),reale(9629298,177188459LL<<24),
      -reale(3949112,493038403LL<<23),reale(276643,3634960421LL<<18),
      reale(77934432511LL,0x7a7ae451fe1d3LL),
      // C4[9], coeff of eps^15, polynomial in n of order 14
      reale(78761274,365004673LL<<20),-reale(201515576,8484307809LL<<19),
      reale(313194006,1602645493LL<<21),-reale(252751914,5568714583LL<<19),
      -reale(13191170,2167441005LL<<20),reale(241719441,7606152787LL<<19),
      -reale(201822768,404840345LL<<22),reale(21302083,5136432093LL<<19),
      reale(37050656,1255073061LL<<20),reale(20598069,641077127LL<<19),
      -reale(39565379,1270355289LL<<21),reale(9630032,7047419793LL<<19),
      reale(3352897,1867330359LL<<20),reale(651457,7128437307LL<<19),
      -reale(1114108,0x7475455348000LL),reale(77934432511LL,0x7a7ae451fe1d3LL),
      // C4[9], coeff of eps^14, polynomial in n of order 15
      reale(1503684,1762694997LL<<21),-reale(12945810,457623793LL<<22),
      reale(59109631,1997027375LL<<21),-reale(165337541,436423661LL<<23),
      reale(292248408,1310925625LL<<21),-reale(302358268,80333091LL<<22),
      reale(101329883,1625451155LL<<21),reale(168206436,256940945LL<<24),
      -reale(245462494,1698275235LL<<21),reale(106772813,575322475LL<<22),
      reale(20184277,1515722423LL<<21),-reale(15841583,115367503LL<<23),
      -reale(24343366,297803839LL<<21),reale(22619454,642864953LL<<22),
      -reale(5751128,1751200357LL<<21),reale(119914,0x778fad9290000LL),
      reale(77934432511LL,0x7a7ae451fe1d3LL),
      // C4[9], coeff of eps^13, polynomial in n of order 16
      -real(494538685723LL<<23),-reale(30244,7532247025LL<<19),
      reale(913230,2357276371LL<<20),-reale(8356271,5886749331LL<<19),
      reale(41054740,50726383LL<<21),-reale(125953300,8377060373LL<<19),
      reale(252781900,3961145001LL<<20),-reale(323011393,7392450487LL<<19),
      reale(214671336,735258085LL<<22),reale(38642307,2838366023LL<<19),
      -reale(221064383,2701482241LL<<20),reale(190191489,7753766309LL<<19),
      -reale(54203341,2021364059LL<<21),-reale(15936650,4639479773LL<<19),
      reale(7069294,1728459477LL<<20),reale(6475976,7904740225LL<<19),
      -reale(3243677,0x65d7af1058000LL),reale(77934432511LL,0x7a7ae451fe1d3LL),
      // C4[9], coeff of eps^12, polynomial in n of order 17
      -real(4941153649LL<<22),-real(2434362319LL<<26),
      -real(480183190319LL<<22),-reale(15428,422153761LL<<23),
      reale(492912,506448323LL<<22),-reale(4815395,177795021LL<<24),
      reale(25573504,64498885LL<<22),-reale(86374812,63633203LL<<23),
      reale(196725482,856584503LL<<22),-reale(303474922,105041487LL<<25),
      reale(296000607,1045914233LL<<22),-reale(124541003,470657541LL<<23),
      -reale(96946363,592229397LL<<22),reale(194787320,217061649LL<<24),
      -reale(139624521,484247315LL<<22),reale(48044895,435975913LL<<23),
      -reale(5082038,276187937LL<<22),-reale(749748,0x6069872020000LL),
      reale(77934432511LL,0x7a7ae451fe1d3LL),
      // C4[9], coeff of eps^11, polynomial in n of order 18
      -real(231323121LL<<20),-real(2351460757LL<<19),-real(912558841LL<<23),
      -real(0xdfda7610580000LL),-real(777314384543LL<<20),
      -reale(6590,3852961377LL<<19),reale(223861,17176789LL<<21),
      -reale(2346980,3623268951LL<<19),reale(13542533,3871010099LL<<20),
      -reale(50565862,7643343277LL<<19),reale(130735502,766383623LL<<22),
      -reale(239800507,7719641123LL<<19),reale(308448660,3395101317LL<<20),
      -reale(258446712,3844536697LL<<19),reale(99709743,227483207LL<<21),
      reale(54337873,5199140497LL<<19),-reale(105984135,215955881LL<<20),
      reale(67263140,627338299LL<<19),-reale(17110329,0x5fa94e648000LL),
      reale(77934432511LL,0x7a7ae451fe1d3LL),
      // C4[9], coeff of eps^10, polynomial in n of order 19
      -real(538707LL<<21),-real(1075491LL<<22),-real(9728097LL<<21),
      -real(3213907LL<<25),-real(333357375LL<<21),-real(1438804621LL<<22),
      -real(39246385997LL<<21),-real(379094211993LL<<23),
      reale(25645,1674653973LL<<21),-reale(290249,472854199LL<<22),
      reale(1830100,1274307463LL<<21),-reale(7588281,99130323LL<<24),
      reale(22282256,82312105LL<<21),-reale(48025833,432719649LL<<22),
      reale(76964476,1304326427LL<<21),-reale(91125940,162742323LL<<23),
      reale(77471536,1478654845LL<<21),-reale(44556474,1023100235LL<<22),
      reale(15423395,377918063LL<<21),-reale(2409905,0x7f0a0dc2b0000LL),
      reale(25978144170LL,0x7e28f6c5ff5f1LL),
      // C4[9], coeff of eps^9, polynomial in n of order 20
      -real(16575LL<<21),-real(226005LL<<19),-real(421083LL<<20),
      -real(3487431LL<<19),-real(1025715LL<<23),-real(90604825LL<<19),
      -real(308056405LL<<20),-real(5606626571LL<<19),-real(20270111449LL<<21),
      -real(0x30ab7cf8dddLL<<19),reale(15220,1707177905LL<<20),
      -reale(187210,7636838095LL<<19),reale(1297995,534056013LL<<22),
      -reale(6003229,1506461473LL<<19),reale(20010763,3942424887LL<<20),
      -reale(50026909,6827222547LL<<19),reale(95435950,2132760845LL<<21),
      -reale(138382128,8075045605LL<<19),reale(146522254,737992253LL<<20),
      -reale(96396219,7300467927LL<<19),reale(27713913,0x34f39e3ee8000LL),
      reale(77934432511LL,0x7a7ae451fe1d3LL),
      // C4[10], coeff of eps^29, polynomial in n of order 0
      real(14059LL<<19),real(0x168a4531304537LL),
      // C4[10], coeff of eps^28, polynomial in n of order 1
      -real(1004279LL<<22),-real(3373361LL<<19),reale(3807,0xdf0925caacfb9LL),
      // C4[10], coeff of eps^27, polynomial in n of order 2
      real(78580619LL<<24),-real(212705597LL<<23),real(705875469LL<<19),
      reale(59656,0xa639fabc960fdLL),
      // C4[10], coeff of eps^26, polynomial in n of order 3
      real(927832218729LL<<21),-real(204500125453LL<<22),
      -real(29157611613LL<<21),-real(0x66c4e2e4040000LL),
      reale(23087123,0x49a60b16d9e77LL),
      // C4[10], coeff of eps^25, polynomial in n of order 4
      real(26024288967LL<<27),-real(7678900515LL<<25),real(13514191015LL<<26),
      -real(31097026337LL<<25),real(89826688809LL<<21),
      reale(25583028,0x820b055e82c23LL),
      // C4[10], coeff of eps^24, polynomial in n of order 5
      reale(1328855,126349401LL<<24),-reale(550962,13774891LL<<26),
      reale(2464835,125518543LL<<24),-reale(784466,25625323LL<<25),
      -reale(93184,68528187LL<<24),-reale(52198,1190112709LL<<21),
      reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[10], coeff of eps^23, polynomial in n of order 6
      -reale(1114607,27405733LL<<26),-reale(4563722,53821803LL<<25),
      reale(3169393,348585LL<<27),reale(68182,92955763LL<<25),
      reale(1172595,45755337LL<<26),-reale(1088988,13585007LL<<25),
      reale(166307,46143431LL<<21),reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[10], coeff of eps^22, polynomial in n of order 7
      reale(5480278,504127481LL<<20),-reale(9293162,155326547LL<<21),
      -reale(197247,3072475525LL<<20),-reale(1644302,932629169LL<<22),
      reale(3811061,3287215741LL<<20),-reale(747954,323686257LL<<21),
      -reale(145848,3935467265LL<<20),-reale(106662,0xb8d6e5aaa0000LL),
      reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[10], coeff of eps^21, polynomial in n of order 8
      reale(22796753,23076841LL<<25),-reale(927290,180149865LL<<22),
      -reale(369606,74581989LL<<23),-reale(9303996,552920075LL<<22),
      reale(3036817,71115369LL<<24),reale(396000,898162707LL<<22),
      reale(2181010,484753161LL<<23),-reale(1556188,389640079LL<<22),
      reale(192540,3401040927LL<<18),reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[10], coeff of eps^20, polynomial in n of order 9
      reale(862955,1266777839LL<<21),reale(7027949,96012647LL<<24),
      reale(20762590,1932890753LL<<21),-reale(9559408,1057130891LL<<22),
      -reale(3064719,1040728173LL<<21),-reale(5129237,23711641LL<<23),
      reale(5760893,1279205669LL<<21),-reale(394463,240514009LL<<22),
      -reale(178786,1942994377LL<<21),-reale(217080,8651652815LL<<18),
      reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[10], coeff of eps^19, polynomial in n of order 10
      -reale(10913096,468931943LL<<23),-reale(57356320,139563275LL<<22),
      reale(21632622,17971173LL<<25),reale(12352870,1067519707LL<<22),
      reale(10546968,197307279LL<<23),-reale(16476123,321986815LL<<22),
      reale(466396,220388453LL<<24),reale(183297,876676071LL<<22),
      reale(4340035,31265157LL<<23),-reale(2266775,500956659LL<<22),
      reale(210337,0xe1ea7a84c0000LL),reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[10], coeff of eps^18, polynomial in n of order 11
      reale(183220667,2590575043LL<<20),reale(3573393,1902991101LL<<21),
      -reale(38433982,657724943LL<<20),-reale(39892891,403988263LL<<23),
      reale(42677900,967722655LL<<20),reale(4292702,1711217099LL<<21),
      -reale(2792039,799969587LL<<20),-reale(14767346,746757159LL<<22),
      reale(7703673,1133060475LL<<20),reale(747527,637790873LL<<21),
      -reale(45502,3704918615LL<<20),-reale(458872,0xc21d355260000LL),
      reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[10], coeff of eps^17, polynomial in n of order 12
      -reale(61780842,49135749LL<<25),-reale(196091506,391376453LL<<23),
      reale(232013693,187926637LL<<24),-reale(59475550,301219495LL<<23),
      -reale(46331600,62864215LL<<26),-reale(5439903,151048009LL<<23),
      reale(49627120,102515675LL<<24),-reale(16690048,509576107LL<<23),
      -reale(7354945,21733079LL<<25),-reale(3769052,366616397LL<<23),
      reale(9145462,214930505LL<<24),-reale(3305318,371590575LL<<23),
      reale(189374,6271289399LL<<19),reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[10], coeff of eps^16, polynomial in n of order 13
      -reale(246951312,552772347LL<<22),reale(295555190,29721595LL<<24),
      -reale(151972143,664869293LL<<22),-reale(114414430,423169395LL<<23),
      reale(248915402,492058657LL<<22),-reale(140322148,99500631LL<<25),
      -reale(15757705,299151505LL<<22),reale(29401843,368167099LL<<23),
      reale(29725213,39057725LL<<22),-reale(34936824,2445655LL<<24),
      reale(5290998,483472011LL<<22),reale(3412892,270211305LL<<23),
      reale(939828,308185177LL<<22),-reale(1058440,2262901433LL<<19),
      reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[10], coeff of eps^15, polynomial in n of order 14
      -reale(29237793,21929809LL<<24),reale(96597143,85827693LL<<23),
      -reale(210653294,75090197LL<<25),reale(297719766,264531499LL<<23),
      -reale(232859751,332419LL<<24),reale(779198,466262825LL<<23),
      reale(210565738,49075321LL<<26),-reale(210231291,35636249LL<<23),
      reale(60435795,147172683LL<<24),reale(30790678,95503589LL<<23),
      -reale(8341137,27440903LL<<25),-reale(25891285,147600733LL<<23),
      reale(19770912,119140889LL<<24),-reale(4475853,348372575LL<<23),
      reale(24304,4909664935LL<<19),reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[10], coeff of eps^14, polynomial in n of order 15
      -reale(326980,1465789373LL<<20),reale(3379554,1566468779LL<<21),
      -reale(19029758,226591575LL<<20),reale(68313947,940737655LL<<22),
      -reale(165836985,3033756273LL<<20),reale(273579872,472941105LL<<21),
      -reale(286670501,2169744907LL<<20),reale(131674848,489609853LL<<23),
      reale(102478771,1504412891LL<<20),-reale(220713363,225877065LL<<21),
      reale(153302553,1201451329LL<<20),-reale(29968476,1046042243LL<<22),
      -reale(18498599,2914872793LL<<20),reale(4637884,270502717LL<<21),
      reale(6604171,2668065421LL<<20),-reale(2936921,0x8973648be0000LL),
      reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[10], coeff of eps^13, polynomial in n of order 16
      real(8181919521LL<<26),reale(4651,463423847LL<<22),
      -reale(165627,528682553LL<<23),reale(1821092,755660373LL<<22),
      -reale(11021646,91392285LL<<24),reale(43145010,992272131LL<<22),
      -reale(116748177,310953403LL<<23),reale(223068773,47271409LL<<22),
      -reale(294932999,99296991LL<<25),reale(242263024,286305695LL<<22),
      -reale(60276785,30271037LL<<23),-reale(125363778,717272947LL<<22),
      reale(182026841,37372833LL<<24),-reale(116787755,417593029LL<<22),
      reale(36759949,346012225LL<<23),-reale(3061422,962217431LL<<22),
      -reale(731281,0xaaf6b13240000LL),reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[10], coeff of eps^12, polynomial in n of order 17
      real(777809483LL<<21),real(436668683LL<<25),real(99139014933LL<<21),
      real(0x1cfc4bfd58dLL<<22),-reale(70023,1623299233LL<<21),
      reale(822756,446933025LL<<23),-reale(5376526,2111656919LL<<21),
      reale(23042396,297910775LL<<22),-reale(69630161,297782669LL<<21),
      reale(153266731,112309515LL<<24),-reale(247130955,1577554627LL<<21),
      reale(284460705,580739169LL<<22),-reale(212091093,1801700473LL<<21),
      reale(61297082,319619083LL<<23),reale(65462218,2096893009LL<<21),
      -reale(98426842,1047683893LL<<22),reale(59152561,1235484315LL<<21),
      -reale(14780753,0xb5d74354c0000LL),
      reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[10], coeff of eps^11, polynomial in n of order 18
      real(1233981LL<<23),real(14104237LL<<22),real(6201077LL<<26),
      real(933195507LL<<22),real(6966040851LL<<23),real(592370721657LL<<22),
      -reale(22184,189431713LL<<24),reale(279989,474035391LL<<22),
      -reale(1985627,52832535LL<<23),reale(9357698,635528005LL<<22),
      -reale(31645438,92987147LL<<25),reale(79909927,996806539LL<<22),
      -reale(153562851,494252097LL<<23),reale(225351987,543734289LL<<22),
      -reale(249473559,252214923LL<<24),reale(201676475,478217047LL<<22),
      -reale(111804841,353712747LL<<23),reale(37701632,700509149LL<<22),
      -reale(5783773,3281237837LL<<18),reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[10], coeff of eps^10, polynomial in n of order 19
      real(57057LL<<20),real(126819LL<<21),real(1284843LL<<20),
      real(478667LL<<24),real(56414325LL<<20),real(279062861LL<<21),
      real(8810413183LL<<20),real(99625441377LL<<22),
      -reale(3997,1800115191LL<<20),reale(54510,559965495LL<<21),
      -reale(421909,1796318189LL<<20),reale(2196607,410595787LL<<23),
      -reale(8328804,1896277603LL<<20),reale(24012916,1506461473LL<<21),
      -reale(53875133,2685050457LL<<20),reale(94820235,193579723LL<<22),
      -reale(129680615,3269639887LL<<20),reale(131955714,608596747LL<<21),
      -reale(84828673,2009615045LL<<20),reale(24099054,0xf664899ae0000LL),
      reale(86138056986LL,0x5ef39e09c8055LL),
      // C4[11], coeff of eps^29, polynomial in n of order 0
      -real(255169LL<<19),real(0xbdc79d6e266b55fLL),
      // C4[11], coeff of eps^28, polynomial in n of order 1
      -real(535829LL<<26),real(6461547LL<<20),real(0x56e2cdab4666fea1LL),
      // C4[11], coeff of eps^27, polynomial in n of order 2
      -real(54075943LL<<25),-real(11012147LL<<24),-real(184884229LL<<19),
      reale(65338,0x3c271ece8bf8fLL),
      // C4[11], coeff of eps^26, polynomial in n of order 3
      -real(29189823LL<<30),real(157366885LL<<32),-real(637753597LL<<30),
      real(13332470307LL<<23),reale(19666808,0xb9ff38da93b23LL),
      // C4[11], coeff of eps^25, polynomial in n of order 4
      -reale(768828,16543417LL<<28),reale(2405043,41201001LL<<26),
      -reale(595679,17511625LL<<27),-reale(94147,42169149LL<<26),
      -reale(60455,661597895LL<<21),reale(94341681461LL,0x436c57c191ed7LL),
      // C4[11], coeff of eps^24, polynomial in n of order 5
      -reale(5042070,2793567LL<<28),reale(2454743,3154771LL<<30),
      reale(191058,8757223LL<<28),reale(1233521,5992667LL<<29),
      -reale(1001395,9125891LL<<28),reale(137539,51052897LL<<22),
      reale(94341681461LL,0x436c57c191ed7LL),
      // C4[11], coeff of eps^23, polynomial in n of order 6
      -reale(7620321,6390387LL<<27),-reale(1118882,49853273LL<<26),
      -reale(2175696,13938625LL<<28),reale(3557797,45648113LL<<26),
      -reale(479218,13589073LL<<27),-reale(128928,23280229LL<<26),
      -reale(115227,1709406351LL<<21),reale(94341681461LL,0x436c57c191ed7LL),
      // C4[11], coeff of eps^22, polynomial in n of order 7
      reale(3030693,3978133LL<<30),reale(1618004,874507LL<<32),
      -reale(9217736,134985LL<<30),reale(1767041,97063LL<<33),
      reale(345531,3069873LL<<30),reale(2240563,263433LL<<32),
      -reale(1400217,895853LL<<30),reale(154222,296573467LL<<23),
      reale(94341681461LL,0x436c57c191ed7LL),
      // C4[11], coeff of eps^21, polynomial in n of order 8
      reale(304504,49998275LL<<26),reale(21950325,110791689LL<<23),
      -reale(5005332,65785063LL<<24),-reale(3038456,102319349LL<<23),
      -reale(5977063,34913149LL<<25),reale(5022754,485487661LL<<23),
      -reale(45293,7681997LL<<24),-reale(123970,179449809LL<<23),
      -reale(222792,7672407751LL<<18),reale(94341681461LL,0x436c57c191ed7LL),
      // C4[11], coeff of eps^20, polynomial in n of order 9
      -reale(56432361,120691497LL<<25),reale(6246807,9955417LL<<28),
      reale(11410351,83254233LL<<25),reale(14692579,49664915LL<<26),
      -reale(13833928,124401461LL<<25),-reale(1245123,9835207LL<<27),
      -reale(339985,114545011LL<<25),reale(4291293,22713457LL<<26),
      -reale(1980685,98627585LL<<25),reale(159775,7030690975LL<<19),
      reale(94341681461LL,0x436c57c191ed7LL),
      // C4[11], coeff of eps^19, polynomial in n of order 10
      reale(40826627,234278683LL<<24),-reale(19124677,161584861LL<<23),
      -reale(51024100,50981393LL<<26),reale(30646271,384869645LL<<23),
      reale(9815197,6859997LL<<24),reale(639236,244693975LL<<23),
      -reale(14951689,12090449LL<<25),reale(5916250,151054657LL<<23),
      reale(1073676,226322463LL<<24),reale(80953,380993803LL<<23),
      -reale(451111,0xff79096c0000LL),reale(94341681461LL,0x436c57c191ed7LL),
      // C4[11], coeff of eps^18, polynomial in n of order 11
      -reale(233788807,3535193LL<<28),reale(177892093,3762413LL<<30),
      -reale(5486729,8981851LL<<28),-reale(45008759,222901LL<<32),
      -reale(22295157,5977845LL<<28),reale(46499690,3347131LL<<30),
      -reale(8381947,991351LL<<28),-reale(7636513,1723229LL<<31),
      -reale(5108235,6476849LL<<28),reale(8568922,940153LL<<30),
      -reale(2769555,11314291LL<<28),reale(126172,1159668425LL<<21),
      reale(94341681461LL,0x436c57c191ed7LL),
      // C4[11], coeff of eps^17, polynomial in n of order 12
      reale(246666787,23370677LL<<26),-reale(50685638,204720607LL<<24),
      -reale(179803952,51059789LL<<25),reale(226753224,100010811LL<<24),
      -reale(83690537,703961LL<<27),-reale(36110826,15584139LL<<24),
      reale(17880019,26951173LL<<25),reale(35367319,73259919LL<<24),
      -reale(29730133,51577369LL<<26),reale(2029349,105583177LL<<24),
      reale(3224077,120316375LL<<25),reale(1158582,83501667LL<<24),
      -reale(999784,6146420159LL<<19),reale(94341681461LL,0x436c57c191ed7LL),
      // C4[11], coeff of eps^16, polynomial in n of order 13
      reale(135472555,2919565LL<<26),-reale(240891001,9823619LL<<28),
      reale(277187915,48314475LL<<26),-reale(153860890,22130685LL<<27),
      -reale(78071452,50966567LL<<26),reale(224257271,6520287LL<<29),
      -reale(168898235,23643785LL<<26),reale(25365615,30758325LL<<27),
      reale(33991594,22223845LL<<26),-reale(1325267,13358465LL<<28),
      -reale(26274549,1352253LL<<26),reale(17195323,12709799LL<<27),
      -reale(3493469,55138895LL<<26),-reale(37171,2996514251LL<<20),
      reale(94341681461LL,0x436c57c191ed7LL),
      // C4[11], coeff of eps^15, polynomial in n of order 14
      reale(8369149,65829073LL<<25),-reale(34468304,77612041LL<<24),
      reale(98238486,50466661LL<<26),-reale(197855127,257258223LL<<24),
      reale(275634083,126808451LL<<25),-reale(237329411,109823349LL<<24),
      reale(57709083,378039LL<<27),reale(144028485,10992165LL<<24),
      -reale(208082193,86131531LL<<25),reale(120116321,182851999LL<<24),
      -reale(12733337,27687817LL<<26),-reale(18886416,178008391LL<<24),
      reale(2557866,23705959LL<<25),reale(6572718,173475635LL<<24),
      -reale(2666354,4022017967LL<<19),reale(94341681461LL,0x436c57c191ed7LL),
      // C4[11], coeff of eps^14, polynomial in n of order 15
      reale(54399,8224347LL<<28),-reale(665621,1410497LL<<30),
      reale(4527846,10561865LL<<28),-reale(20181039,1593975LL<<31),
      reale(63299017,4532479LL<<28),-reale(144047710,3737571LL<<30),
      reale(238046961,3527085LL<<28),-reale(274611219,485845LL<<32),
      reale(189061064,3080067LL<<28),-reale(9459768,127989LL<<30),
      -reale(141083601,3627343LL<<28),reale(166868825,1278147LL<<31),
      -reale(97711641,16702617LL<<28),reale(28303864,4120681LL<<30),
      -reale(1707991,14300715LL<<28),-reale(691965,964491519LL<<21),
      reale(94341681461LL,0x436c57c191ed7LL),
      // C4[11], coeff of eps^13, polynomial in n of order 16
      -real(394848061LL<<27),-real(277855615551LL<<23),
      reale(21507,221049445LL<<24),-reale(280152,15918397LL<<23),
      reale(2046623,76965257LL<<25),-reale(9908745,30179611LL<<23),
      reale(34287090,9035647LL<<24),-reale(88042794,304571673LL<<23),
      reale(170266683,41403331LL<<26),-reale(246546803,514564215LL<<23),
      reale(257558635,22204697LL<<24),-reale(171596509,74164853LL<<23),
      reale(32098211,100286083LL<<25),reale(71621283,185724333LL<<23),
      -reale(91026815,208301517LL<<24),reale(52421624,501338287LL<<23),
      -reale(12919309,7987587551LL<<18),reale(94341681461LL,0x436c57c191ed7LL),
      // C4[11], coeff of eps^12, polynomial in n of order 17
      -real(2506701LL<<25),-real(1595211LL<<29),-real(414133331LL<<25),
      -real(9611154693LL<<26),reale(6318,129560535LL<<25),
      -reale(88018,7994433LL<<27),reale(693686,99032081LL<<25),
      -reale(3663195,37640223LL<<26),reale(14022738,83451835LL<<25),
      -reale(40598902,6803915LL<<28),reale(90961965,119128629LL<<25),
      -reale(159220781,788729LL<<26),reale(217217490,112380639LL<<25),
      -reale(227284915,26366059LL<<27),reale(176075660,9208089LL<<25),
      -reale(94610548,45670931LL<<26),reale(31201351,21556291LL<<25),
      -reale(4712704,700509149LL<<19),reale(94341681461LL,0x436c57c191ed7LL),
      // C4[11], coeff of eps^11, polynomial in n of order 18
      -real(13041LL<<24),-real(166957LL<<23),-real(82777LL<<27),
      -real(14154867LL<<23),-real(121102751LL<<24),-real(11919970777LL<<23),
      real(140288886837LL<<25),-reale(15649,252654239LL<<23),
      reale(133731,225409843LL<<24),-reale(773734,115698949LL<<23),
      reale(3285982,23309223LL<<26),-reale(10716783,181102411LL<<23),
      reale(27557442,232845957LL<<24),-reale(56645854,420384689LL<<23),
      reale(93299054,125728615LL<<25),-reale(121534295,132369527LL<<23),
      reale(119605179,120525655LL<<24),-reale(75403265,163638237LL<<23),
      reale(21207168,6304582341LL<<18),reale(94341681461LL,0x436c57c191ed7LL),
      // C4[12], coeff of eps^29, polynomial in n of order 0
      real(2113LL<<23),real(0x495846bc80a035LL),
      // C4[12], coeff of eps^28, polynomial in n of order 1
      -real(5059597LL<<25),-real(23775299LL<<22),
      reale(61953,0x75e619a89ce07LL),
      // C4[12], coeff of eps^27, polynomial in n of order 2
      real(30823201LL<<29),-real(55301563LL<<28),real(131431881LL<<24),
      reale(497138,0xbe8dd4238d2e7LL),
      // C4[12], coeff of eps^26, polynomial in n of order 3
      real(8059635627LL<<28),-real(757042391LL<<29),-real(311216327LL<<28),
      -real(7273579LL<<33),reale(21376966,0x1d2a1f8b6ccdLL),
      // C4[12], coeff of eps^25, polynomial in n of order 4
      reale(590308,751003LL<<30),reale(77521,16047653LL<<28),
      reale(426657,125003LL<<29),-reale(306166,5244457LL<<28),
      reale(37995,207060411LL<<24),reale(34181768645LL,0x62a1b07dc9473LL),
      // C4[12], coeff of eps^24, polynomial in n of order 5
      -reale(1599658,2394579LL<<27),-reale(2671318,5123391LL<<29),
      reale(3256460,16377243LL<<27),-reale(261261,3982303LL<<28),
      -reale(106562,1204279LL<<27),-reale(120793,1029973LL<<24),
      reale(102545305936LL,0x27e511795bd59LL),
      // C4[12], coeff of eps^23, polynomial in n of order 6
      reale(1248773,4542469LL<<29),-reale(2889741,9813249LL<<28),
      reale(234885,3135591LL<<30),reale(66922,9908313LL<<28),
      reale(755418,635863LL<<29),-reale(419245,13200621LL<<28),
      reale(41213,192739239LL<<24),reale(34181768645LL,0x62a1b07dc9473LL),
      // C4[12], coeff of eps^22, polynomial in n of order 7
      reale(20885911,4938503LL<<28),-reale(1107830,1234733LL<<29),
      -reale(2370377,3771643LL<<28),-reale(6561451,624527LL<<30),
      reale(4279851,10797315LL<<28),reale(210660,3761905LL<<29),
      -reale(68724,2033407LL<<28),-reale(224555,1152577LL<<29),
      reale(102545305936LL,0x27e511795bd59LL),
      // C4[12], coeff of eps^21, polynomial in n of order 8
      -reale(5562062,38325LL<<31),reale(7741765,4470897LL<<28),
      reale(17399328,6149297LL<<29),-reale(10890962,10744893LL<<28),
      -reale(2368414,446197LL<<30),-reale(891562,9146315LL<<28),
      reale(4183586,393403LL<<29),-reale(1730085,6072185LL<<28),
      reale(120797,18742483LL<<24),reale(102545305936LL,0x27e511795bd59LL),
      // C4[12], coeff of eps^20, polynomial in n of order 9
      reale(3332722,179104103LL<<24),-reale(54245156,21887465LL<<27),
      reale(18428910,34326153LL<<24),reale(12293411,106053413LL<<25),
      reale(4024929,78680811LL<<24),-reale(14528270,1262953LL<<26),
      reale(4350569,36952333LL<<24),reale(1251271,92345015LL<<25),
      reale(191236,5049199LL<<24),-reale(439124,1983321823LL<<21),
      reale(102545305936LL,0x27e511795bd59LL),
      // C4[12], coeff of eps^19, polynomial in n of order 10
      reale(117817828,9756529LL<<27),reale(29304818,21859033LL<<26),
      -reale(33857646,3348243LL<<29),-reale(34756825,30241097LL<<26),
      reale(40576649,11012471LL<<27),-reale(1809964,37385419LL<<26),
      -reale(7014724,9945043LL<<28),-reale(6163099,62399597LL<<26),
      reale(7950550,2557949LL<<27),-reale(2323999,3159279LL<<26),
      reale(80031,1030811061LL<<22),reale(102545305936LL,0x27e511795bd59LL),
      // C4[12], coeff of eps^18, polynomial in n of order 11
      reale(37677439,43610729LL<<26),-reale(212417438,31724009LL<<27),
      reale(188774384,60946099LL<<26),-reale(37276694,6182933LL<<29),
      -reale(44097735,31570179LL<<26),reale(5696664,10497345LL<<27),
      reale(38054298,7154503LL<<26),-reale(24525159,12952085LL<<28),
      -reale(350073,57822319LL<<26),reale(2901654,18012267LL<<27),
      reale(1319354,63457243LL<<26),-reale(941373,59576227LL<<26),
      reale(102545305936LL,0x27e511795bd59LL),
      // C4[12], coeff of eps^17, polynomial in n of order 12
      -reale(253394431,1462439LL<<28),reale(237669216,409221LL<<26),
      -reale(76296320,28335185LL<<27),-reale(133872864,17217849LL<<26),
      reale(218174046,2622387LL<<29),-reale(127998192,57914967LL<<26),
      reale(363472,30718057LL<<27),reale(32681168,4189163LL<<26),
      reale(4677048,16088179LL<<28),-reale(25857930,7142835LL<<26),
      reale(14914891,9312419LL<<27),-reale(2731797,52301425LL<<26),
      -reale(76352,726189181LL<<22),reale(102545305936LL,0x27e511795bd59LL),
      // C4[12], coeff of eps^16, polynomial in n of order 13
      -reale(53643409,97684423LL<<25),reale(127408278,3174879LL<<27),
      -reale(219370358,126672641LL<<25),reale(262176902,49024297LL<<26),
      -reale(182579118,132254331LL<<25),-reale(3956056,15289739LL<<28),
      reale(167880537,57011019LL<<25),-reale(188895775,46555265LL<<26),
      reale(91621970,25449425LL<<25),-reale(762367,26232331LL<<27),
      -reale(18049661,12932969LL<<25),reale(839158,10679509LL<<26),
      reale(6440415,29973277LL<<25),-reale(2428550,982597961LL<<22),
      reale(102545305936LL,0x27e511795bd59LL),
      // C4[12], coeff of eps^15, polynomial in n of order 14
      -reale(1786573,13634499LL<<27),reale(8939902,21088027LL<<26),
      -reale(31907799,6174271LL<<28),reale(84216248,4944333LL<<26),
      -reale(166330228,11364729LL<<27),reale(242706491,8863071LL<<26),
      -reale(246937724,7698133LL<<29),reale(139651898,50060433LL<<26),
      reale(29493809,19297617LL<<27),-reale(148014628,18656733LL<<26),
      reale(151165884,622571LL<<28),-reale(81883041,55488299LL<<26),
      reale(21903841,15379355LL<<27),-reale(792996,14574745LL<<26),
      -reale(644309,457907141LL<<22),reale(102545305936LL,0x27e511795bd59LL),
      // C4[12], coeff of eps^14, polynomial in n of order 15
      -reale(6504,28793619LL<<26),reale(93075,33271365LL<<27),
      -reale(752118,6462873LL<<26),reale(4061558,11832697LL<<28),
      -reale(15840997,35193887LL<<26),reale(46482714,19239327LL<<27),
      -reale(104709924,13039269LL<<26),reale(181860520,7828435LL<<29),
      -reale(240159499,36173099LL<<26),reale(229992094,10037497LL<<27),
      -reale(136854274,43911089LL<<26),reale(9990763,2550227LL<<28),
      reale(74520212,5689801LL<<26),-reale(84057599,709549LL<<27),
      reale(46786776,54603587LL<<26),-reale(11406945,39298831LL<<26),
      reale(102545305936LL,0x27e511795bd59LL),
      // C4[12], coeff of eps^13, polynomial in n of order 16
      real(1030055LL<<30),real(829418525LL<<26),-real(19924010015LL<<27),
      reale(9050,10804695LL<<26),-reale(78488,9283787LL<<28),
      reale(459151,22444081LL<<26),-reale(1962716,17985613LL<<27),
      reale(6408338,7820523LL<<26),-reale(16395176,1626457LL<<29),
      reale(33311385,35536325LL<<26),-reale(53946341,7054843LL<<27),
      reale(69201696,61410431LL<<26),-reale(69012103,3773337LL<<28),
      reale(51544754,7834329LL<<26),-reale(26961871,12889641LL<<27),
      reale(8722958,26104467LL<<26),-reale(1300056,320360129LL<<22),
      reale(34181768645LL,0x62a1b07dc9473LL),
      // C4[12], coeff of eps^12, polynomial in n of order 17
      real(127075LL<<24),real(91195LL<<28),real(26902525LL<<24),
      real(715607165LL<<25),-real(73094160425LL<<24),
      reale(4440,35913265LL<<26),-reale(41519,978831LL<<24),
      reale(264211,112928967LL<<25),-reale(1241795,175695285LL<<24),
      reale(4515620,18853435LL<<27),-reale(13069247,261014043LL<<24),
      reale(30619380,129358865LL<<25),-reale(58537051,226171969LL<<24),
      reale(91194564,65482939LL<<26),-reale(113993206,58979239LL<<24),
      reale(109036979,115740763LL<<25),-reale(67602927,138149837LL<<24),
      reale(18850816,700509149LL<<21),reale(102545305936LL,0x27e511795bd59LL),
      // C4[13], coeff of eps^29, polynomial in n of order 0
      -real(634219LL<<23),reale(3193,0x402148867236bLL),
      // C4[13], coeff of eps^28, polynomial in n of order 1
      -real(400561LL<<32),real(1739049LL<<27),reale(66909,0xbcc54ee94d445LL),
      // C4[13], coeff of eps^27, polynomial in n of order 2
      -real(6387996953LL<<29),-real(3461245957LL<<28),-real(49206438547LL<<24),
      reale(286172946,0xcc6f5fc7e64c9LL),
      // C4[13], coeff of eps^26, polynomial in n of order 3
      real(7296571113LL<<30),reale(10661,1488313LL<<31),
      -reale(6836,2507629LL<<30),real(103233906747LL<<25),
      reale(900397808,0x384bb07b32421LL),
      // C4[13], coeff of eps^25, polynomial in n of order 4
      -reale(1030602,1434287LL<<30),reale(976249,6303335LL<<28),
      -reale(29214,7243007LL<<29),-reale(27193,4360723LL<<28),
      -reale(41363,170006437LL<<24),reale(36916310137LL,0x41f43bb0c949LL),
      // C4[13], coeff of eps^24, polynomial in n of order 5
      -reale(2597630,109963LL<<31),-reale(46366,88065LL<<33),
      real(835763379LL<<31),reale(754229,667751LL<<32),
      -reale(376195,1000319LL<<31),reale(33027,62908623LL<<26),
      reale(36916310137LL,0x41f43bb0c949LL),
      // C4[13], coeff of eps^23, polynomial in n of order 6
      reale(1907767,7512493LL<<29),-reale(1322539,10099505LL<<28),
      -reale(6889396,2784065LL<<30),reale(3566231,12064393LL<<28),
      reale(392016,1668111LL<<29),-reale(16024,9677725LL<<28),
      -reale(223530,46890859LL<<24),reale(110748930411LL,0xc5dcb3125bdbLL),
      // C4[13], coeff of eps^22, polynomial in n of order 7
      reale(2768819,1533979LL<<30),reale(18682895,1051157LL<<31),
      -reale(7962826,2061455LL<<30),-reale(3008388,501585LL<<32),
      -reale(1419492,411945LL<<30),reale(4033867,1208903LL<<31),
      -reale(1511425,98131LL<<30),reale(90538,115806565LL<<25),
      reale(110748930411LL,0xc5dcb3125bdbLL),
      // C4[13], coeff of eps^21, polynomial in n of order 8
      -reale(51332124,214119LL<<31),reale(7579765,3153587LL<<28),
      reale(12475441,1655707LL<<29),reale(7005177,5256105LL<<28),
      -reale(13679833,119207LL<<30),reale(3016909,4530623LL<<28),
      reale(1323928,2024201LL<<29),reale(284896,6925237LL<<28),
      -reale(424636,138679005LL<<24),reale(110748930411LL,0xc5dcb3125bdbLL),
      // C4[13], coeff of eps^20, polynomial in n of order 9
      reale(47250711,14679LL<<32),-reale(18472981,62575LL<<35),
      -reale(42459575,893863LL<<32),reale(33307537,106651LL<<33),
      reale(3060800,842635LL<<32),-reale(5867540,102671LL<<34),
      -reale(6941741,849331LL<<32),reale(7324844,423881LL<<33),
      -reale(1953157,771073LL<<32),reale(46148,28524089LL<<27),
      reale(110748930411LL,0xc5dcb3125bdbLL),
      // C4[13], coeff of eps^19, polynomial in n of order 10
      -reale(218954922,27801141LL<<27),reale(144947317,36456347LL<<26),
      -reale(2491117,129025LL<<29),-reale(43746541,53566187LL<<26),
      -reale(5414513,33128531LL<<27),reale(38475112,39418671LL<<26),
      -reale(19653214,3660993LL<<28),-reale(2031714,8235735LL<<26),
      reale(2517568,31068687LL<<27),reale(1433299,8158787LL<<26),
      -reale(884985,911850811LL<<22),reale(110748930411LL,0xc5dcb3125bdbLL),
      // C4[13], coeff of eps^18, polynomial in n of order 11
      reale(186784429,10521821LL<<28),-reale(7066121,6171767LL<<29),
      -reale(168709085,159265LL<<28),reale(199772613,1997709LL<<31),
      -reale(91000398,5796399LL<<28),-reale(16385586,3500385LL<<29),
      reale(28845005,1198547LL<<28),reale(9523912,3994797LL<<30),
      -reale(24921512,7172347LL<<28),reale(12920997,2065141LL<<29),
      -reale(2137442,11262329LL<<28),-reale(100773,90157665LL<<23),
      reale(110748930411LL,0xc5dcb3125bdbLL),
      // C4[13], coeff of eps^17, polynomial in n of order 12
      reale(153014747,10291963LL<<28),-reale(229746959,27043849LL<<26),
      reale(237324258,14238909LL<<27),-reale(127910449,9268979LL<<26),
      -reale(52661288,2811671LL<<29),reale(178449477,48064707LL<<26),
      -reale(166899831,11458293LL<<27),reale(67870692,24951769LL<<26),
      reale(7326612,206249LL<<28),-reale(16569622,39442673LL<<26),
      -reale(550002,21806887LL<<27),reale(6246699,62490405LL<<26),
      -reale(2219585,747027389LL<<22),reale(110748930411LL,0xc5dcb3125bdbLL),
      // C4[13], coeff of eps^16, polynomial in n of order 13
      reale(15145062,3114639LL<<29),-reale(45473383,71569LL<<31),
      reale(104280194,4043033LL<<29),-reale(182691434,3191599LL<<30),
      reale(238813543,4302931LL<<29),-reale(215430056,686779LL<<32),
      reale(95643898,4170781LL<<29),reale(58502156,871831LL<<30),
      -reale(149008930,6169513LL<<29),reale(135928257,1117477LL<<31),
      -reale(68778720,227103LL<<29),reale(17013972,3743517LL<<30),
      -reale(171458,5381733LL<<29),-reale(594747,43724235LL<<24),
      reale(110748930411LL,0xc5dcb3125bdbLL),
      // C4[13], coeff of eps^15, polynomial in n of order 14
      reale(268265,12727175LL<<27),-reale(1599130,17232215LL<<26),
      reale(6942204,4883571LL<<28),-reale(22914299,20494129LL<<26),
      reale(58880733,8011269LL<<27),-reale(118985431,7979051LL<<26),
      reale(188592645,4054545LL<<29),-reale(229762161,35295621LL<<26),
      reale(203148724,31300867LL<<27),-reale(107385077,53637247LL<<26),
      -reale(6680614,2406191LL<<28),reale(75281884,8527271LL<<26),
      -reale(77627899,32310399LL<<27),reale(42028799,34263981LL<<26),
      -reale(10160264,35032709LL<<22),reale(110748930411LL,0xc5dcb3125bdbLL),
      // C4[13], coeff of eps^14, polynomial in n of order 15
      real(8350913025LL<<28),-reale(8241,2495877LL<<29),
      reale(78008,13111987LL<<28),-reale(500800,4045265LL<<30),
      reale(2364509,9424021LL<<28),-reale(8593646,4773407LL<<29),
      reale(24709323,11418567LL<<28),-reale(57114100,2066875LL<<31),
      reale(106928260,4889705LL<<28),-reale(162113251,5033977LL<<29),
      reale(197269922,8596123LL<<28),-reale(188736396,4070811LL<<30),
      reale(136566807,16720509LL<<28),-reale(69783667,938643LL<<29),
      reale(22203894,1359919LL<<28),-reale(3271109,212531129LL<<23),
      reale(110748930411LL,0xc5dcb3125bdbLL),
      // C4[13], coeff of eps^13, polynomial in n of order 16
      -real(94185LL<<30),-real(86179275LL<<26),real(2372802705LL<<27),
      -real(83726038305LL<<26),reale(12668,1555717LL<<28),
      -reale(87922,39994007LL<<26),reale(452934,19637187LL<<27),
      -reale(1815855,62281965LL<<26),reale(5835571,1574167LL<<29),
      -reale(15318374,24668899LL<<26),reale(33211265,14617205LL<<27),
      -reale(59722012,27257657LL<<26),reale(88729847,57943LL<<28),
      -reale(107054489,21433519LL<<26),reale(99917523,12239271LL<<27),
      -reale(61060708,48513541LL<<26),reale(16900731,943456205LL<<22),
      reale(110748930411LL,0xc5dcb3125bdbLL),
      // C4[14], coeff of eps^29, polynomial in n of order 0
      real(41LL<<28),real(0x3fbc634a12a6b1LL),
      // C4[14], coeff of eps^28, polynomial in n of order 1
      -real(6907093LL<<31),-real(59887787LL<<28),
      reale(5739014,0x909af11944e4bLL),
      // C4[14], coeff of eps^27, polynomial in n of order 2
      reale(3432,499601LL<<33),-real(2083199471LL<<32),real(3406572267LL<<28),
      reale(307370942,0xdb94118adae9fLL),
      // C4[14], coeff of eps^26, polynomial in n of order 3
      reale(287986,4314073LL<<29),reale(5344,3636147LL<<30),
      -reale(6205,2906637LL<<29),-reale(13964,12467885LL<<26),
      reale(13216950542LL,0xe1def252c54b5LL),
      // C4[14], coeff of eps^25, polynomial in n of order 4
      -reale(258061,515595LL<<33),-reale(74790,1657665LL<<31),
      reale(745027,493173LL<<32),-reale(337382,84843LL<<31),
      reale(26418,5099583LL<<27),reale(39650851628LL,0xa59cd6f84fe1fLL),
      // C4[14], coeff of eps^24, polynomial in n of order 5
      -reale(100052,3082133LL<<30),-reale(6991386,428305LL<<32),
      reale(2902871,3549453LL<<30),reale(514674,1320943LL<<31),
      reale(32543,4070319LL<<30),-reale(220557,2292103LL<<27),
      reale(118952554885LL,0xf0d684e8efa5dLL),
      // C4[14], coeff of eps^23, polynomial in n of order 6
      reale(6249633,975799LL<<32),-reale(1750517,1286063LL<<31),
      -reale(1090661,209219LL<<33),-reale(631632,1802089LL<<31),
      reale(1285387,761149LL<<32),-reale(440347,2020899LL<<31),
      reale(22303,14762615LL<<27),reale(39650851628LL,0xa59cd6f84fe1fLL),
      // C4[14], coeff of eps^22, polynomial in n of order 7
      -reale(1155507,7367607LL<<29),reale(11090657,3295693LL<<30),
      reale(9416360,3921899LL<<29),-reale(12562252,1614097LL<<31),
      reale(1905484,7990669LL<<29),reale(1324142,2135535LL<<30),
      reale(362851,6226223LL<<29),-reale(408795,45924241LL<<26),
      reale(118952554885LL,0xf0d684e8efa5dLL),
      // C4[14], coeff of eps^21, polynomial in n of order 8
      -reale(2556392,83451LL<<35),-reale(45924405,628445LL<<32),
      reale(25722566,76303LL<<33),reale(6427311,738073LL<<32),
      -reale(4460754,79355LL<<34),-reale(7474566,842481LL<<32),
      reale(6714086,421381LL<<33),-reale(1643964,835835LL<<32),
      reale(21175,2825543LL<<28),reale(118952554885LL,0xf0d684e8efa5dLL),
      // C4[14], coeff of eps^20, polynomial in n of order 9
      reale(101794762,1213867LL<<31),reale(21384252,53331LL<<34),
      -reale(38294895,814203LL<<31),-reale(14666563,397319LL<<32),
      reale(37283642,1395551LL<<31),-reale(15279397,125869LL<<33),
      -reale(3174330,433863LL<<31),reale(2115734,458067LL<<32),
      reale(1510128,1624339LL<<31),-reale(831539,10478291LL<<28),
      reale(118952554885LL,0xf0d684e8efa5dLL),
      // C4[14], coeff of eps^19, polynomial in n of order 10
      reale(50295468,469581LL<<33),-reale(186185623,531407LL<<32),
      reale(174713425,41641LL<<35),-reale(59412258,3489LL<<32),
      -reale(26728127,294149LL<<33),reale(23787374,31373LL<<32),
      reale(13262792,54953LL<<34),-reale(23669724,520005LL<<32),
      reale(11190603,172969LL<<33),-reale(1670792,243479LL<<32),
      -reale(115324,7271069LL<<28),reale(118952554885LL,0xf0d684e8efa5dLL),
      // C4[14], coeff of eps^18, polynomial in n of order 11
      -reale(229751836,26450113LL<<27),reale(205122059,12799441LL<<28),
      -reale(76881886,7573243LL<<27),-reale(89213757,3943587LL<<30),
      reale(179505441,31406283LL<<27),-reale(144430080,11541225LL<<28),
      reale(48475411,26026641LL<<27),reale(12591449,3614557LL<<29),
      -reale(14798010,26226089LL<<27),-reale(1654995,15989667LL<<28),
      reale(6017860,18394141LL<<27),-reale(2035659,55899187LL<<24),
      reale(118952554885LL,0xf0d684e8efa5dLL),
      // C4[14], coeff of eps^17, polynomial in n of order 12
      -reale(60003627,419631LL<<31),reale(122260158,890121LL<<29),
      -reale(193015194,2586489LL<<30),reale(228332901,6912147LL<<29),
      -reale(182676146,109669LL<<32),reale(57596630,2823965LL<<29),
      reale(79437172,3055697LL<<30),-reale(146103578,5035353LL<<29),
      reale(121669517,1691035LL<<31),-reale(57926620,1249999LL<<29),
      reale(13245099,2677787LL<<30),reale(250593,3348667LL<<29),
      -reale(546524,56364575LL<<25),reale(118952554885LL,0xf0d684e8efa5dLL),
      // C4[14], coeff of eps^16, polynomial in n of order 13
      -reale(2910026,15317989LL<<28),reale(10671028,169493LL<<30),
      -reale(30788418,3245427LL<<28),reale(70862414,261923LL<<29),
      -reale(130581700,1010945LL<<28),reale(191189814,642567LL<<31),
      -reale(216782974,13106831LL<<28),reale(177827671,7710997LL<<29),
      -reale(82572754,6587933LL<<28),-reale(19188450,2518521LL<<30),
      reale(74652545,11169877LL<<28),-reale(71762036,443641LL<<29),
      reale(37978089,1743047LL<<28),-reale(9119456,66783679LL<<25),
      reale(118952554885LL,0xf0d684e8efa5dLL),
      // C4[14], coeff of eps^15, polynomial in n of order 14
      -reale(25313,471763LL<<30),reale(176943,4508751LL<<29),
      -reale(914488,1483519LL<<31),reale(3661023,8037561LL<<29),
      -reale(11683077,3602217LL<<30),reale(30253421,7276067LL<<29),
      -reale(64215578,725077LL<<32),reale(112133608,2030221LL<<29),
      -reale(160624032,1744767LL<<30),reale(186713478,2165751LL<<29),
      -reale(172286017,2016853LL<<31),reale(121247637,6964321LL<<29),
      -reale(60696359,459733LL<<30),reale(19031909,1781195LL<<29),
      -reale(2775486,102023215LL<<25),reale(118952554885LL,0xf0d684e8efa5dLL),
      // C4[14], coeff of eps^14, polynomial in n of order 15
      -real(614557125LL<<27),real(5831464275LL<<28),
      -reale(3808,17097199LL<<27),reale(28626,5026047LL<<29),
      -reale(160361,32462873LL<<27),reale(702411,15495849LL<<28),
      -reale(2480054,13665347LL<<27),reale(7201343,703573LL<<30),
      -reale(17420896,11395693LL<<27),reale(35365729,6899711LL<<28),
      -reale(60346284,10497687LL<<27),reale(86059048,7827541LL<<29),
      -reale(100689087,8447169LL<<27),reale(91987561,3237205LL<<28),
      -reale(55509735,6799595LL<<27),reale(15265177,48513541LL<<24),
      reale(118952554885LL,0xf0d684e8efa5dLL),
      // C4[15], coeff of eps^29, polynomial in n of order 0
      -real(204761LL<<28),reale(20426,0xaa7b82b97d24fLL),
      // C4[15], coeff of eps^28, polynomial in n of order 1
      -real(34699LL<<42),real(26415501LL<<29),reale(6134808,0xac3bb24726559LL),
      // C4[15], coeff of eps^27, polynomial in n of order 2
      reale(16894,439LL<<40),-reale(3396,5539LL<<38),
      -reale(13997,7293149LL<<28),reale(14128464373LL,0x6d08ce11dbba7LL),
      // C4[15], coeff of eps^26, polynomial in n of order 3
      -reale(50643,63489LL<<36),reale(243167,8553LL<<37),
      -reale(100839,3467LL<<36),reale(7018,548741LL<<30),
      reale(14128464373LL,0x6d08ce11dbba7LL),
      // C4[15], coeff of eps^25, polynomial in n of order 4
      -reale(6907413,21379LL<<36),reale(2301071,198931LL<<34),
      reale(591806,32973LL<<35),reale(76262,38289LL<<34),
      -reale(216244,23833777LL<<27),reale(127156179360LL,0xd54f3ea0b98dfLL),
      // C4[15], coeff of eps^24, polynomial in n of order 5
      -reale(2869395,52521LL<<36),-reale(3255913,8819LL<<38),
      -reale(2304160,33823LL<<36),reale(3661540,28261LL<<37),
      -reale(1155441,18213LL<<36),reale(48366,13607837LL<<28),
      reale(127156179360LL,0xd54f3ea0b98dfLL),
      // C4[15], coeff of eps^23, polynomial in n of order 6
      reale(8754539,110435LL<<35),reale(11218727,249609LL<<34),
      -reale(11298141,31087LL<<36),reale(996220,194783LL<<34),
      reale(1275763,41633LL<<35),reale(426630,95573LL<<34),
      -reale(392368,19533817LL<<27),reale(127156179360LL,0xd54f3ea0b98dfLL),
      // C4[15], coeff of eps^22, polynomial in n of order 7
      -reale(46020147,1607LL<<36),reale(18483914,31121LL<<37),
      reale(8546239,40923LL<<36),-reale(2972379,4277LL<<38),
      -reale(7799822,49315LL<<36),reale(6131851,9051LL<<37),
      -reale(1385578,60289LL<<36),reale(2743,3362879LL<<30),
      reale(127156179360LL,0xd54f3ea0b98dfLL),
      // C4[15], coeff of eps^21, polynomial in n of order 8
      reale(36025526,1303LL<<40),-reale(30131090,26093LL<<37),
      -reale(21835156,15459LL<<38),reale(35026415,31673LL<<37),
      -reale(11464406,5705LL<<39),-reale(3907909,12145LL<<37),
      reale(1722090,1439LL<<38),reale(1557916,18869LL<<37),
      -reale(781446,6381283LL<<28),reale(127156179360LL,0xd54f3ea0b98dfLL),
      // C4[15], coeff of eps^20, polynomial in n of order 9
      -reale(190262221,2387LL<<40),reale(146996287,1227LL<<41),
      -reale(33573688,3541LL<<40),-reale(32294922,2067LL<<39),
      reale(18331180,2115LL<<40),reale(16022794,2331LL<<40),
      -reale(22246846,3383LL<<40),reale(9695242,4143LL<<39),
      -reale(1302304,2671LL<<40),-reale(123235,5577019LL<<29),
      reale(127156179360LL,0xd54f3ea0b98dfLL),
      // C4[15], coeff of eps^19, polynomial in n of order 10
      reale(169057636,9637LL<<37),-reale(31515275,17095LL<<36),
      -reale(115123722,7359LL<<39),reale(174060780,58071LL<<36),
      -reale(122862790,20125LL<<37),reale(32880337,12981LL<<36),
      reale(15824026,705LL<<38),-reale(12943852,57005LL<<36),
      -reale(2522257,22495LL<<37),reale(5771316,18225LL<<36),
      -reale(1873338,7714415LL<<28),reale(127156179360LL,0xd54f3ea0b98dfLL),
      // C4[15], coeff of eps^18, polynomial in n of order 11
      reale(137352006,26079LL<<36),-reale(197705648,26891LL<<37),
      reale(213128803,51077LL<<36),-reale(150461460,3135LL<<39),
      reale(25445949,34251LL<<36),reale(93962136,11667LL<<37),
      -reale(140732252,24207LL<<36),reale(108614245,6273LL<<38),
      -reale(48923563,62665LL<<36),reale(10316934,1969LL<<37),
      reale(535285,33757LL<<36),-reale(501186,3348667LL<<30),
      reale(127156179360LL,0xd54f3ea0b98dfLL),
      // C4[15], coeff of eps^17, polynomial in n of order 12
      reale(15155809,205049LL<<34),-reale(39104030,957115LL<<32),
      reale(81967212,139599LL<<33),-reale(139468172,993913LL<<32),
      reale(190415844,61587LL<<35),-reale(202311488,967447LL<<32),
      reale(154439958,403401LL<<33),-reale(61783996,889045LL<<32),
      -reale(28504911,66989LL<<34),reale(73132006,1030157LL<<32),
      -reale(66442314,293949LL<<33),reale(34502754,346959LL<<32),
      -reale(8240730,128798053LL<<25),reale(127156179360LL,0xd54f3ea0b98dfLL),
      // C4[15], coeff of eps^16, polynomial in n of order 13
      reale(114172,142577LL<<34),-reale(499141,33119LL<<36),
      reale(1750098,174183LL<<34),-reale(5016097,114721LL<<35),
      reale(11893006,66221LL<<34),-reale(23470909,19093LL<<37),
      reale(38591591,188131LL<<34),-reale(52613301,65223LL<<35),
      reale(58753809,139305LL<<34),-reale(52512562,23925LL<<36),
      reale(36059714,158111LL<<34),-reale(17726185,93229LL<<35),
      reale(5486676,138853LL<<34),-reale(792996,14574745LL<<26),
      reale(42385393120LL,0x471a6a35932f5LL),
      // C4[15], coeff of eps^15, polynomial in n of order 14
      real(592706205LL<<33),-reale(9147,839013LL<<32),
      reale(55355,240865LL<<34),-reale(262940,644275LL<<32),
      reale(1011310,29095LL<<33),-reale(3215965,1023905LL<<32),
      reale(8575909,35467LL<<35),-reale(19352216,329839LL<<32),
      reale(37124659,455473LL<<33),-reale(60529336,778589LL<<32),
      reale(83288367,93771LL<<34),-reale(94856196,165035LL<<32),
      reale(85043486,110139LL<<33),-reale(50751757,943257LL<<32),
      reale(13877433,107462891LL<<25),reale(127156179360LL,0xd54f3ea0b98dfLL),
      // C4[16], coeff of eps^29, polynomial in n of order 0
      real(553LL<<31),real(0x292ecb9a960d27d1LL),
      // C4[16], coeff of eps^28, polynomial in n of order 1
      -real(61453LL<<36),-real(4754645LL<<34),
      reale(19591808,0x57955a5f17535LL),
      // C4[16], coeff of eps^27, polynomial in n of order 2
      reale(33770,14237LL<<36),-reale(12917,115767LL<<35),
      real(1665987897LL<<31),reale(2148568314LL,0xda506166fe05fLL),
      // C4[16], coeff of eps^26, polynomial in n of order 3
      reale(1765351,9719LL<<36),reale(634098,16193LL<<37),
      reale(114937,5021LL<<36),-reale(211035,902511LL<<32),
      reale(135359803835LL,0xb9c7f85883761LL),
      // C4[16], coeff of eps^25, polynomial in n of order 4
      -reale(3041817,11535LL<<37),-reale(2643315,63657LL<<35),
      reale(3458443,225LL<<36),-reale(1011407,29251LL<<35),
      reale(33755,354965LL<<31),reale(135359803835LL,0xb9c7f85883761LL),
      // C4[16], coeff of eps^24, polynomial in n of order 5
      reale(12443946,111847LL<<35),-reale(9978547,23661LL<<37),
      reale(264818,24689LL<<35),reale(1196082,9587LL<<36),
      reale(477961,17339LL<<35),-reale(375862,243659LL<<32),
      reale(135359803835LL,0xb9c7f85883761LL),
      // C4[16], coeff of eps^23, polynomial in n of order 6
      reale(11971225,59849LL<<36),reale(9677599,56595LL<<35),
      -reale(1515717,2317LL<<37),-reale(7956047,112667LL<<35),
      reale(5585704,62147LL<<36),-reale(1169086,64041LL<<35),
      -reale(10840,1435009LL<<31),reale(135359803835LL,0xb9c7f85883761LL),
      // C4[16], coeff of eps^22, polynomial in n of order 7
      -reale(20905609,47207LL<<36),-reale(27003899,18815LL<<37),
      reale(32128586,62715LL<<36),-reale(8207156,6437LL<<38),
      -reale(4335741,20931LL<<36),reale(1351161,14763LL<<37),
      reale(1583200,44703LL<<36),-reale(734819,355141LL<<32),
      reale(135359803835LL,0xb9c7f85883761LL),
      // C4[16], coeff of eps^21, polynomial in n of order 8
      reale(119271221,13241LL<<38),-reale(13185134,117885LL<<35),
      -reale(34424757,12741LL<<36),reale(12971898,62105LL<<35),
      reale(17958221,23929LL<<37),-reale(20751983,45233LL<<35),
      reale(8405753,5609LL<<36),-reale(1009805,84123LL<<35),
      -reale(126669,998091LL<<31),reale(135359803835LL,0xb9c7f85883761LL),
      // C4[16], coeff of eps^20, polynomial in n of order 9
      reale(7281953,46177LL<<36),-reale(132136826,3687LL<<39),
      reale(164419146,28463LL<<36),-reale(102943145,13301LL<<37),
      reale(20499773,15997LL<<36),reale(17609391,14489LL<<38),
      -reale(11127728,9397LL<<36),-reale(3194109,31911LL<<37),
      reale(5518515,63129LL<<36),-reale(1729623,95963LL<<34),
      reale(135359803835LL,0xb9c7f85883761LL),
      // C4[16], coeff of eps^19, polynomial in n of order 10
      -reale(197461925,11965LL<<36),reale(194820269,1667LL<<35),
      -reale(119937281,937LL<<38),-reale(1213288,24915LL<<35),
      reale(103481944,52469LL<<36),-reale(133891976,7945LL<<35),
      reale(96822293,18071LL<<37),-reale(41434588,121951LL<<35),
      reale(8025452,27431LL<<36),reale(724406,126187LL<<35),
      -reale(459367,1295029LL<<31),reale(135359803835LL,0xb9c7f85883761LL),
      // C4[16], coeff of eps^18, polynomial in n of order 11
      -reale(47525285,62545LL<<36),reale(91887649,22453LL<<37),
      -reale(145781941,19979LL<<36),reale(186991182,8001LL<<39),
      -reale(187151585,35749LL<<36),reale(133148350,20179LL<<37),
      -reale(44425461,13151LL<<36),-reale(35371425,9983LL<<38),
      reale(71056997,38023LL<<36),-reale(61631567,21647LL<<37),
      reale(31499493,50637LL<<36),-reale(7491423,758351LL<<32),
      reale(135359803835LL,0xb9c7f85883761LL),
      // C4[16], coeff of eps^17, polynomial in n of order 12
      -reale(2259631,24697LL<<37),reale(7098981,78723LL<<35),
      -reale(18582556,8879LL<<36),reale(40852668,12369LL<<35),
      -reale(75692831,12691LL<<38),reale(118080654,84927LL<<35),
      -reale(154130872,52073LL<<36),reale(166118829,74381LL<<35),
      -reale(144327913,2259LL<<37),reale(96964720,98683LL<<35),
      -reale(46899246,18595LL<<36),reale(14349769,50505LL<<35),
      -reale(2057503,1465135LL<<31),reale(135359803835LL,0xb9c7f85883761LL),
      // C4[16], coeff of eps^16, polynomial in n of order 13
      -reale(18695,264305LL<<33),reale(95700,8265LL<<35),
      -reale(398136,419847LL<<33),reale(1375381,177071LL<<34),
      -reale(4004787,429789LL<<33),reale(9930000,13635LL<<36),
      -reale(21101250,231795LL<<33),reale(38532718,52073LL<<34),
      -reale(60367925,93257LL<<33),reale(80490566,118467LL<<35),
      -reale(89511061,246751LL<<33),reale(78923731,162339LL<<34),
      -reale(46636750,263349LL<<33),reale(12687939,1991833LL<<30),
      reale(135359803835LL,0xb9c7f85883761LL),
      // C4[17], coeff of eps^29, polynomial in n of order 0
      -real(280331LL<<31),reale(154847,0x4e6e7be138cdbLL),
      // C4[17], coeff of eps^28, polynomial in n of order 1
      -real(82431LL<<38),real(142069LL<<33),reale(989485,0x4511e2f2b39a3LL),
      // C4[17], coeff of eps^27, polynomial in n of order 2
      reale(30957,2723LL<<36),reale(7080,38071LL<<35),
      -reale(9773,1986585LL<<31),reale(6836353729LL,0x13b9f01928417LL),
      // C4[17], coeff of eps^26, polynomial in n of order 3
      -reale(138771,28785LL<<37),reale(154910,14439LL<<38),
      -reale(42193,29611LL<<37),real(1108797915LL<<32),
      reale(6836353729LL,0x13b9f01928417LL),
      // C4[17], coeff of eps^25, polynomial in n of order 4
      -reale(1238256,21701LL<<37),-reale(44811,81027LL<<35),
      reale(156785,14859LL<<36),reale(74079,77407LL<<35),
      -reale(51372,1082481LL<<31),reale(20509061187LL,0x3b2dd04b78c45LL),
      // C4[17], coeff of eps^24, polynomial in n of order 5
      reale(10057115,7495LL<<39),-reale(158283,1579LL<<41),
      -reale(7978477,2703LL<<39),reale(5079175,3021LL<<40),
      -reale(987192,2101LL<<39),-reale(20806,242401LL<<34),
      reale(143563428310LL,0x9e40b2104d5e3LL),
      // C4[17], coeff of eps^23, polynomial in n of order 6
      -reale(30405369,16203LL<<36),reale(28904813,10839LL<<35),
      -reale(5472666,28841LL<<37),-reale(4538327,21695LL<<35),
      reale(1010309,2151LL<<36),reale(1591197,61387LL<<35),
      -reale(691600,842821LL<<31),reale(143563428310LL,0x9e40b2104d5e3LL),
      // C4[17], coeff of eps^22, polynomial in n of order 7
      reale(2360974,20517LL<<37),-reale(34168343,7885LL<<38),
      reale(7988557,399LL<<37),reale(19220645,2761LL<<39),
      -reale(19251394,21847LL<<37),reale(7294617,7633LL<<38),
      -reale(776533,25709LL<<37),-reale(127091,628387LL<<32),
      reale(143563428310LL,0x9e40b2104d5e3LL),
      // C4[17], coeff of eps^21, polynomial in n of order 8
      -reale(141970389,5875LL<<38),reale(152285106,31LL<<35),
      -reale(85013706,54665LL<<36),reale(10784519,74157LL<<35),
      reale(18376223,22989LL<<37),-reale(9415616,123045LL<<35),
      -reale(3707065,39939LL<<36),reale(5266887,19945LL<<35),
      -reale(1601936,974407LL<<31),reale(143563428310LL,0x9e40b2104d5e3LL),
      // C4[17], coeff of eps^20, polynomial in n of order 9
      reale(174732199,7199LL<<38),-reale(91781661,1783LL<<41),
      -reale(22947906,1007LL<<38),reale(109147441,451LL<<39),
      -reale(126268040,11085LL<<38),reale(86262862,2409LL<<40),
      -reale(35185382,1435LL<<38),reale(6220582,7041LL<<39),
      reale(846498,391LL<<38),-reale(421214,84365LL<<33),
      reale(143563428310LL,0x9e40b2104d5e3LL),
      // C4[17], coeff of eps^19, polynomial in n of order 10
      reale(100447726,5039LL<<36),-reale(149758021,121873LL<<35),
      reale(181554380,1939LL<<38),-reale(171878757,123903LL<<35),
      reale(113962110,29289LL<<36),-reale(29967290,80205LL<<35),
      -reale(40353928,13613LL<<37),reale(68655180,86853LL<<35),
      -reale(57285125,57949LL<<36),reale(28886745,8439LL<<35),
      -reale(6846764,2025561LL<<31),reale(143563428310LL,0x9e40b2104d5e3LL),
      // C4[17], coeff of eps^18, polynomial in n of order 11
      reale(9163438,32371LL<<37),-reale(22188557,9937LL<<38),
      reale(45681407,15569LL<<37),-reale(80085759,21LL<<40),
      reale(119267529,32127LL<<37),-reale(149784698,12951LL<<38),
      reale(156408668,30941LL<<37),-reale(132494258,5045LL<<39),
      reale(87286969,10059LL<<37),-reale(41608633,10397LL<<38),
      reale(12599797,16681LL<<37),-reale(1793721,181577LL<<32),
      reale(143563428310LL,0x9e40b2104d5e3LL),
      // C4[17], coeff of eps^17, polynomial in n of order 12
      reale(152058,3531LL<<37),-reale(566838,109449LL<<35),
      reale(1788421,65069LL<<36),-reale(4828739,49907LL<<35),
      reale(11241509,10969LL<<38),-reale(22666304,107837LL<<35),
      reale(39633653,283LL<<36),-reale(59939783,113319LL<<35),
      reale(77715030,3737LL<<37),-reale(84609105,47985LL<<35),
      reale(73498818,52617LL<<36),-reale(43049308,20443LL<<35),
      reale(11659187,1311925LL<<31),reale(143563428310LL,0x9e40b2104d5e3LL),
      // C4[18], coeff of eps^29, polynomial in n of order 0
      real(35LL<<34),real(0x29845c2bcb5c10d7LL),
      // C4[18], coeff of eps^28, polynomial in n of order 1
      reale(3628,18373LL<<37),-reale(4063,232509LL<<34),
      reale(3097286791LL,0x8a812bfedbe75LL),
      // C4[18], coeff of eps^27, polynomial in n of order 2
      reale(435730,613LL<<39),-reale(110987,3811LL<<38),real(489021323LL<<34),
      reale(21681007540LL,0xc98833f803533LL),
      // C4[18], coeff of eps^26, polynomial in n of order 3
      -reale(762945,31179LL<<36),reale(988791,87LL<<37),
      reale(550009,38375LL<<36),-reale(343815,323189LL<<33),
      reale(151767052785LL,0x82b96bc817465LL),
      // C4[18], coeff of eps^25, polynomial in n of order 4
      reale(1063744,27LL<<41),-reale(7897635,7767LL<<39),
      reale(4613149,699LL<<40),-reale(833936,93LL<<39),
      -reale(28054,94387LL<<35),reale(151767052785LL,0x82b96bc817465LL),
      // C4[18], coeff of eps^24, polynomial in n of order 5
      reale(25578507,4379LL<<38),-reale(3209600,1553LL<<40),
      -reale(4577572,5923LL<<38),reale(702466,1583LL<<39),
      reale(1586031,287LL<<38),-reale(651636,122639LL<<35),
      reale(151767052785LL,0x82b96bc817465LL),
      // C4[18], coeff of eps^23, polynomial in n of order 6
      -reale(32324739,1815LL<<40),reale(3520775,1207LL<<39),
      reale(19946468,259LL<<41),-reale(17787966,4575LL<<39),
      reale(6336978,3235LL<<40),-reale(589727,5685LL<<39),
      -reale(125505,20667LL<<35),reale(151767052785LL,0x82b96bc817465LL),
      // C4[18], coeff of eps^22, polynomial in n of order 7
      reale(138884729,22203LL<<36),-reale(69168625,14473LL<<37),
      reale(3249237,4577LL<<36),reale(18436830,10301LL<<38),
      -reale(7840055,31609LL<<36),-reale(4091705,30595LL<<37),
      reale(5021146,27565LL<<36),-reale(1488082,115687LL<<33),
      reale(151767052785LL,0x82b96bc817465LL),
      // C4[18], coeff of eps^21, polynomial in n of order 8
      -reale(66334778,1299LL<<41),-reale(40377625,16285LL<<38),
      reale(111882749,4839LL<<39),-reale(118325119,4711LL<<38),
      reale(76858390,3693LL<<40),-reale(29952902,3569LL<<38),
      reale(4790818,4941LL<<39),reale(921311,4421LL<<38),
      -reale(386621,181821LL<<34),reale(151767052785LL,0x82b96bc817465LL),
      // C4[18], coeff of eps^20, polynomial in n of order 9
      -reale(151679112,16629LL<<37),reale(174648786,1667LL<<40),
      -reale(156892091,15835LL<<37),reale(96799837,4169LL<<38),
      -reale(17949188,6721LL<<37),-reale(43885384,7293LL<<39),
      reale(66080580,25305LL<<37),-reale(53357084,1853LL<<38),
      reale(26599572,17011LL<<37),-reale(6287689,169979LL<<34),
      reale(151767052785LL,0x82b96bc817465LL),
      // C4[18], coeff of eps^19, polynomial in n of order 10
      -reale(8594193,5169LL<<39),reale(16702080,5475LL<<38),
      -reale(27882498,1245LL<<41),reale(39843622,14413LL<<38),
      -reale(48340851,951LL<<39),reale(49066184,11639LL<<38),
      -reale(40627946,3165LL<<40),reale(26296855,15713LL<<38),
      -reale(12371894,1597LL<<39),reale(3711568,4235LL<<38),
      -reale(524991,147555LL<<34),reale(50589017595LL,0x2b9323ed5d177LL),
      // C4[18], coeff of eps^18, polynomial in n of order 11
      -reale(768539,29011LL<<36),reale(2243105,18035LL<<37),
      -reale(5671852,39713LL<<36),reale(12494515,7255LL<<39),
      -reale(24051943,5231LL<<36),reale(40468348,22085LL<<37),
      -reale(59307062,46653LL<<36),reale(74994737,5975LL<<38),
      -reale(80108014,59787LL<<36),reale(68664012,25623LL<<37),
      -reale(39899358,51033LL<<36),reale(10762327,20443LL<<33),
      reale(151767052785LL,0x82b96bc817465LL),
      // C4[19], coeff of eps^29, polynomial in n of order 0
      -real(69697LL<<34),reale(220556,0x6c98ea537e51fLL),
      // C4[19], coeff of eps^28, polynomial in n of order 1
      -real(1238839LL<<41),real(675087LL<<35),
      reale(141943813,0x222cc7846d81LL),
      // C4[19], coeff of eps^27, polynomial in n of order 2
      reale(876102,3999LL<<40),reale(573743,1451LL<<39),
      -reale(328615,14973LL<<34),reale(159970677260LL,0x6732257fe12e7LL),
      // C4[19], coeff of eps^26, polynomial in n of order 3
      -reale(7739083,17LL<<46),reale(4186838,53LL<<45),-reale(704448,1LL<<46),
      -reale(33249,11241LL<<37),reale(159970677260LL,0x6732257fe12e7LL),
      // C4[19], coeff of eps^25, polynomial in n of order 4
      -reale(1360864,133LL<<42),-reale(4500609,2667LL<<40),
      reale(427896,299LL<<41),reale(1570943,1191LL<<40),
      -reale(614728,45789LL<<35),reale(159970677260LL,0x6732257fe12e7LL),
      // C4[19], coeff of eps^24, polynomial in n of order 5
      -reale(379105,631LL<<42),reale(20252634,139LL<<44),
      -reale(16388211,705LL<<42),reale(5510947,339LL<<43),
      -reale(439601,699LL<<42),-reale(122601,56745LL<<36),
      reale(159970677260LL,0x6732257fe12e7LL),
      // C4[19], coeff of eps^23, polynomial in n of order 6
      -reale(55355388,567LL<<41),-reale(2520461,2117LL<<40),
      reale(18017708,147LL<<42),-reale(6413373,771LL<<40),
      -reale(4373212,61LL<<41),reale(4784182,2079LL<<40),
      -reale(1386197,54485LL<<35),reale(159970677260LL,0x6732257fe12e7LL),
      // C4[19], coeff of eps^22, polynomial in n of order 7
      -reale(54112477,29LL<<46),reale(112419812,35LL<<45),
      -reale(110372726,9LL<<46),reale(68510282,53LL<<46),
      -reale(25556330,19LL<<46),reale(3652507,1LL<<45),reale(962676,17LL<<46),
      -reale(355362,30093LL<<37),reale(159970677260LL,0x6732257fe12e7LL),
      // C4[19], coeff of eps^21, polynomial in n of order 8
      reale(166723371,209LL<<42),-reale(142457721,7469LL<<39),
      reale(81530379,2787LL<<40),-reale(7977897,3383LL<<39),
      -reale(46298043,1775LL<<41),reale(63437092,799LL<<39),
      -reale(49803454,3807LL<<40),reale(24585849,2581LL<<39),
      -reale(5799325,105875LL<<34),reale(159970677260LL,0x6732257fe12e7LL),
      // C4[19], coeff of eps^20, polynomial in n of order 9
      reale(54095236,1729LL<<41),-reale(86448328,33LL<<44),
      reale(119042325,527LL<<41),-reale(140012701,875LL<<42),
      reale(138519104,1133LL<<41),-reale(112357061,257LL<<43),
      reale(71568963,1275LL<<41),-reale(33272498,441LL<<42),
      reale(9897515,729LL<<41),-reale(1391838,12705LL<<35),
      reale(159970677260LL,0x6732257fe12e7LL),
      // C4[19], coeff of eps^19, polynomial in n of order 10
      reale(2731650,3225LL<<40),-reale(6520331,5423LL<<39),
      reale(13678206,885LL<<42),-reale(25266687,5569LL<<39),
      reale(41073925,3215LL<<40),-reale(58519302,7091LL<<39),
      reale(72351138,181LL<<41),-reale(75968694,8133LL<<39),
      reale(64333849,3333LL<<40),-reale(37115682,4791LL<<39),
      reale(9974839,182105LL<<34),reale(159970677260LL,0x6732257fe12e7LL),
      // C4[20], coeff of eps^29, polynomial in n of order 0
      real(1LL<<39),reale(386445,0x44b61aebc827LL),
      // C4[20], coeff of eps^28, polynomial in n of order 1
      reale(3670,3431LL<<40),-real(63923791LL<<37),
      reale(1044560880,0x57ec63f8653c9LL),
      // C4[20], coeff of eps^27, polynomial in n of order 2
      reale(165149,453LL<<43),-reale(25858,471LL<<42),-real(26276299LL<<38),
      reale(7311926162LL,0x6776bbcac4a7fLL),
      // C4[20], coeff of eps^26, polynomial in n of order 3
      -reale(4343033,595LL<<42),reale(185313,303LL<<43),
      reale(1548473,271LL<<42),-reale(580654,777LL<<40),
      reale(168174301735LL,0x4baadf37ab169LL),
      // C4[20], coeff of eps^25, polynomial in n of order 4
      reale(20236427,149LL<<44),-reale(15067334,133LL<<42),
      reale(4797544,165LL<<43),-reale(318599,375LL<<42),
      -reale(118861,3875LL<<38),reale(168174301735LL,0x4baadf37ab169LL),
      // C4[20], coeff of eps^24, polynomial in n of order 5
      -reale(6870833,1979LL<<41),reale(17282399,281LL<<43),
      -reale(5135975,189LL<<41),-reale(4572111,263LL<<42),
      reale(4557653,1537LL<<41),-reale(1294702,4061LL<<38),
      reale(168174301735LL,0x4baadf37ab169LL),
      // C4[20], coeff of eps^23, polynomial in n of order 6
      reale(111332564,131LL<<43),-reale(102611836,439LL<<42),
      reale(61113705,49LL<<44),-reale(21849131,865LL<<42),
      reale(2742318,257LL<<43),reale(980372,533LL<<42),
      -reale(327159,8391LL<<38),reale(168174301735LL,0x4baadf37ab169LL),
      // C4[20], coeff of eps^22, polynomial in n of order 7
      -reale(128743521,979LL<<42),reale(67998970,481LL<<43),
      reale(279122,855LL<<42),-reale(47847734,245LL<<44),
      reale(60794248,257LL<<42),-reale(46583621,181LL<<43),
      reale(22803394,43LL<<42),-reale(5369928,2229LL<<40),
      reale(168174301735LL,0x4baadf37ab169LL),
      // C4[20], coeff of eps^21, polynomial in n of order 8
      -reale(88564699,121LL<<45),reale(117949702,533LL<<42),
      -reale(134881895,27LL<<43),reale(130376590,239LL<<42),
      -reale(103788735,57LL<<44),reale(65154071,233LL<<42),
      -reale(29963298,393LL<<43),reale(8844588,195LL<<42),
      -reale(1237189,6873LL<<38),reale(168174301735LL,0x4baadf37ab169LL),
      // C4[20], coeff of eps^20, polynomial in n of order 9
      -reale(7362630,999LL<<40),reale(14785858,137LL<<43),
      -reale(26321377,9LL<<40),reale(41483460,1083LL<<41),
      -reale(57615917,1643LL<<40),reale(69797568,521LL<<42),
      -reale(72155594,1933LL<<40),reale(60438019,617LL<<41),
      -reale(34641303,3055LL<<40),reale(9278920,21175LL<<37),
      reale(168174301735LL,0x4baadf37ab169LL),
      // C4[21], coeff of eps^29, polynomial in n of order 0
      -real(2017699LL<<39),reale(144690669,0x92d5d14b2b5b9LL),
      // C4[21], coeff of eps^28, polynomial in n of order 1
      -reale(21806,31LL<<47),-real(1751493LL<<42),
      reale(7668605487LL,0x6644548ff9f4dLL),
      // C4[21], coeff of eps^27, polynomial in n of order 2
      -real(610053LL<<43),reale(66113,223LL<<42),-reale(23877,14131LL<<38),
      reale(7668605487LL,0x6644548ff9f4dLL),
      // C4[21], coeff of eps^26, polynomial in n of order 3
      -reale(601427,223LL<<44),reale(181759,65LL<<45),-reale(9602,5LL<<44),
      -reale(4983,2721LL<<39),reale(7668605487LL,0x6644548ff9f4dLL),
      // C4[21], coeff of eps^25, polynomial in n of order 4
      reale(16348405,227LL<<44),-reale(4001511,795LL<<42),
      -reale(4705038,397LL<<43),reale(4342393,855LL<<42),
      -reale(1212256,1051LL<<38),reale(176377926210LL,0x302398ef74febLL),
      // C4[21], coeff of eps^24, polynomial in n of order 5
      -reale(95167920,19LL<<45),reale(54565817,7LL<<47),
      -reale(18712410,5LL<<45),reale(2011897,15LL<<46),reale(981374,25LL<<45),
      -reale(301721,597LL<<40),reale(176377926210LL,0x302398ef74febLL),
      // C4[21], coeff of eps^23, polynomial in n of order 6
      reale(56043535,133LL<<43),reale(7101303,759LL<<42),
      -reale(48732132,249LL<<44),reale(58197907,161LL<<42),
      -reale(43660867,425LL<<43),reale(21217809,619LL<<42),
      -reale(4990122,11039LL<<38),reale(176377926210LL,0x302398ef74febLL),
      // C4[21], coeff of eps^22, polynomial in n of order 7
      reale(38792824,189LL<<44),-reale(43241527,125LL<<45),
      reale(40920531,151LL<<44),-reale(32022608,39LL<<46),
      reale(19836099,97LL<<44),-reale(9032168,63LL<<45),
      reale(2647359,187LL<<44),-reale(368524,4161LL<<39),
      reale(58792642070LL,0x100bdda526ff9LL),
      // C4[21], coeff of eps^21, polynomial in n of order 8
      reale(15813930,121LL<<45),-reale(27228018,205LL<<42),
      reale(41726053,443LL<<43),-reale(56628215,983LL<<42),
      reale(67341662,57LL<<44),-reale(68636694,193LL<<42),
      reale(56918234,105LL<<43),-reale(32430156,715LL<<42),
      reale(8660325,15343LL<<38),reale(176377926210LL,0x302398ef74febLL),
      // C4[22], coeff of eps^29, polynomial in n of order 0
      -real(229LL<<43),reale(2018939,0x935060fc493cdLL),
      // C4[22], coeff of eps^28, polynomial in n of order 1
      reale(64733,61LL<<46),-reale(22613,493LL<<43),
      reale(8025284812LL,0x6511ed552f41bLL),
      // C4[22], coeff of eps^27, polynomial in n of order 2
      reale(158513,3LL<<48),-reale(6162,29LL<<47),-reale(4786,487LL<<43),
      reale(8025284812LL,0x6511ed552f41bLL),
      // C4[22], coeff of eps^26, polynomial in n of order 3
      -reale(130438,301LL<<43),-reale(208062,47LL<<44),reale(179942,497LL<<43),
      -reale(49466,167LL<<40),reale(8025284812LL,0x6511ed552f41bLL),
      // C4[22], coeff of eps^25, polynomial in n of order 4
      reale(2120438,3LL<<47),-reale(697803,39LL<<45),reale(61914,3LL<<46),
      reale(42203,115LL<<45),-reale(12120,543LL<<41),
      reale(8025284812LL,0x6511ed552f41bLL),
      // C4[22], coeff of eps^24, polynomial in n of order 5
      reale(12722577,33LL<<44),-reale(49104495,51LL<<46),
      reale(55677556,71LL<<44),-reale(41002422,115LL<<45),
      reale(19800840,109LL<<44),-reale(4652345,837LL<<41),
      reale(184581550685LL,0x149c52a73ee6dLL),
      // C4[22], coeff of eps^23, polynomial in n of order 6
      -reale(124610244,57LL<<46),reale(115654934,113LL<<45),
      -reale(89096506,19LL<<47),reale(54518354,119LL<<45),
      -reale(24598996,19LL<<46),reale(7163443,125LL<<45),
      -reale(992759,1841LL<<41),reale(184581550685LL,0x149c52a73ee6dLL),
      // C4[22], coeff of eps^22, polynomial in n of order 7
      -reale(27999005,155LL<<43),reale(41827085,121LL<<44),
      -reale(55581037,1LL<<43),reale(64987058,83LL<<45),
      -reale(65383321,103LL<<43),reale(53725829,211LL<<44),
      -reale(30444636,461LL<<43),reale(8107539,715LL<<40),
      reale(184581550685LL,0x149c52a73ee6dLL),
      // C4[23], coeff of eps^29, polynomial in n of order 0
      -reale(4289,21LL<<43),reale(1676392827,0x7a5fe79ee0e95LL),
      // C4[23], coeff of eps^28, polynomial in n of order 1
      -real(1351LL<<51),-real(234789LL<<44),
      reale(1676392827,0x7a5fe79ee0e95LL),
      // C4[23], coeff of eps^27, polynomial in n of order 2
      -reale(209744,1LL<<50),reale(171585,3LL<<49),-reale(46526,469LL<<43),
      reale(8381964137LL,0x63df861a648e9LL),
      // C4[23], coeff of eps^26, polynomial in n of order 3
      -reale(599194,1LL<<51),reale(41297,0),reale(41388,1LL<<51),
      -reale(11218,97LL<<45),reale(8381964137LL,0x63df861a648e9LL),
      // C4[23], coeff of eps^25, polynomial in n of order 4
      -reale(2134087,7LL<<49),reale(2315275,31LL<<47),-reale(1677358,15LL<<48),
      reale(805613,21LL<<47),-reale(189149,1213LL<<41),
      reale(8381964137LL,0x63df861a648e9LL),
      // C4[23], coeff of eps^24, polynomial in n of order 5
      reale(4740508,1LL<<49),-reale(3599518,1LL<<51),reale(2177844,7LL<<49),
      -reale(974429,1LL<<50),reale(282071,5LL<<49),-reale(38931,779LL<<42),
      reale(8381964137LL,0x63df861a648e9LL),
      // C4[23], coeff of eps^23, polynomial in n of order 6
      reale(1817763,3LL<<48),-reale(2369306,23LL<<47),reale(2727592,1LL<<49),
      -reale(2711734,1LL<<47),reale(2209561,1LL<<48),-reale(1245816,11LL<<47),
      reale(330919,1979LL<<41),reale(8381964137LL,0x63df861a648e9LL),
      // C4[24], coeff of eps^29, polynomial in n of order 0
      -real(1439LL<<46),reale(44813556,0x37a4fd885dffdLL),
      // C4[24], coeff of eps^28, polynomial in n of order 1
      reale(32742,3LL<<50),-reale(8770,21LL<<47),
      reale(1747728692,0x7a229fc651f8bLL),
      // C4[24], coeff of eps^27, polynomial in n of order 2
      reale(4928,1LL<<51),reale(8067,1LL<<50),-reale(2080,43LL<<46),
      reale(1747728692,0x7a229fc651f8bLL),
      // C4[24], coeff of eps^26, polynomial in n of order 3
      reale(2214330,0),-reale(1581120,0),reale(755790,0),
      -reale(177363,7LL<<47),reale(8738643462LL,0x62ad1edf99db7LL),
      // C4[24], coeff of eps^25, polynomial in n of order 4
      -reale(1116955,0),reale(668788,3LL<<50),-reale(296917,1LL<<51),
      reale(85476,1LL<<50),-reale(11752,63LL<<46),
      reale(2912881154LL,0x20e45f9fddf3dLL),
      // C4[24], coeff of eps^24, polynomial in n of order 5
      -reale(2320992,3LL<<48),reale(2634056,1LL<<50),-reale(2590155,5LL<<48),
      reale(2094168,1LL<<49),-reale(1175298,7LL<<48),reale(311454,11LL<<45),
      reale(8738643462LL,0x62ad1edf99db7LL),
      // C4[25], coeff of eps^29, polynomial in n of order 0
      -real(3707LL<<46),reale(12720731,0x2bd144a4925efLL),
      // C4[25], coeff of eps^28, polynomial in n of order 1
      real(301LL<<53),-real(2379LL<<48),reale(139928042,0xe1fdf3124a145LL),
      // C4[25], coeff of eps^27, polynomial in n of order 2
      -reale(298603,1LL<<51),reale(142145,1LL<<50),-reale(33346,63LL<<46),
      reale(1819064557,0x79e557edc3081LL),
      // C4[25], coeff of eps^26, polynomial in n of order 3
      reale(370617,0),-reale(163358,0),reale(46787,0),-reale(6410,23LL<<47),
      reale(1819064557,0x79e557edc3081LL),
      // C4[25], coeff of eps^25, polynomial in n of order 4
      reale(508963,0),-reale(495426,3LL<<50),reale(397689,1LL<<51),
      -reale(222238,1LL<<50),reale(58764,59LL<<46),
      reale(1819064557,0x79e557edc3081LL),
      // C4[26], coeff of eps^29, polynomial in n of order 0
      -real(1LL<<49),reale(131359,0xe834f81ee20c1LL),
      // C4[26], coeff of eps^28, polynomial in n of order 1
      reale(10305,0),-reale(2417,1LL<<49),reale(145415417,0x1d0ced8b7a293LL),
      // C4[26], coeff of eps^27, polynomial in n of order 2
      -reale(11556,0),reale(3294,0),-real(3599LL<<49),
      reale(145415417,0x1d0ced8b7a293LL),
      // C4[26], coeff of eps^26, polynomial in n of order 3
      -reale(36490,1LL<<51),reale(29097,0),-reale(16195,1LL<<51),
      reale(4273,13LL<<48),reale(145415417,0x1d0ced8b7a293LL),
      // C4[27], coeff of eps^29, polynomial in n of order 0
      -real(2029LL<<49),reale(16766976,0xd0e6a80084b19LL),
      // C4[27], coeff of eps^28, polynomial in n of order 1
      real(7LL<<56),-real(61LL<<50),reale(5588992,0x45a238002c3b3LL),
      // C4[27], coeff of eps^27, polynomial in n of order 2
      reale(3080,0),-real(427LL<<54),real(3599LL<<49),
      reale(16766976,0xd0e6a80084b19LL),
      // C4[28], coeff of eps^29, polynomial in n of order 0
      -real(1LL<<53),reale(827461,0x318a62b8e0a5bLL),
      // C4[28], coeff of eps^28, polynomial in n of order 1
      -real(29LL<<55),real(61LL<<52),reale(2482383,0x949f282aa1f11LL),
      // C4[29], coeff of eps^29, polynomial in n of order 0
      real(1LL<<53),reale(88602,0xec373d36a45dfLL),
    };  // count = 5425
#else
#error "Bad value for GEOGRAPHICLIB_GEODESICEXACT_ORDER"
#endif
    static_assert(sizeof(coeff) / sizeof(real) ==
                  (nC4_ * (nC4_ + 1) * (nC4_ + 5)) / 6,
                  "Coefficient array size mismatch in C4coeff");
    int o = 0, k = 0;
    for (int l = 0; l < nC4_; ++l) {        // l is index of C4[l]
      for (int j = nC4_ - 1; j >= l; --j) { // coeff of eps^j
        int m = nC4_ - j - 1;               // order of polynomial in n
        _cC4x[k++] = Math::polyval(m, coeff + o, _n) / coeff[o + m + 1];
        o += m + 2;
      }
    }
    // Post condition: o == sizeof(coeff) / sizeof(real) && k == nC4x_
    if  (!(o == sizeof(coeff) / sizeof(real) && k == nC4x_))
      throw GeographicErr("C4 misalignment");
  }

} // namespace GeographicLib
