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
 * Copyright (C) 2001-2006 Refractions Research Inc.
 * Copyright (C) 2017-2018 Daniel Baston <dbaston@gmail.com>
 *
 **********************************************************************/

#include "liblwgeom/liblwgeom.hpp"
#include "liblwgeom/liblwgeom_internal.hpp"
#include "liblwgeom/lwinline.hpp"

#include <cassert>

namespace duckdb {

LWCOLLECTION *lwgeom_as_lwcollection(const LWGEOM *lwgeom) {
	if (lwgeom == NULL)
		return NULL;
	if (lwgeom_is_collection(lwgeom))
		return (LWCOLLECTION *)lwgeom;
	else
		return NULL;
}

LWPOLY *lwgeom_as_lwpoly(const LWGEOM *lwgeom) {
	if (lwgeom == NULL)
		return NULL;
	if (lwgeom->type == POLYGONTYPE)
		return (LWPOLY *)lwgeom;
	else
		return NULL;
}

LWLINE *lwgeom_as_lwline(const LWGEOM *lwgeom) {
	if (lwgeom == NULL)
		return NULL;
	if (lwgeom->type == LINETYPE)
		return (LWLINE *)lwgeom;
	else
		return NULL;
}

LWTRIANGLE *lwgeom_as_lwtriangle(const LWGEOM *lwgeom) {
	if (lwgeom == NULL)
		return NULL;
	if (lwgeom->type == TRIANGLETYPE)
		return (LWTRIANGLE *)lwgeom;
	else
		return NULL;
}

LWCIRCSTRING *lwgeom_as_lwcircstring(const LWGEOM *lwgeom) {
	if (lwgeom == NULL)
		return NULL;
	if (lwgeom->type == CIRCSTRINGTYPE)
		return (LWCIRCSTRING *)lwgeom;
	else
		return NULL;
}

LWCOMPOUND *lwgeom_as_lwcompound(const LWGEOM *lwgeom) {
	if (lwgeom == NULL)
		return NULL;
	if (lwgeom->type == COMPOUNDTYPE)
		return (LWCOMPOUND *)lwgeom;
	else
		return NULL;
}

LWCURVEPOLY *lwgeom_as_lwcurvepoly(const LWGEOM *lwgeom) {
	if (lwgeom == NULL)
		return NULL;
	if (lwgeom->type == CURVEPOLYTYPE)
		return (LWCURVEPOLY *)lwgeom;
	else
		return NULL;
}

LWGEOM *lwcollection_as_lwgeom(const LWCOLLECTION *obj) {
	if (obj == NULL)
		return NULL;
	return (LWGEOM *)obj;
}

LWGEOM *lwpoly_as_lwgeom(const LWPOLY *obj) {
	if (obj == NULL)
		return NULL;
	return (LWGEOM *)obj;
}

LWGEOM *lwtriangle_as_lwgeom(const LWTRIANGLE *obj) {
	if (obj == NULL)
		return NULL;
	return (LWGEOM *)obj;
}

LWGEOM *lwline_as_lwgeom(const LWLINE *obj) {
	if (obj == NULL)
		return NULL;
	return (LWGEOM *)obj;
}

LWGEOM *lwcircstring_as_lwgeom(const LWCIRCSTRING *obj) {
	if (obj == NULL)
		return NULL;
	return (LWGEOM *)obj;
}

LWGEOM *lwcurvepoly_as_lwgeom(const LWCURVEPOLY *obj) {
	if (obj == NULL)
		return NULL;
	return (LWGEOM *)obj;
}

LWGEOM *lwpoint_as_lwgeom(const LWPOINT *obj) {
	if (obj == NULL)
		return NULL;
	return (LWGEOM *)obj;
}

/**
 * Deep-clone an #LWGEOM object. #POINTARRAY <em>are</em> copied.
 */
LWGEOM *lwgeom_clone_deep(const LWGEOM *lwgeom) {
	switch (lwgeom->type) {
	case POINTTYPE:
	case LINETYPE:
	case CIRCSTRINGTYPE:
	case TRIANGLETYPE:
		return (LWGEOM *)lwline_clone_deep((LWLINE *)lwgeom);
	case POLYGONTYPE:
		return (LWGEOM *)lwpoly_clone_deep((LWPOLY *)lwgeom);
	case COMPOUNDTYPE:
	case CURVEPOLYTYPE:
	case MULTICURVETYPE:
	case MULTISURFACETYPE:
	case MULTIPOINTTYPE:
	case MULTILINETYPE:
	case MULTIPOLYGONTYPE:
	case POLYHEDRALSURFACETYPE:
	case TINTYPE:
	case COLLECTIONTYPE:
		return (LWGEOM *)lwcollection_clone_deep((LWCOLLECTION *)lwgeom);
	default:
		lwerror("lwgeom_clone_deep: Unknown geometry type: %s", lwtype_name(lwgeom->type));
		return NULL;
	}
}

void lwgeom_set_srid(LWGEOM *geom, int32_t srid) {
	uint32_t i;

	geom->srid = srid;

	if (lwgeom_is_collection(geom)) {
		/* All the children are set to the same SRID value */
		LWCOLLECTION *col = lwgeom_as_lwcollection(geom);
		for (i = 0; i < col->ngeoms; i++) {
			lwgeom_set_srid(col->geoms[i], srid);
		}
	}
}

int32_t lwgeom_get_srid(const LWGEOM *geom) {
	if (!geom)
		return SRID_UNKNOWN;
	return geom->srid;
}

int lwgeom_has_z(const LWGEOM *geom) {
	if (!geom)
		return LW_FALSE;
	return FLAGS_GET_Z(geom->flags);
}

int lwgeom_needs_bbox(const LWGEOM *geom) {
	assert(geom);
	if (geom->type == POINTTYPE) {
		return LW_FALSE;
	} else if (geom->type == LINETYPE) {
		if (lwgeom_count_vertices(geom) <= 2)
			return LW_FALSE;
		else
			return LW_TRUE;
	} else {
		return LW_TRUE;
	}
}

/**
 * Count points in an #LWGEOM.
 * TODO: Make sure the internal functions don't overflow
 */
uint32_t lwgeom_count_vertices(const LWGEOM *geom) {
	int result = 0;

	/* Null? Zero. */
	if (!geom)
		return 0;

	/* Empty? Zero. */
	if (lwgeom_is_empty(geom))
		return 0;

	switch (geom->type) {
	case POINTTYPE:
		result = 1;
		break;
	case TRIANGLETYPE:
	case CIRCSTRINGTYPE:
	case LINETYPE:
		result = lwline_count_vertices((LWLINE *)geom);
		break;
	case POLYGONTYPE:
		result = lwpoly_count_vertices((LWPOLY *)geom);
		break;
	case COMPOUNDTYPE:
	case CURVEPOLYTYPE:
	case MULTICURVETYPE:
	case MULTISURFACETYPE:
	case MULTIPOINTTYPE:
	case MULTILINETYPE:
	case MULTIPOLYGONTYPE:
	case POLYHEDRALSURFACETYPE:
	case TINTYPE:
	case COLLECTIONTYPE:
		result = lwcollection_count_vertices((LWCOLLECTION *)geom);
		break;
	default:
		break;
	}
	return result;
}

/**
 * For an #LWGEOM, returns 0 for points, 1 for lines,
 * 2 for polygons, 3 for volume, and the max dimension
 * of a collection.
 */
int lwgeom_dimension(const LWGEOM *geom) {

	/* Null? Zero. */
	if (!geom)
		return -1;

	/* Empty? Zero. */
	/* if( lwgeom_is_empty(geom) ) return 0; */

	switch (geom->type) {
	case POINTTYPE:
	case MULTIPOINTTYPE:
		return 0;
	case CIRCSTRINGTYPE:
	case LINETYPE:
	case COMPOUNDTYPE:
	case MULTICURVETYPE:
	case MULTILINETYPE:
		return 1;
	case TRIANGLETYPE:
	case POLYGONTYPE:
	case CURVEPOLYTYPE:
	case MULTISURFACETYPE:
	case MULTIPOLYGONTYPE:
	case TINTYPE:
		return 2;
	case POLYHEDRALSURFACETYPE: {
		/* A closed polyhedral surface contains a volume. */
		int closed = lwpsurface_is_closed((LWPSURFACE *)geom);
		return (closed ? 3 : 2);
	}
	case COLLECTIONTYPE: {
		int maxdim = 0;
		uint32_t i;
		LWCOLLECTION *col = (LWCOLLECTION *)geom;
		for (i = 0; i < col->ngeoms; i++) {
			int dim = lwgeom_dimension(col->geoms[i]);
			maxdim = (dim > maxdim ? dim : maxdim);
		}
		return maxdim;
	}
	default:
		// lwerror("%s: unsupported input geometry type: %s", __func__, lwtype_name(geom->type));
		return -1;
	}
	return -1;
}

int lwgeom_has_srid(const LWGEOM *geom) {
	if (geom->srid != SRID_UNKNOWN)
		return LW_TRUE;

	return LW_FALSE;
}

/**
 * Ensure there's a box in the LWGEOM.
 * If the box is already there just return,
 * else compute it.
 */
void lwgeom_add_bbox(LWGEOM *lwgeom) {
	/* an empty LWGEOM has no bbox */
	if (lwgeom_is_empty(lwgeom))
		return;

	if (lwgeom->bbox)
		return;
	FLAGS_SET_BBOX(lwgeom->flags, 1);
	lwgeom->bbox = gbox_new(lwgeom->flags);
	lwgeom_calculate_gbox(lwgeom, lwgeom->bbox);
}

void lwgeom_free(LWGEOM *lwgeom) {
	/* There's nothing here to free... */
	if (!lwgeom)
		return;

	switch (lwgeom->type) {
	case POINTTYPE:
		lwpoint_free((LWPOINT *)lwgeom);
		break;

	case LINETYPE:
		lwline_free((LWLINE *)lwgeom);
		break;

	case POLYGONTYPE:
		lwpoly_free((LWPOLY *)lwgeom);
		break;

	case CIRCSTRINGTYPE:
		lwcircstring_free((LWCIRCSTRING *)lwgeom);
		break;

	case TRIANGLETYPE:
		lwtriangle_free((LWTRIANGLE *)lwgeom);
		break;

	case MULTIPOINTTYPE:
		lwmpoint_free((LWMPOINT *)lwgeom);
		break;

	case MULTILINETYPE:
		lwmline_free((LWMLINE *)lwgeom);
		break;

	case CURVEPOLYTYPE:
	case COMPOUNDTYPE:
	case MULTICURVETYPE:
	case MULTISURFACETYPE:
	case COLLECTIONTYPE:
		lwcollection_free((LWCOLLECTION *)lwgeom);
		break;

	default:
		// lwerror("lwgeom_free called with unknown type (%d) %s", lwgeom->type, lwtype_name(lwgeom->type));
		return;
	}
	return;
}

LWGEOM *lwgeom_force_2d(const LWGEOM *geom) {
	return lwgeom_force_dims(geom, 0, 0, 0, 0);
}

LWGEOM *lwgeom_force_dims(const LWGEOM *geom, int hasz, int hasm, double zval, double mval) {
	if (!geom)
		return NULL;
	switch (geom->type) {
	case POINTTYPE:
		return lwpoint_as_lwgeom(lwpoint_force_dims((LWPOINT *)geom, hasz, hasm, zval, mval));
	case CIRCSTRINGTYPE:
	case LINETYPE:
	case TRIANGLETYPE:
		return lwline_as_lwgeom(lwline_force_dims((LWLINE *)geom, hasz, hasm, zval, mval));
	case POLYGONTYPE:
		return lwpoly_as_lwgeom(lwpoly_force_dims((LWPOLY *)geom, hasz, hasm, zval, mval));
	case COMPOUNDTYPE:
	case CURVEPOLYTYPE:
	case MULTICURVETYPE:
	case MULTISURFACETYPE:
	case MULTIPOINTTYPE:
	case MULTILINETYPE:
	case MULTIPOLYGONTYPE:
	case POLYHEDRALSURFACETYPE:
	case TINTYPE:
	case COLLECTIONTYPE:
		return lwcollection_as_lwgeom(lwcollection_force_dims((LWCOLLECTION *)geom, hasz, hasm, zval, mval));
		// Need to do with postgis

	default:
		// lwerror("lwgeom_force_2d: unsupported geom type: %s", lwtype_name(geom->type));
		return NULL;
	}
}

int lwgeom_is_collection(const LWGEOM *geom) {
	if (!geom)
		return LW_FALSE;
	return lwtype_is_collection(geom->type);
}

/** Return TRUE if the geometry may contain sub-geometries, i.e. it is a MULTI* or COMPOUNDCURVE */
int lwtype_is_collection(uint8_t type) {
	switch (type) {
	case MULTIPOINTTYPE:
	case MULTILINETYPE:
	case MULTIPOLYGONTYPE:
	case COLLECTIONTYPE:
	case CURVEPOLYTYPE:
	case COMPOUNDTYPE:
	case MULTICURVETYPE:
	case MULTISURFACETYPE:
	case POLYHEDRALSURFACETYPE:
	case TINTYPE:
		return LW_TRUE;
		break;

	default:
		return LW_FALSE;
	}
}

/**
 * Given an lwtype number, what homogeneous collection can hold it?
 */
uint32_t lwtype_get_collectiontype(uint8_t type) {
	switch (type) {
	case POINTTYPE:
		return MULTIPOINTTYPE;
	case LINETYPE:
		return MULTILINETYPE;
	case POLYGONTYPE:
		return MULTIPOLYGONTYPE;
	case CIRCSTRINGTYPE:
		return MULTICURVETYPE;
	case COMPOUNDTYPE:
		return MULTICURVETYPE;
	case CURVEPOLYTYPE:
		return MULTISURFACETYPE;
	case TRIANGLETYPE:
		return TINTYPE;
	default:
		return COLLECTIONTYPE;
	}
}

/**
 * Calculate the gbox for this geometry, a cartesian box or
 * geodetic box, depending on how it is flagged.
 */
int lwgeom_calculate_gbox(const LWGEOM *lwgeom, GBOX *gbox) {
	gbox->flags = lwgeom->flags;
	if (FLAGS_GET_GEODETIC(lwgeom->flags))
		return lwgeom_calculate_gbox_geodetic(lwgeom, gbox);
	else
		return lwgeom_calculate_gbox_cartesian(lwgeom, gbox);
}

int lwgeom_startpoint(const LWGEOM *lwgeom, POINT4D *pt) {
	if (!lwgeom)
		return LW_FAILURE;

	switch (lwgeom->type) {
	case POINTTYPE:
		return ptarray_startpoint(((LWPOINT *)lwgeom)->point, pt);
	case TRIANGLETYPE:
	case CIRCSTRINGTYPE:
	case LINETYPE:
		return ptarray_startpoint(((LWLINE *)lwgeom)->points, pt);
	case POLYGONTYPE:
		return lwpoly_startpoint((LWPOLY *)lwgeom, pt);
	case TINTYPE:
	case CURVEPOLYTYPE:
	case COMPOUNDTYPE:
	case MULTIPOINTTYPE:
	case MULTILINETYPE:
	case MULTIPOLYGONTYPE:
	case COLLECTIONTYPE:
	case POLYHEDRALSURFACETYPE:
		return lwcollection_startpoint((LWCOLLECTION *)lwgeom, pt);
	// Need to do with postgis
	default:
		// lwerror("lwgeom_startpoint: unsupported geometry type: %s", lwtype_name(lwgeom->type));
		return LW_FAILURE;
	}
}

int lwgeom_is_closed(const LWGEOM *geom) {
	int type = geom->type;

	if (lwgeom_is_empty(geom))
		return LW_FALSE;

	/* Test linear types for closure */
	switch (type) {
	case LINETYPE:
		return lwline_is_closed((LWLINE *)geom);
	case POLYGONTYPE:
		return lwpoly_is_closed((LWPOLY *)geom);
	case CIRCSTRINGTYPE:
		return lwcircstring_is_closed((LWCIRCSTRING *)geom);
	case COMPOUNDTYPE:
		return lwcompound_is_closed((LWCOMPOUND *)geom);
	case TINTYPE:
		return lwtin_is_closed((LWTIN *)geom);
	case POLYHEDRALSURFACETYPE:
		return lwpsurface_is_closed((LWPSURFACE *)geom);
	}

	/* Recurse into collections and see if anything is not closed */
	if (lwgeom_is_collection(geom)) {
		LWCOLLECTION *col = lwgeom_as_lwcollection(geom);
		uint32_t i;
		int closed;
		for (i = 0; i < col->ngeoms; i++) {
			closed = lwgeom_is_closed(col->geoms[i]);
			if (!closed)
				return LW_FALSE;
		}
		return LW_TRUE;
	}

	/* All non-linear non-collection types we will call closed */
	return LW_TRUE;
}

} // namespace duckdb
