/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * PostGIS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * PostGIS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PostGIS.  If not, see <http://www.gnu.org/licenses/>.
 *
 **********************************************************************
 *
 * Copyright (C) 2009 Paul Ramsey <pramsey@cleverelephant.ca>
 *
 **********************************************************************/

#include "postgis/geography_measurement.hpp"

#include "liblwgeom/gserialized.hpp"
#include "liblwgeom/liblwgeom.hpp"
#include "liblwgeom/liblwgeom_internal.hpp"
#include "liblwgeom/lwgeodetic_tree.hpp"
#include "libpgcommon/lwgeom_pg.hpp"
#include "libpgcommon/lwgeom_transform.hpp"
#include "postgis/geography_measurement_trees.hpp"

namespace duckdb {

#ifdef PROJ_GEODESIC
/* round to 10 nm precision */
#define INVMINDIST 1.0e8
#else
/* round to 100 nm precision */
#define INVMINDIST 1.0e7
#endif

/*
 ** geography_distance(GSERIALIZED *g1, GSERIALIZED *g2, double tolerance, boolean use_spheroid)
 ** returns double distance in meters
 */
double geography_distance(GSERIALIZED *g1, GSERIALIZED *g2, bool use_spheroid) {
	double distance;
	SPHEROID s;

	gserialized_error_if_srid_mismatch(g1, g2, __func__);

	/* Initialize spheroid */
	spheroid_init_from_srid(gserialized_get_srid(g1), &s);

	/* Set to sphere if requested */
	if (!use_spheroid)
		s.a = s.b = s.radius;

	/* Return NULL on empty arguments. */
	if (gserialized_is_empty(g1) || gserialized_is_empty(g2)) {
		PG_ERROR_NULL();
	}

	/* Do the brute force calculation if the cached calculation doesn't tick over */
	// if (LW_FAILURE == geography_distance_cache(fcinfo, shared_geom1, shared_geom2, &s, &distance))
	// {
	/* default to using tree-based distance calculation at all times */
	/* in standard distance call. */
	geography_tree_distance(g1, g2, &s, FP_TOLERANCE, &distance);
	/*
	LWGEOM* lwgeom1 = lwgeom_from_gserialized(g1);
	LWGEOM* lwgeom2 = lwgeom_from_gserialized(g2);
	distance = lwgeom_distance_spheroid(lwgeom1, lwgeom2, &s, tolerance);
	lwgeom_free(lwgeom1);
	lwgeom_free(lwgeom2);
	*/
	// }

	/* Knock off any funny business at the nanometer level, ticket #2168 */
	distance = round(distance * INVMINDIST) / INVMINDIST;

	/* Something went wrong, negative return... should already be eloged, return NULL */
	if (distance < 0.0) {
		PG_ERROR_NULL();
	}

	return distance;
}

} // namespace duckdb
