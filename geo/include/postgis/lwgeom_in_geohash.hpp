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
 * Copyright 2012 J Smith <dark.panda@gmail.com>
 *
 **********************************************************************/

#pragma once
#include "duckdb.hpp"
#include "liblwgeom/liblwgeom.hpp"

namespace duckdb {

GBOX *box2d_from_geohash(char *hash, int precision = -1);

GSERIALIZED *LWGEOM_from_GeoHash(char *hash, int precision = -1);

} // namespace duckdb
