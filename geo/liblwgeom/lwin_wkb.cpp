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

#include "liblwgeom/lwin_wkt.hpp"
#include "liblwgeom/lwinline.hpp"

#include <cstring>
#include <limits.h>
#include <math.h>

namespace duckdb {

/** Max depth in a geometry. Matches the default YYINITDEPTH for WKT */
#define LW_PARSER_MAX_DEPTH 200

/**
 * Used for passing the parse state between the parsing functions.
 */
typedef struct {
	const uint8_t *wkb; /* Points to start of WKB */
	int32_t srid;       /* Current SRID we are handling */
	size_t wkb_size;    /* Expected size of WKB */
	int8_t swap_bytes;  /* Do an endian flip? */
	int8_t check;       /* Simple validity checks on geometries */
	int8_t lwtype;      /* Current type we are handling */
	int8_t has_z;       /* Z? */
	int8_t has_m;       /* M? */
	int8_t has_srid;    /* SRID? */
	int8_t error;       /* An error was found (not enough bytes to read) */
	uint8_t depth;      /* Current recursion level (to prevent stack overflows). Maxes at LW_PARSER_MAX_DEPTH */
	const uint8_t *pos; /* Current parse position */
} wkb_parse_state;

/**********************************************************************/

/* Our static character->number map. Anything > 15 is invalid */
static uint8_t hex2char[256] = {
    /* not Hex characters */
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    /* 0-9 */
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 20, 20, 20, 20, 20, 20,
    /* A-F */
    20, 10, 11, 12, 13, 14, 15, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    /* not Hex characters */
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    /* a-f */
    20, 10, 11, 12, 13, 14, 15, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    20, 20, 20,
    /* not Hex characters (upper 128 characters) */
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20,
    20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20};

/**
 * Internal function declarations.
 */
LWGEOM *lwgeom_from_wkb_state(wkb_parse_state *s);

/**
 * Check that we are not about to read off the end of the WKB
 * array.
 */
static inline void wkb_parse_state_check(wkb_parse_state *s, size_t next) {
	if ((s->pos + next) > (s->wkb + s->wkb_size)) {
		lwerror("WKB structure does not match expected size!");
		s->error = LW_TRUE;
	}
}

/**
 * Take in an unknown kind of wkb type number and ensure it comes out
 * as an extended WKB type number (with Z/M/SRID flags masked onto the
 * high bits).
 */
static void lwtype_from_wkb_state(wkb_parse_state *s, uint32_t wkb_type) {
	uint32_t wkb_simple_type;

	s->has_z = LW_FALSE;
	s->has_m = LW_FALSE;
	s->has_srid = LW_FALSE;

	/* If any of the higher bits are set, this is probably an extended type. */
	if (wkb_type & 0xF0000000) {
		if (wkb_type & WKBZOFFSET)
			s->has_z = LW_TRUE;
		if (wkb_type & WKBMOFFSET)
			s->has_m = LW_TRUE;
		if (wkb_type & WKBSRIDFLAG)
			s->has_srid = LW_TRUE;
	}

	/* Mask off the flags */
	wkb_type = wkb_type & 0x0FFFFFFF;

	/* Catch strange Oracle WKB type numbers */
	if (wkb_type >= 4000) {
		lwerror("Unknown WKB type (%d)!", wkb_type);
		return;
	}

	/* Strip out just the type number (1-12) from the ISO number (eg 3001-3012) */
	wkb_simple_type = wkb_type % 1000;

	/* Extract the Z/M information from ISO style numbers */
	if (wkb_type >= 3000 && wkb_type < 4000) {
		s->has_z = LW_TRUE;
		s->has_m = LW_TRUE;
	} else if (wkb_type >= 2000 && wkb_type < 3000) {
		s->has_m = LW_TRUE;
	} else if (wkb_type >= 1000 && wkb_type < 2000) {
		s->has_z = LW_TRUE;
	}

	switch (wkb_simple_type) {
	case WKB_POINT_TYPE:
		s->lwtype = POINTTYPE;
		break;
	case WKB_LINESTRING_TYPE:
		s->lwtype = LINETYPE;
		break;
	case WKB_POLYGON_TYPE:
		s->lwtype = POLYGONTYPE;
		break;
	case WKB_CIRCULARSTRING_TYPE:
		s->lwtype = CIRCSTRINGTYPE;
		break;
	case WKB_MULTIPOINT_TYPE:
		s->lwtype = MULTIPOINTTYPE;
		break;
	case WKB_MULTILINESTRING_TYPE:
		s->lwtype = MULTILINETYPE;
		break;
	case WKB_MULTIPOLYGON_TYPE:
		s->lwtype = MULTIPOLYGONTYPE;
		break;
	case WKB_TRIANGLE_TYPE:
		s->lwtype = TRIANGLETYPE;
		break;
	case WKB_GEOMETRYCOLLECTION_TYPE:
		s->lwtype = COLLECTIONTYPE;
		break;
	case WKB_COMPOUNDCURVE_TYPE:
		s->lwtype = COMPOUNDTYPE;
		break;
	case WKB_CURVEPOLYGON_TYPE:
		s->lwtype = CURVEPOLYTYPE;
		break;
	case WKB_MULTICURVE_TYPE:
		s->lwtype = MULTICURVETYPE;
		break;
	case WKB_MULTISURFACE_TYPE:
		s->lwtype = MULTISURFACETYPE;
		break;
	case WKB_POLYHEDRALSURFACE_TYPE:
		s->lwtype = POLYHEDRALSURFACETYPE;
		break;
	case WKB_TIN_TYPE:
		s->lwtype = TINTYPE;
		break;

	case WKB_CURVE_TYPE:
		s->lwtype = CURVEPOLYTYPE;
		break;
	case WKB_SURFACE_TYPE:
		s->lwtype = MULTICURVETYPE;
		break;

	default: /* Error! */
		lwerror("Unknown WKB type (%d)! Full WKB type number was (%d).", wkb_simple_type, wkb_type);
		break;
	}

	return;
}

/**
 * Byte
 * Read a byte and advance the parse state forward.
 */
static char byte_from_wkb_state(wkb_parse_state *s) {
	char char_value = 0;

	wkb_parse_state_check(s, WKB_BYTE_SIZE);
	if (s->error)
		return 0;

	char_value = s->pos[0];
	s->pos += WKB_BYTE_SIZE;

	return char_value;
}

uint8_t *bytes_from_hexbytes(const char *hexbuf, size_t hexsize) {
	uint8_t *buf = NULL;
	uint8_t h1, h2;
	uint32_t i;

	if (hexsize % 2) {
		lwerror("Invalid hex string, length (%d) has to be a multiple of two!", hexsize);
		return nullptr;
	}

	buf = (uint8_t *)lwalloc(hexsize / 2);

	if (!buf) {
		lwerror("Unable to allocate memory buffer.");
		return nullptr;
	}

	for (i = 0; i < hexsize / 2; i++) {
		h1 = hex2char[(int)hexbuf[2 * i]];
		h2 = hex2char[(int)hexbuf[2 * i + 1]];
		if (h1 > 15) {
			lwerror("Invalid hex character (%c) encountered", hexbuf[2 * i]);
			return nullptr;
		}
		if (h2 > 15) {
			lwerror("Invalid hex character (%c) encountered", hexbuf[2 * i + 1]);
			return nullptr;
		}
		/* First character is high bits, second is low bits */
		buf[i] = ((h1 & 0x0F) << 4) | (h2 & 0x0F);
	}
	return buf;
}

/**
 * WKB inputs *must* have a declared size, to prevent malformed WKB from reading
 * off the end of the memory segment (this stops a malevolent user from declaring
 * a one-ring polygon to have 10 rings, causing the WKB reader to walk off the
 * end of the memory).
 *
 * Check is a bitmask of: LW_PARSER_CHECK_MINPOINTS, LW_PARSER_CHECK_ODD,
 * LW_PARSER_CHECK_CLOSURE, LW_PARSER_CHECK_NONE, LW_PARSER_CHECK_ALL
 */
LWGEOM *lwgeom_from_wkb(const uint8_t *wkb, const size_t wkb_size, const char check) {
	wkb_parse_state s;

	/* Initialize the state appropriately */
	s.wkb = wkb;
	s.wkb_size = wkb_size;
	s.swap_bytes = LW_FALSE;
	s.check = check;
	s.lwtype = 0;
	s.srid = SRID_UNKNOWN;
	s.has_z = LW_FALSE;
	s.has_m = LW_FALSE;
	s.has_srid = LW_FALSE;
	s.error = LW_FALSE;
	s.pos = wkb;
	s.depth = 1;

	if (!wkb || !wkb_size)
		return NULL;

	return lwgeom_from_wkb_state(&s);
}

/**
 * Int32
 * Read 4-byte integer and advance the parse state forward.
 */
static uint32_t integer_from_wkb_state(wkb_parse_state *s) {
	uint32_t i = 0;

	wkb_parse_state_check(s, WKB_INT_SIZE);
	if (s->error)
		return 0;

	memcpy(&i, s->pos, WKB_INT_SIZE);

	/* Swap? Copy into a stack-allocated integer. */
	if (s->swap_bytes) {
		int j = 0;
		uint8_t tmp;

		for (j = 0; j < WKB_INT_SIZE / 2; j++) {
			tmp = ((uint8_t *)(&i))[j];
			((uint8_t *)(&i))[j] = ((uint8_t *)(&i))[WKB_INT_SIZE - j - 1];
			((uint8_t *)(&i))[WKB_INT_SIZE - j - 1] = tmp;
		}
	}

	s->pos += WKB_INT_SIZE;
	return i;
}

/**
 * Double
 * Read an 8-byte double and advance the parse state forward.
 */
static double double_from_wkb_state(wkb_parse_state *s) {
	double d = 0;

	memcpy(&d, s->pos, WKB_DOUBLE_SIZE);

	/* Swap? Copy into a stack-allocated integer. */
	if (s->swap_bytes) {
		int i = 0;
		uint8_t tmp;

		for (i = 0; i < WKB_DOUBLE_SIZE / 2; i++) {
			tmp = ((uint8_t *)(&d))[i];
			((uint8_t *)(&d))[i] = ((uint8_t *)(&d))[WKB_DOUBLE_SIZE - i - 1];
			((uint8_t *)(&d))[WKB_DOUBLE_SIZE - i - 1] = tmp;
		}
	}

	s->pos += WKB_DOUBLE_SIZE;
	return d;
}

/**
 * POINTARRAY
 * Read a dynamically sized point array and advance the parse state forward.
 * First read the number of points, then read the points.
 */
static POINTARRAY *ptarray_from_wkb_state(wkb_parse_state *s) {
	POINTARRAY *pa = NULL;
	size_t pa_size;
	uint32_t ndims = 2;
	uint32_t npoints = 0;
	static uint32_t maxpoints = UINT_MAX / WKB_DOUBLE_SIZE / 4;

	/* Calculate the size of this point array. */
	npoints = integer_from_wkb_state(s);
	if (s->error)
		return NULL;

	if (npoints > maxpoints) {
		s->error = LW_TRUE;
		return NULL;
	}

	if (s->has_z)
		ndims++;
	if (s->has_m)
		ndims++;
	pa_size = npoints * ndims * WKB_DOUBLE_SIZE;

	/* Empty! */
	if (npoints == 0)
		return ptarray_construct(s->has_z, s->has_m, npoints);

	/* Does the data we want to read exist? */
	wkb_parse_state_check(s, pa_size);
	if (s->error)
		return NULL;

	/* If we're in a native endianness, we can just copy the data directly! */
	if (!s->swap_bytes) {
		pa = ptarray_construct_copy_data(s->has_z, s->has_m, npoints, (uint8_t *)s->pos);
		s->pos += pa_size;
	}
	/* Otherwise we have to read each double, separately. */
	else {
		uint32_t i = 0;
		double *dlist;
		pa = ptarray_construct(s->has_z, s->has_m, npoints);
		dlist = (double *)(pa->serialized_pointlist);
		for (i = 0; i < npoints * ndims; i++) {
			dlist[i] = double_from_wkb_state(s);
		}
	}

	return pa;
}

/**
 * POINT
 * Read a WKB point, starting just after the endian byte,
 * type number and optional srid number.
 * Advance the parse state forward appropriately.
 * WKB point has just a set of doubles, with the quantity depending on the
 * dimension of the point, so this looks like a special case of the above
 * with only one point.
 */
static LWPOINT *lwpoint_from_wkb_state(wkb_parse_state *s) {
	static uint32_t npoints = 1;
	POINTARRAY *pa = NULL;
	size_t pa_size;
	uint32_t ndims = 2;
	const POINT2D *pt;

	/* Count the dimensions. */
	if (s->has_z)
		ndims++;
	if (s->has_m)
		ndims++;
	pa_size = ndims * WKB_DOUBLE_SIZE;

	/* Does the data we want to read exist? */
	wkb_parse_state_check(s, pa_size);
	if (s->error)
		return NULL;

	/* If we're in a native endianness, we can just copy the data directly! */
	if (!s->swap_bytes) {
		pa = ptarray_construct_copy_data(s->has_z, s->has_m, npoints, (uint8_t *)s->pos);
		s->pos += pa_size;
	}
	/* Otherwise we have to read each double, separately */
	else {
		uint32_t i = 0;
		double *dlist;
		pa = ptarray_construct(s->has_z, s->has_m, npoints);
		dlist = (double *)(pa->serialized_pointlist);
		for (i = 0; i < ndims; i++) {
			dlist[i] = double_from_wkb_state(s);
		}
	}

	/* Check for POINT(NaN NaN) ==> POINT EMPTY */
	pt = getPoint2d_cp(pa, 0);
	if (std::isnan(pt->x) && std::isnan(pt->y)) {
		ptarray_free(pa);
		return lwpoint_construct_empty(s->srid, s->has_z, s->has_m);
	} else {
		return lwpoint_construct(s->srid, NULL, pa);
	}
}

/**
 * LINESTRING
 * Read a WKB linestring, starting just after the endian byte,
 * type number and optional srid number. Advance the parse state
 * forward appropriately.
 * There is only one pointarray in a linestring. Optionally
 * check for minimal following of rules (two point minimum).
 */
static LWLINE *lwline_from_wkb_state(wkb_parse_state *s) {
	POINTARRAY *pa = ptarray_from_wkb_state(s);
	if (s->error)
		return NULL;

	if (pa == NULL || pa->npoints == 0) {
		if (pa)
			ptarray_free(pa);
		return lwline_construct_empty(s->srid, s->has_z, s->has_m);
	}

	if (s->check & LW_PARSER_CHECK_MINPOINTS && pa->npoints < 2) {
		auto error_msg = std::string(lwtype_name(s->lwtype)) + " must have at least two points";
		lwerror(error_msg.c_str());
		return NULL;
	}

	return lwline_construct(s->srid, NULL, pa);
}

/**
 * CIRCULARSTRING
 * Read a WKB circularstring, starting just after the endian byte,
 * type number and optional srid number. Advance the parse state
 * forward appropriately.
 * There is only one pointarray in a linestring. Optionally
 * check for minimal following of rules (three point minimum,
 * odd number of points).
 */
static LWCIRCSTRING *lwcircstring_from_wkb_state(wkb_parse_state *s) {
	POINTARRAY *pa = ptarray_from_wkb_state(s);
	if (s->error)
		return NULL;

	if (pa == NULL || pa->npoints == 0) {
		if (pa)
			ptarray_free(pa);
		return lwcircstring_construct_empty(s->srid, s->has_z, s->has_m);
	}

	if (s->check & LW_PARSER_CHECK_MINPOINTS && pa->npoints < 3) {
		return NULL;
	}

	if (s->check & LW_PARSER_CHECK_ODD && !(pa->npoints % 2)) {
		return NULL;
	}

	return lwcircstring_construct(s->srid, NULL, pa);
}

/**
 * POLYGON
 * Read a WKB polygon, starting just after the endian byte,
 * type number and optional srid number. Advance the parse state
 * forward appropriately.
 * First read the number of rings, then read each ring
 * (which are structured as point arrays)
 */
static LWPOLY *lwpoly_from_wkb_state(wkb_parse_state *s) {
	uint32_t nrings = integer_from_wkb_state(s);
	if (s->error)
		return NULL;
	uint32_t i = 0;
	LWPOLY *poly = lwpoly_construct_empty(s->srid, s->has_z, s->has_m);

	/* Empty polygon? */
	if (nrings == 0)
		return poly;

	for (i = 0; i < nrings; i++) {
		POINTARRAY *pa = ptarray_from_wkb_state(s);
		if (pa == NULL) {
			lwpoly_free(poly);
			return NULL;
		}

		/* Check for at least four points. */
		if (s->check & LW_PARSER_CHECK_MINPOINTS && pa->npoints < 4) {
			lwpoly_free(poly);
			ptarray_free(pa);
			lwerror("%s must have at least four points in each ring", lwtype_name(s->lwtype));
			return NULL;
		}

		/* Check that first and last points are the same. */
		if (s->check & LW_PARSER_CHECK_CLOSURE && !ptarray_is_closed_2d(pa)) {
			lwpoly_free(poly);
			ptarray_free(pa);
			lwerror("%s must have closed rings", lwtype_name(s->lwtype));
			return NULL;
		}

		/* Add ring to polygon */
		if (lwpoly_add_ring(poly, pa) == LW_FAILURE) {
			lwpoly_free(poly);
			ptarray_free(pa);
			lwerror("Unable to add ring to polygon");
			return NULL;
		}
	}
	return poly;
}

/**
 * TRIANGLE
 * Read a WKB triangle, starting just after the endian byte,
 * type number and optional srid number. Advance the parse state
 * forward appropriately.
 * Triangles are encoded like polygons in WKB, but more like linestrings
 * as lwgeometries.
 */
static LWTRIANGLE *lwtriangle_from_wkb_state(wkb_parse_state *s) {
	uint32_t nrings = integer_from_wkb_state(s);
	if (s->error)
		return NULL;

	/* Empty triangle? */
	if (nrings == 0)
		return lwtriangle_construct_empty(s->srid, s->has_z, s->has_m);

	/* Should be only one ring. */
	if (nrings != 1) {
		lwerror("Triangle has wrong number of rings: %d", nrings);
		return nullptr;
	}

	/* There's only one ring, we hope? */
	POINTARRAY *pa = ptarray_from_wkb_state(s);

	/* If there's no points, return an empty triangle. */
	if (pa == NULL)
		return lwtriangle_construct_empty(s->srid, s->has_z, s->has_m);

	/* Check for at least four points. */
	if (s->check & LW_PARSER_CHECK_MINPOINTS && pa->npoints < 4) {
		ptarray_free(pa);
		lwerror("%s must have at least four points", lwtype_name(s->lwtype));
		return NULL;
	}

	if (s->check & LW_PARSER_CHECK_ZCLOSURE && !ptarray_is_closed_z(pa)) {
		ptarray_free(pa);
		lwerror("%s must have closed rings", lwtype_name(s->lwtype));
		return NULL;
	}

	/* Empty TRIANGLE starts w/ empty POINTARRAY, free it first */
	return lwtriangle_construct(s->srid, NULL, pa);
}

/**
 * CURVEPOLYTYPE
 */
static LWCURVEPOLY *lwcurvepoly_from_wkb_state(wkb_parse_state *s) {
	uint32_t ngeoms = integer_from_wkb_state(s);
	if (s->error)
		return NULL;
	LWCURVEPOLY *cp = lwcurvepoly_construct_empty(s->srid, s->has_z, s->has_m);
	LWGEOM *geom = NULL;
	uint32_t i;

	/* Empty collection? */
	if (ngeoms == 0)
		return cp;

	for (i = 0; i < ngeoms; i++) {
		geom = lwgeom_from_wkb_state(s);
		if (lwcurvepoly_add_ring(cp, geom) == LW_FAILURE) {
			lwgeom_free(geom);
			lwgeom_free((LWGEOM *)cp);
			lwerror("Unable to add geometry (%p) to curvepoly (%p)", geom, cp);
			return NULL;
		}
	}

	return cp;
}

/**
 * COLLECTION, MULTIPOINTTYPE, MULTILINETYPE, MULTIPOLYGONTYPE, COMPOUNDTYPE,
 * MULTICURVETYPE, MULTISURFACETYPE,
 * TINTYPE
 */
static LWCOLLECTION *lwcollection_from_wkb_state(wkb_parse_state *s) {
	uint32_t ngeoms = integer_from_wkb_state(s);
	if (s->error)
		return NULL;
	LWCOLLECTION *col = lwcollection_construct_empty(s->lwtype, s->srid, s->has_z, s->has_m);
	LWGEOM *geom = NULL;
	uint32_t i;

	/* Empty collection? */
	if (ngeoms == 0)
		return col;

	/* Be strict in polyhedral surface closures */
	if (s->lwtype == POLYHEDRALSURFACETYPE)
		s->check |= LW_PARSER_CHECK_ZCLOSURE;

	s->depth++;
	if (s->depth >= LW_PARSER_MAX_DEPTH) {
		lwcollection_free(col);
		lwerror("Geometry has too many chained collections");
		return NULL;
	}
	for (i = 0; i < ngeoms; i++) {
		geom = lwgeom_from_wkb_state(s);
		if (lwcollection_add_lwgeom(col, geom) == NULL) {
			lwgeom_free(geom);
			lwgeom_free((LWGEOM *)col);
			lwerror("Unable to add geometry (%p) to collection (%p)", geom, col);
			return NULL;
		}
	}
	s->depth--;

	return col;
}

/**
 * GEOMETRY
 * Generic handling for WKB geometries. The front of every WKB geometry
 * (including those embedded in collections) is an endian byte, a type
 * number and an optional srid number. We handle all those here, then pass
 * to the appropriate handler for the specific type.
 */
LWGEOM *lwgeom_from_wkb_state(wkb_parse_state *s) {
	char wkb_little_endian;
	uint32_t wkb_type;

	/* Fail when handed incorrect starting byte */
	wkb_little_endian = byte_from_wkb_state(s);
	if (s->error)
		return NULL;
	if (wkb_little_endian != 1 && wkb_little_endian != 0) {
		lwerror("Invalid endian flag value encountered.");
		return NULL;
	}

	/* Check the endianness of our input  */
	s->swap_bytes = LW_FALSE;

	/* Machine arch is big endian, request is for little */
	if (IS_BIG_ENDIAN && wkb_little_endian)
		s->swap_bytes = LW_TRUE;
	/* Machine arch is little endian, request is for big */
	else if ((!IS_BIG_ENDIAN) && (!wkb_little_endian))
		s->swap_bytes = LW_TRUE;

	/* Read the type number */
	wkb_type = integer_from_wkb_state(s);
	if (s->error)
		return NULL;
	lwtype_from_wkb_state(s, wkb_type);

	/* Read the SRID, if necessary */
	if (s->has_srid) {
		s->srid = clamp_srid(integer_from_wkb_state(s));
		if (s->error)
			return NULL;
		/* TODO: warn on explicit UNKNOWN srid ? */
	}

	/* Do the right thing */
	switch (s->lwtype) {
	case POINTTYPE:
		return (LWGEOM *)lwpoint_from_wkb_state(s);
		break;
	case LINETYPE:
		return (LWGEOM *)lwline_from_wkb_state(s);
		break;
	case POLYGONTYPE:
		return (LWGEOM *)lwpoly_from_wkb_state(s);
		break;
	case CIRCSTRINGTYPE:
		return (LWGEOM *)lwcircstring_from_wkb_state(s);
		break;
	case TRIANGLETYPE:
		return (LWGEOM *)lwtriangle_from_wkb_state(s);
		break;
	case CURVEPOLYTYPE:
		return (LWGEOM *)lwcurvepoly_from_wkb_state(s);
		break;
	case MULTIPOINTTYPE:
	case MULTILINETYPE:
	case MULTIPOLYGONTYPE:
	case COMPOUNDTYPE:
	case MULTICURVETYPE:
	case MULTISURFACETYPE:
	case POLYHEDRALSURFACETYPE:
	case TINTYPE:
	case COLLECTIONTYPE:
		return (LWGEOM *)lwcollection_from_wkb_state(s);
	/* Unknown type! */
	default: {
		lwerror("%s: Unsupported geometry type: %s", __func__, lwtype_name(s->lwtype));
		return NULL;
	}
	}

	/* Return value to keep compiler happy. */
	return NULL;
}

} // namespace duckdb
