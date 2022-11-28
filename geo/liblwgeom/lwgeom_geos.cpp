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
 * Copyright 2011-2020 Sandro Santilli <strk@kbt.io>
 * Copyright 2015-2018 Daniel Baston <dbaston@gmail.com>
 * Copyright 2017-2018 Darafei Praliaskouski <me@komzpa.net>
 *
 **********************************************************************/

#include "liblwgeom/lwgeom_geos.hpp"

// #include "geos_c.hpp"
#include "liblwgeom/liblwgeom.hpp"
#include "liblwgeom/liblwgeom_internal.hpp"
#include "liblwgeom/lwinline.hpp"

#include <stdarg.h>
#include <stdlib.h>

namespace duckdb {

#define AUTOFIX                    LW_TRUE
#define LWGEOM_GEOS_ERRMSG_MAXSIZE 256
char lwgeom_geos_errmsg[LWGEOM_GEOS_ERRMSG_MAXSIZE];

extern void lwgeom_geos_error(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	/* Call the supplied function */
	if (LWGEOM_GEOS_ERRMSG_MAXSIZE - 1 < vsnprintf(lwgeom_geos_errmsg, LWGEOM_GEOS_ERRMSG_MAXSIZE - 1, fmt, ap))
		lwgeom_geos_errmsg[LWGEOM_GEOS_ERRMSG_MAXSIZE - 1] = '\0';

	va_end(ap);
}

/* Destroy any non-null GEOSGeometry* pointers passed as arguments */
#define GEOS_FREE(...)                                                                                                 \
	do {                                                                                                               \
		geos_destroy((sizeof((void *[]) {__VA_ARGS__}) / sizeof(void *)), __VA_ARGS__);                                \
	} while (0)

/* Pass the latest GEOS error to lwerror, then return NULL */
#define GEOS_FAIL()                                                                                                    \
	do {                                                                                                               \
		lwerror("%s: GEOS Error: %s", __func__, lwgeom_geos_errmsg);                                                   \
		return NULL;                                                                                                   \
	} while (0)

#define GEOS_FREE_AND_FAIL(...)                                                                                        \
	do {                                                                                                               \
		GEOS_FREE(__VA_ARGS__);                                                                                        \
		GEOS_FAIL();                                                                                                   \
	} while (0)

/* Return the consistent SRID of all inputs, or call lwerror
 * in case of SRID mismatch. */
#define RESULT_SRID(...)                                                                                               \
	(get_result_srid((sizeof((const void *[]) {__VA_ARGS__}) / sizeof(void *)), __func__, __VA_ARGS__))

/* Free any non-null GEOSGeometry* pointers passed as arguments *
 * Called by GEOS_FREE, which populates 'count' */
static void geos_destroy(size_t count, ...) {
	va_list ap;
	va_start(ap, count);
	while (count--) {
		// GEOSGeometry *g = va_arg(ap, GEOSGeometry *);
		// if (g) {
		// 	GEOSGeom_destroy(g);
		// }
	}
}

/* Return the consistent SRID of all input.
 * Intended to be called from RESULT_SRID macro */
static int32_t get_result_srid(size_t count, const char *funcname, ...) {
	va_list ap;
	va_start(ap, funcname);
	int32_t srid = SRID_INVALID;
	size_t i;
	for (i = 0; i < count; i++) {
		LWGEOM *g = va_arg(ap, LWGEOM *);
		if (!g) {
			lwerror("%s: Geometry is null", funcname);
			return SRID_INVALID;
		}
		if (i == 0) {
			srid = g->srid;
		} else {
			if (g->srid != srid) {
				lwerror("%s: Operation on mixed SRID geometries (%d != %d)", funcname, srid, g->srid);
				return SRID_INVALID;
			}
		}
	}
	return srid;
}

// GEOSCoordSeq ptarray_to_GEOSCoordSeq(const POINTARRAY *pa, uint8_t fix_ring) {
// 	uint32_t dims = 2;
// 	uint32_t i;
// 	int append_points = 0;
// 	const POINT3D *p3d = NULL;
// 	const POINT2D *p2d = NULL;
// 	GEOSCoordSeq sq;

// 	if (FLAGS_GET_Z(pa->flags))
// 		dims = 3;

// 	if (fix_ring) {
// 		if (pa->npoints < 1) {
// 			lwerror("ptarray_to_GEOSCoordSeq called with fix_ring and 0 vertices in ring, cannot fix");
// 			return NULL;
// 		} else {
// 			if (pa->npoints < 4)
// 				append_points = 4 - pa->npoints;
// 			if (!ptarray_is_closed_2d(pa) && append_points == 0)
// 				append_points = 1;
// 		}
// 	}

// 	if (!(sq = GEOSCoordSeq_create(pa->npoints + append_points, dims))) {
// 		lwerror("Error creating GEOS Coordinate Sequence");
// 		return NULL;
// 	}

// 	for (i = 0; i < pa->npoints; i++) {
// 		if (dims == 3) {
// 			p3d = getPoint3d_cp(pa, i);
// 			p2d = (const POINT2D *)p3d;
// 		} else {
// 			p2d = getPoint2d_cp(pa, i);
// 		}

// 		if (dims == 3)
// 			GEOSCoordSeq_setXYZ(sq, i, p2d->x, p2d->y, p3d->z);
// 		else
// 			GEOSCoordSeq_setXY(sq, i, p2d->x, p2d->y);
// 	}

// 	if (append_points) {
// 		if (dims == 3) {
// 			p3d = getPoint3d_cp(pa, 0);
// 			p2d = (const POINT2D *)p3d;
// 		} else
// 			p2d = getPoint2d_cp(pa, 0);
// 		for (i = pa->npoints; i < pa->npoints + append_points; i++) {
// 			GEOSCoordSeq_setXY(sq, i, p2d->x, p2d->y);

// 			if (dims == 3)
// 				GEOSCoordSeq_setZ(sq, i, p3d->z);
// 		}
// 	}

// 	return sq;
// }

// static inline GEOSGeometry *ptarray_to_GEOSLinearRing(const POINTARRAY *pa, uint8_t autofix) {
// 	GEOSCoordSeq sq;
// 	GEOSGeom g;
// 	sq = ptarray_to_GEOSCoordSeq(pa, autofix);
// 	g = GEOSGeom_createLinearRing(sq);
// 	return g;
// }

// GEOSGeometry *LWGEOM2GEOS(const LWGEOM *lwgeom, uint8_t autofix) {
// 	GEOSCoordSeq sq;
// 	GEOSGeom g, shell;
// 	GEOSGeom *geoms = NULL;
// 	uint32_t ngeoms, i, j;
// 	int geostype;

// 	if (autofix) {
// 		/* cross fingers and try without autofix, maybe it'll work? */
// 		g = LWGEOM2GEOS(lwgeom, LW_FALSE);
// 		if (g)
// 			return g;
// 	}

// 	if (lwgeom_has_arc(lwgeom)) {
// 		LWGEOM *lwgeom_stroked = lwgeom_stroke(lwgeom, 32);
// 		GEOSGeometry *g = LWGEOM2GEOS(lwgeom_stroked, autofix);
// 		lwgeom_free(lwgeom_stroked);
// 		return g;
// 	}

// 	LWPOINT *lwp = NULL;
// 	LWPOLY *lwpoly = NULL;
// 	LWLINE *lwl = NULL;
// 	LWCOLLECTION *lwc = NULL;

// 	switch (lwgeom->type) {
// 	case POINTTYPE:
// 		lwp = (LWPOINT *)lwgeom;

// 		if (lwgeom_is_empty(lwgeom))
// 			g = GEOSGeom_createEmptyPolygon();
// 		else {
// 			if (lwgeom_has_z(lwgeom)) {
// 				sq = ptarray_to_GEOSCoordSeq(lwp->point, 0);
// 				g = GEOSGeom_createPoint(sq);
// 			} else {
// 				const POINT2D *p = getPoint2d_cp(lwp->point, 0);
// 				g = GEOSGeom_createPointFromXY(p->x, p->y);
// 			}
// 		}
// 		if (!g)
// 			return NULL;
// 		break;

// 	case LINETYPE:
// 		lwl = (LWLINE *)lwgeom;
// 		/* TODO: if (autofix) */
// 		if (lwl->points->npoints == 1) {
// 			/* Duplicate point, to make geos-friendly */
// 			lwl->points = ptarray_addPoint(lwl->points, getPoint_internal(lwl->points, 0),
// 			                               FLAGS_NDIMS(lwl->points->flags), lwl->points->npoints);
// 		}
// 		sq = ptarray_to_GEOSCoordSeq(lwl->points, 0);
// 		g = GEOSGeom_createLineString(sq);
// 		if (!g)
// 			return NULL;
// 		break;

// 	case POLYGONTYPE:
// 		lwpoly = (LWPOLY *)lwgeom;
// 		if (lwgeom_is_empty(lwgeom))
// 			g = GEOSGeom_createEmptyPolygon();
// 		else {
// 			shell = ptarray_to_GEOSLinearRing(lwpoly->rings[0], autofix);
// 			if (!shell)
// 				return NULL;
// 			ngeoms = lwpoly->nrings - 1;
// 			if (ngeoms > 0)
// 				geoms = (GEOSGeom *)lwalloc(sizeof(GEOSGeom) * ngeoms);

// 			for (i = 1; i < lwpoly->nrings; i++) {
// 				geoms[i - 1] = ptarray_to_GEOSLinearRing(lwpoly->rings[i], autofix);
// 				if (!geoms[i - 1]) {
// 					uint32_t k;
// 					for (k = 0; k < i - 1; k++)
// 						GEOSGeom_destroy(geoms[k]);
// 					lwfree(geoms);
// 					GEOSGeom_destroy(shell);
// 					return NULL;
// 				}
// 			}
// 			g = GEOSGeom_createPolygon(shell, geoms, ngeoms);
// 			if (geoms)
// 				lwfree(geoms);
// 		}
// 		if (!g)
// 			return NULL;
// 		break;

// 	case TRIANGLETYPE:
// 		if (lwgeom_is_empty(lwgeom))
// 			g = GEOSGeom_createEmptyPolygon();
// 		else {
// 			LWTRIANGLE *lwt = (LWTRIANGLE *)lwgeom;
// 			shell = ptarray_to_GEOSLinearRing(lwt->points, autofix);
// 			if (!shell)
// 				return NULL;
// 			g = GEOSGeom_createPolygon(shell, NULL, 0);
// 		}
// 		if (!g)
// 			return NULL;
// 		break;
// 	case MULTIPOINTTYPE:
// 	case MULTILINETYPE:
// 	case MULTIPOLYGONTYPE:
// 	case TINTYPE:
// 	case COLLECTIONTYPE:
// 		if (lwgeom->type == MULTIPOINTTYPE)
// 			geostype = GEOS_MULTIPOINT;
// 		else if (lwgeom->type == MULTILINETYPE)
// 			geostype = GEOS_MULTILINESTRING;
// 		else if (lwgeom->type == MULTIPOLYGONTYPE)
// 			geostype = GEOS_MULTIPOLYGON;
// 		else
// 			geostype = GEOS_GEOMETRYCOLLECTION;

// 		lwc = (LWCOLLECTION *)lwgeom;

// 		ngeoms = lwc->ngeoms;
// 		if (ngeoms > 0)
// 			geoms = (GEOSGeom *)lwalloc(sizeof(GEOSGeom) * ngeoms);

// 		j = 0;
// 		for (i = 0; i < ngeoms; ++i) {
// 			GEOSGeometry *g;

// 			if (lwgeom_is_empty(lwc->geoms[i]))
// 				continue;

// 			g = LWGEOM2GEOS(lwc->geoms[i], 0);
// 			if (!g) {
// 				uint32_t k;
// 				for (k = 0; k < j; k++)
// 					GEOSGeom_destroy(geoms[k]);
// 				lwfree(geoms);
// 				return NULL;
// 			}
// 			geoms[j++] = g;
// 		}
// 		g = GEOSGeom_createCollection(geostype, geoms, j);
// 		if (ngeoms > 0)
// 			lwfree(geoms);
// 		if (!g)
// 			return NULL;
// 		break;

// 	default:
// 		lwerror("Unknown geometry type: %d - %s", lwgeom->type, lwtype_name(lwgeom->type));
// 		return NULL;
// 	}

// 	GEOSSetSRID(g, lwgeom->srid);

// 	return g;
// }

LWGEOM *lwgeom_difference_prec(const LWGEOM *geom1, const LWGEOM *geom2, double prec) {
	LWGEOM *result = nullptr;
	// 	int32_t srid = RESULT_SRID(geom1, geom2);
	// 	uint8_t is3d = (FLAGS_GET_Z(geom1->flags) || FLAGS_GET_Z(geom2->flags));
	// 	GEOSGeometry *g1, *g2, *g3;

	// 	if (srid == SRID_INVALID)
	// 		return NULL;

	// 	/* A.Intersection(Empty) == Empty */
	// 	if (lwgeom_is_empty(geom2))
	// 		return lwgeom_clone_deep(geom1); /* match empty type? */

	// 	/* Empty.Intersection(A) == Empty */
	// 	if (lwgeom_is_empty(geom1))
	// 		return lwgeom_clone_deep(geom1); /* match empty type? */

	// 	initGEOS(lwnotice, lwgeom_geos_error);

	// 	if (!(g1 = LWGEOM2GEOS(geom1, AUTOFIX)))
	// 		GEOS_FAIL();
	// 	if (!(g2 = LWGEOM2GEOS(geom2, AUTOFIX)))
	// 		GEOS_FREE_AND_FAIL(g1);

	// 	if (prec >= 0) {
	// #if POSTGIS_GEOS_VERSION < 39
	// 		lwerror("Fixed-precision difference requires GEOS-3.9 or higher");
	// 		GEOS_FREE_AND_FAIL(g1, g2);
	// 		return NULL;
	// #else
	// 		g3 = GEOSDifferencePrec(g1, g2, prec);
	// #endif
	// 	} else {
	// 		g3 = GEOSDifference(g1, g2);
	// 	}

	// 	if (!g3)
	// 		GEOS_FREE_AND_FAIL(g1, g2);
	// 	GEOSSetSRID(g3, srid);

	// 	if (!(result = GEOS2LWGEOM(g3, is3d)))
	// 		GEOS_FREE_AND_FAIL(g1, g2, g3);

	// 	GEOS_FREE(g1, g2, g3);
	return result;
}

LWGEOM *lwgeom_union_prec(const LWGEOM *geom1, const LWGEOM *geom2, double gridSize) {
	LWGEOM *result = nullptr;
// 	int32_t srid = RESULT_SRID(geom1, geom2);
// 	uint8_t is3d = (FLAGS_GET_Z(geom1->flags) || FLAGS_GET_Z(geom2->flags));
// 	GEOSGeometry *g1, *g2, *g3;

// 	if (srid == SRID_INVALID)
// 		return NULL;

// 	/* A.Union(empty) == A */
// 	if (lwgeom_is_empty(geom1))
// 		return lwgeom_clone_deep(geom2);

// 	/* B.Union(empty) == B */
// 	if (lwgeom_is_empty(geom2))
// 		return lwgeom_clone_deep(geom1);

// 	initGEOS(lwnotice, lwgeom_geos_error);

// 	if (!(g1 = LWGEOM2GEOS(geom1, AUTOFIX)))
// 		GEOS_FAIL();
// 	if (!(g2 = LWGEOM2GEOS(geom2, AUTOFIX)))
// 		GEOS_FREE_AND_FAIL(g1);

// 	if (gridSize >= 0) {
// #if POSTGIS_GEOS_VERSION < 39
// 		lwerror("Fixed-precision union requires GEOS-3.9 or higher");
// 		GEOS_FREE_AND_FAIL(g1, g2);
// 		return NULL;
// #else
// 		g3 = GEOSUnionPrec(g1, g2, gridSize);
// #endif
// 	} else {
// 		g3 = GEOSUnion(g1, g2);
// 	}

// 	if (!g3)
// 		GEOS_FREE_AND_FAIL(g1, g2);
// 	GEOSSetSRID(g3, srid);

// 	if (!(result = GEOS2LWGEOM(g3, is3d)))
// 		GEOS_FREE_AND_FAIL(g1, g2, g3);

// 	GEOS_FREE(g1, g2, g3);
	return result;
}

} // namespace duckdb