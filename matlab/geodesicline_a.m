function [latlong, aux] = geodesicline_a(lat1, lon1, azi1, distances)
%geodesicline  Compute points along a geodesic
%
%   [latlong, aux] = geodesicline(lat1, lon1, azi1, distances)
%   [latlong, aux] = geodesicline(lat1, lon1, azi1, distances, a, f)
%
%   lat1 is the latitude of point 1 (scalar) in degrees
%   lon1 is the longitude of point 1 (scalar) in degrees
%   azi1 is the azimuth at point 1 (scalar) in degrees
%   distances is an M x 1 vector of distances to point 2 in meters
%
%   latlong is an M x 3 matrix
%       latitude of point 2 = geodesic(:,1) in degrees
%       longitude of point 2 = geodesic(:,2) in degrees
%       azimuth at point 2 = geodesic(:,3) in degrees
%   aux is an M x 5 matrix
%       spherical arc length = aux(:,1) in degrees
%       reduced length = aux(:,2) in meters
%       geodesic scale 1 to 2 = aux(:,3)
%       geodesic scale 2 to 1 = aux(:,4)
%       area under geodesic = aux(:,5) in meters^2
%
%   a = major radius (meters)
%   f = flattening (0 means a sphere)
%   If a and f are omitted, the WGS84 values are used.
%
%   The result is the same as produced by
%       geodesicdirect([repmat([lat1, lon1, azi1],size(distances)), ...
%                       distances], a, f)
%
% The algorithm used in this function is given in
%
%     C. F. F. Karney, Algorithms for geodesics,
%     J. Geodesy 87, 43-55 (2013);
%     https://dx.doi.org/10.1007/s00190-012-0578-z
%     Addenda: http://geographiclib.sf.net/geod-addenda.html
%
% A native MATLAB implementation is available as GEODRECKON.
%
% See also GEODRECKON.
  if (nargin < 2)
    ellipsoid = defaultellipsoid;
  elseif (nargin < 3)
    ellipsoid = [a, 0];
  else
    ellipsoid = [a, flat2ecc(f)];
  end
  [lat2, lon2, azi2, S12, m12, M12, M21, a12] = ...
      geodreckon(lat1, lon1, distances, azi1, ellipsoid);
  latlong = [lat2, lon2, azi2];
  aux = [a12, m12, M12, M21, S12];
end
