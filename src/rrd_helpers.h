/*
 * rrd_helpers
 *
 * Copyright (C) 2013 Maxime Hadjinlian <maxime.hadjinlian@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#pragma once

#include <time.h>      // for time_t
#include <stdint.h>    // for uint_fast16_t, uint_least16_t

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))

int RRD_update(const char *rrd, const uint_fast16_t value, const time_t process_time);
int RRD_create(const char *rrd, const uint_least16_t step);
