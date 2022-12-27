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
 * Copyright 2001-2006 Refractions Research Inc.
 * Copyright 2017-2018 Daniel Baston <dbaston@gmail.com>
 *
 **********************************************************************/

#include "postgis/lwgeom_functions_basic.hpp"

#include "liblwgeom/gserialized.hpp"
#include "liblwgeom/liblwgeom.hpp"
#include "liblwgeom/lwinline.hpp"
#include "libpgcommon/lwgeom_pg.hpp"

#include <float.h>

namespace duckdb {

GSERIALIZED *LWGEOM_makepoint(double x, double y) {
	LWPOINT *point;
	GSERIALIZED *result;

	point = lwpoint_make2d(SRID_UNKNOWN, x, y);

	result = geometry_serialize((LWGEOM *)point);
	lwgeom_free((LWGEOM *)point);

	return result;
}

GSERIALIZED *LWGEOM_makepoint(double x, double y, double z) {
	LWPOINT *point;
	GSERIALIZED *result;

	point = lwpoint_make3dz(SRID_UNKNOWN, x, y, z);

	result = geometry_serialize((LWGEOM *)point);
	lwgeom_free((LWGEOM *)point);

	return result;
}

GSERIALIZED *LWGEOM_makeline(GSERIALIZED *geom1, GSERIALIZED *geom2) {
	LWGEOM *lwgeoms[2];
	GSERIALIZED *result = NULL;
	LWLINE *outline;

	if ((gserialized_get_type(geom1) != POINTTYPE && gserialized_get_type(geom1) != LINETYPE) ||
	    (gserialized_get_type(geom2) != POINTTYPE && gserialized_get_type(geom2) != LINETYPE)) {
		// elog(ERROR, "Input geometries must be points or lines");
		return NULL;
	}

	gserialized_error_if_srid_mismatch(geom1, geom2, __func__);

	lwgeoms[0] = lwgeom_from_gserialized(geom1);
	lwgeoms[1] = lwgeom_from_gserialized(geom2);

	outline = lwline_from_lwgeom_array(lwgeoms[0]->srid, 2, lwgeoms);

	result = geometry_serialize((LWGEOM *)outline);

	lwgeom_free(lwgeoms[0]);
	lwgeom_free(lwgeoms[1]);

	return result;
}

GSERIALIZED *LWGEOM_makeline_garray(GSERIALIZED *gserArray[], int nelems) {
	GSERIALIZED *result = NULL;
	LWGEOM **geoms;
	LWGEOM *outlwg;
	uint32 ngeoms;
	int32_t srid = SRID_UNKNOWN;

	/* Return null on 0-elements input array */
	if (nelems == 0)
		return nullptr;

	/* possibly more then required */
	geoms = (LWGEOM **)malloc(sizeof(LWGEOM *) * nelems);
	ngeoms = 0;

	for (size_t i = 0; i < (size_t)nelems; i++) {
		GSERIALIZED *geom = gserArray[i];

		if (!geom)
			continue;

		if (gserialized_get_type(geom) != POINTTYPE && gserialized_get_type(geom) != LINETYPE &&
		    gserialized_get_type(geom) != MULTIPOINTTYPE) {
			continue;
		}

		geoms[ngeoms++] = lwgeom_from_gserialized(geom);

		/* Check SRID homogeneity */
		if (ngeoms == 1) {
			/* Get first geometry SRID */
			srid = geoms[ngeoms - 1]->srid;
			/* TODO: also get ZMflags */
		} else
			gserialized_error_if_srid_mismatch_reference(geom, srid, __func__);
	}

	/* Return null on 0-points input array */
	if (ngeoms == 0) {
		/* TODO: should we return LINESTRING EMPTY here ? */
		return nullptr;
	}

	outlwg = (LWGEOM *)lwline_from_lwgeom_array(srid, ngeoms, geoms);

	result = geometry_serialize(outlwg);
	lwgeom_free(outlwg);

	return result;
}

GSERIALIZED *LWGEOM_makepoly(GSERIALIZED *pglwg1, GSERIALIZED *gserArray[], int nholes) {
	GSERIALIZED *result = NULL;
	const LWLINE *shell = NULL;
	const LWLINE **holes = NULL;
	LWPOLY *outpoly;
	uint32 i;

	/* Get input shell */
	if (gserialized_get_type(pglwg1) != LINETYPE) {
		// lwpgerror("Shell is not a line");
		return nullptr;
	}
	shell = lwgeom_as_lwline(lwgeom_from_gserialized(pglwg1));

	/* Get input holes if any */
	if (nholes > 0) {
		holes = (const LWLINE **)lwalloc(sizeof(LWLINE *) * nholes);
		for (i = 0; i < (size_t)nholes; i++) {
			LWLINE *hole;
			auto g = gserArray[i];
			if (gserialized_get_type(g) != LINETYPE) {
				// lwpgerror("Hole %d is not a line", i);
				return nullptr;
			}
			hole = lwgeom_as_lwline(lwgeom_from_gserialized(g));
			holes[i] = hole;
		}
	}

	outpoly = lwpoly_from_lwlines(shell, nholes, holes);
	result = geometry_serialize((LWGEOM *)outpoly);

	lwline_free((LWLINE *)shell);

	for (i = 0; i < (size_t)nholes; i++) {
		lwline_free((LWLINE *)holes[i]);
	}

	return result;
}

double ST_distance(GSERIALIZED *geom1, GSERIALIZED *geom2) {
	double mindist;
	LWGEOM *lwgeom1 = lwgeom_from_gserialized(geom1);
	LWGEOM *lwgeom2 = lwgeom_from_gserialized(geom2);
	gserialized_error_if_srid_mismatch(geom1, geom2, __func__);

	mindist = lwgeom_mindistance2d(lwgeom1, lwgeom2);

	lwgeom_free(lwgeom1);
	lwgeom_free(lwgeom2);

	/* if called with empty geometries the ingoing mindistance is untouched, and makes us return NULL*/
	if (mindist < FLT_MAX)
		return mindist;

	PG_ERROR_NULL();
	return mindist;
}

lwvarlena_t *ST_GeoHash(GSERIALIZED *geom, size_t m_chars) {
	int precision = m_chars;
	lwvarlena_t *geohash = NULL;

	geohash = lwgeom_geohash((LWGEOM *)(lwgeom_from_gserialized(geom)), precision);
	if (geohash)
		return geohash;

	return nullptr;
}

bool ST_IsCollection(GSERIALIZED *geom) {
	int type = gserialized_get_type(geom);
	return lwtype_is_collection(type);
}

bool LWGEOM_isempty(GSERIALIZED *geom) {
	return gserialized_is_empty(geom);
}

/** number of points in an object */
int LWGEOM_npoints(GSERIALIZED *geom) {
	LWGEOM *lwgeom = lwgeom_from_gserialized(geom);
	int npoints = 0;

	npoints = lwgeom_count_vertices(lwgeom);
	lwgeom_free(lwgeom);

	return npoints;
}

/**
Returns the point in first input geometry that is closest to the second input geometry in 2d
*/
GSERIALIZED *LWGEOM_closestpoint(GSERIALIZED *geom1, GSERIALIZED *geom2) {
	GSERIALIZED *result;
	LWGEOM *point;
	LWGEOM *lwgeom1 = lwgeom_from_gserialized(geom1);
	LWGEOM *lwgeom2 = lwgeom_from_gserialized(geom2);
	gserialized_error_if_srid_mismatch(geom1, geom2, __func__);

	point = lwgeom_closest_point(lwgeom1, lwgeom2);

	if (lwgeom_is_empty(point))
		return nullptr;

	result = geometry_serialize(point);
	lwgeom_free(point);
	lwgeom_free(lwgeom1);
	lwgeom_free(lwgeom2);

	return result;
}

/**
Returns boolean describing if
mininimum 2d distance between objects in
geom1 and geom2 is shorter than tolerance
*/
bool LWGEOM_dwithin(GSERIALIZED *geom1, GSERIALIZED *geom2, double tolerance) {
	double mindist;
	LWGEOM *lwgeom1 = lwgeom_from_gserialized(geom1);
	LWGEOM *lwgeom2 = lwgeom_from_gserialized(geom2);

	if (tolerance < 0) {
		throw "Tolerance cannot be less than zero\n";
		return false;
	}

	gserialized_error_if_srid_mismatch(geom1, geom2, __func__);

	mindist = lwgeom_mindistance2d_tolerance(lwgeom1, lwgeom2, tolerance);

	/*empty geometries cases should be right handled since return from underlying
	 functions should be FLT_MAX which causes false as answer*/
	return tolerance >= mindist;
}

/**
 * @brief Calculate the area of all the subobj in a polygon
 * 		area(point) = 0
 * 		area (line) = 0
 * 		area(polygon) = find its 2d area
 */
double ST_Area(GSERIALIZED *geom) {
	LWGEOM *lwgeom = lwgeom_from_gserialized(geom);
	double area = 0.0;

	area = lwgeom_area(lwgeom);

	lwgeom_free(lwgeom);

	return area;
}

} // namespace duckdb
