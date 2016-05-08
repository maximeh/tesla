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

#include "rrd_helpers.h"
#include <rrd.h>       // for rrd_clear_error, rrd_get_error, rrd_test_error
#include <stdint.h>    // for uint_fast16_t, uint_least16_t
#include <stdio.h>     // for fprintf, sprintf, stderr, perror, NULL
#include <sys/time.h>  // for gettimeofday, timeval
#include <time.h>      // for time_t
#include "tesla.h"     // for DPRINTF

int
RRD_update(const char *rrd,
	const uint_fast16_t value, const time_t process_time)
{
	char val[20];
	if (sprintf(val, "%ld:%lu", process_time, value) < 0) {
		fprintf(stderr, "ERROR: Could not create string 'val'.\n");
		return -1;
	}

	rrd_clear_error();

	const char *argv[] = { val };
	rrd_update_r(rrd, NULL, 1, argv);

	if (rrd_test_error()) {
		fprintf(stderr, "ERROR: RRD_update (%s): %s\n", rrd,
				rrd_get_error());
		return -1;
	}
	DPRINTF(1, "Updated rrd %s with value %s\n", rrd, val);
	return 0;
}

int
RRD_create(const char *rrd, const uint_least16_t step)
{

	struct timeval tv;
	if (gettimeofday(&tv, NULL)) {
		perror("RRD_create");
		return -1;
	}
	time_t time_start = tv.tv_sec - 2629743;

	/* http://anders.olssons.info/_other/rrdcalc.html */
	const char *argv[] = {
		"DS:energy:GAUGE:240:0:U",
		"RRA:LAST:0.5:1:1",
		"RRA:AVERAGE:0.5:1:144",
		"RRA:AVERAGE:0.5:1:1440",
		"RRA:AVERAGE:0.5:1:10080",
		"RRA:AVERAGE:0.5:1:43800",
		"RRA:AVERAGE:0.5:10:52596",
		"RRA:MAX:0.5:1:144",
		"RRA:MAX:0.5:1:1440",
		"RRA:MAX:0.5:1:10080",
		"RRA:MAX:0.5:1:43800",
		"RRA:MAX:0.5:10:52596",
		"RRA:MIN:0.5:1:144",
		"RRA:MIN:0.5:1:1440",
		"RRA:MIN:0.5:1:10080",
		"RRA:MIN:0.5:1:43800",
		"RRA:MIN:0.5:10:52596"
	};

	rrd_clear_error();

	rrd_create_r(rrd, step, time_start, ARRAY_SIZE(argv), argv);

	if (rrd_test_error()) {
		fprintf(stderr, "ERROR: RRD_create: %s\n", rrd_get_error());
		return -1;
	}
	DPRINTF(1, "Created rrd %s\n", rrd);
	return 0;
}
