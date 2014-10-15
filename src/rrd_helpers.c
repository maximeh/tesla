// vim: set expandtab:set tabstop=2
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
#include "tesla.h"

int
RRD_update(char *rrd, const unsigned int value, const long process_time)
{
        char val[20];

        if (sprintf(val, "%ld:%u", process_time, value) < 0) {
                fprintf(stderr, "ERROR: Could not create string 'val'.\n");
                return -1;
        }

        char *argv[3] = { "dummy", rrd, val };

        optind = 0;
        opterr = 0;
        rrd_clear_error();
        rrd_update(3, argv);
        if (rrd_test_error()) {
                fprintf(stderr, "ERROR: RRD_update (%s): %s\n", rrd,
                        rrd_get_error());
                return -1;
        }
        DPRINTF(1, "Updated rrd %s with value %s\n", rrd, val);
        return 0;
}

int
RRD_create(char *rrd, const unsigned int step)
{
        char *argv[13];
        int argc = 0;
        char s[16], start[64];
        char energy[64];

        struct timeval tv;
        if (gettimeofday(&tv, NULL)) {
                perror("RRD_create");
                return -1;
        }

        argv[argc++] = "dummy";
        argv[argc++] = rrd;
        argv[argc++] = "--step";
        if (sprintf(s, "%u", step) < 0) {
                fprintf(stderr, "ERROR: Could not create string 's'.\n");
                return -1;
        }

        argv[argc++] = s;
        argv[argc++] = "--start";
        // Start a month ago, because that's the history that may be stored in the device.
        if (sprintf(start, "%lu", (unsigned long)tv.tv_sec - 2629743) < 0) {
                fprintf(stderr, "ERROR: Could not create string 'start'.\n");
                return -1;
        }

        argv[argc++] = start;
        if (sprintf(energy, "DS:energy:GAUGE:60:0:U") < 0) {
                fprintf(stderr, "ERROR: Could not create string 'energy'.\n");
                return -1;
        }

        argv[argc++] = energy;
        // Keep the last value
        argv[argc++] = "RRA:LAST:0.5:1:1";

        // http://eccentric.cx/misc/rrdcalc.html
        // Hourly, keep 1 data every step (60s), keep 60 of them, 60*60 = 1h
        argv[argc++] = "RRA:AVERAGE:0.5:1:60";
        // Daily, keep 1 cons. data every 30 step, keep 48 of them, 60*30*48 = 24h
        argv[argc++] = "RRA:AVERAGE:0.5:30:48";
        // Weekly, every 720 data, keep one, keep 14 of them. 720*60*14 = 1 week
        argv[argc++] = "RRA:AVERAGE:0.5:720:14";
        // Monthly, every 1440 data, keep one. Keep 30 of them, 1440*60*30 = 1 month
        argv[argc++] = "RRA:AVERAGE:0.5:1440:30";
        // Yearly, every 10080 minutes (a week), keep one. Keep 52 of them..
        argv[argc++] = "RRA:AVERAGE:0.5:10080:52";

        optind = 0;
        opterr = 0;
        rrd_clear_error();
        rrd_create(argc, argv);
        if (rrd_test_error()) {
                fprintf(stderr, "ERROR: RRD_create: %s\n", rrd_get_error());
                return -1;
        }
        DPRINTF(1, "Created rrd %s\n", rrd);
        return 0;
}
