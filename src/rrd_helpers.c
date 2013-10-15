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

void
RRD_update (char *rrd, unsigned int value, long process_time)
{
  char *argv[3];
  int argc = 3;
  char val[20];

  sprintf (val, "%ld:%u", process_time, value);

  argv[0] = "dummy";
  argv[1] = rrd;
  argv[2] = val;

  optind=0; opterr=0;
  rrd_clear_error ();
  rrd_update (argc, argv);
  if(rrd_test_error ())
  {
    log ("RRD_update (%s): %s\n", rrd, rrd_get_error ());
    return;
  }
  log ("Updated rrd %s with value %s\n", rrd, val);
  return;
}

void
RRD_create (char *rrd, unsigned int step)
{
  char *argv[12];
  int argc=0;
  char s[16], start[64];
  char energy[64];

  struct timeval tv;
  gettimeofday (&tv, NULL);

  argv[argc++] = "dummy";
  argv[argc++] = rrd;
  argv[argc++] = "--step";
  sprintf (s, "%u", step);
  argv[argc++] = s;
  argv[argc++] = "--start";
  // Start a month ago, because that's the history that may be stored in the device.
  sprintf (start, "%lu", (unsigned long) tv.tv_sec-2629743);
  argv[argc++] = start;
  sprintf (energy,"DS:energy:GAUGE:60:0:U");
  argv[argc++] = energy;

  // http://eccentric.cx/misc/rrdcalc.html
  // Hourly, keep 1 data every step (60s), keep 60 of them, 60*60 = 1h
  argv[argc++] = "RRA:AVERAGE:0.5:1:60";
  // Daily, keep 1 cons. data every step (60s), keep 1440 of them, 60*1440 = 24h
  argv[argc++] = "RRA:AVERAGE:0.5:1:1440";
  // Weekly, every 5 data, keep one. Keep 2016 of them, 2016*5*60 = 1 week
  // It can also be viewed as keep 2016 rows of data, with a resolution of 5 minutes. (2016*5*step = 1W)
  argv[argc++] = "RRA:AVERAGE:0.5:5:2016";
  // Monthly, every 10 data (minutes), keep one. Keep 4380 of them, 4380*10*60 = 1 month
  argv[argc++] = "RRA:AVERAGE:0.5:10:4380";

  optind=0; opterr=0;
  rrd_clear_error ();
  rrd_create (argc, argv);
  if (rrd_test_error ())
  {
    log ("RRD_create: %s\n", rrd_get_error ());
    return;
  }
  log ("Created rrd %s\n", rrd);
  return;
}

void
RRD_graph (char *rrd, char *period, char *graph_path)
{
  char *argv[80];
  int argc=0;
  char start[20];
  char path[PATH_MAX];
  char title[32];
  char energy[20+PATH_MAX];

  argv[argc++] = "graph";
  sprintf (path, "%s/energy-%s.png", graph_path, period);
  argv[argc++] = path;
  argv[argc++] = "--start";
  sprintf (start, "-1%s", period);
  argv[argc++] = start;
  argv[argc++] = "-t";
  sprintf (title, "Energy usage of the last %s", period);
  argv[argc++] = title;
  argv[argc++] = "-z";
  argv[argc++] = "-c";
  argv[argc++] = "BACK#FFFFFF";
  argv[argc++] = "-c";
  argv[argc++] = "SHADEA#FFFFFF";
  argv[argc++] = "-c";
  argv[argc++] = "SHADEB#FFFFFF";
  argv[argc++] = "-c";
  argv[argc++] = "MGRID#AAAAAA";
  argv[argc++] = "-c";
  argv[argc++] = "GRID#CCCCCC";
  argv[argc++] = "-c";
  argv[argc++] = "ARROW#333333";
  argv[argc++] = "-c";
  argv[argc++] = "FONT#333333";
  argv[argc++] = "-c";
  argv[argc++] = "AXIS#333333";
  argv[argc++] = "-c";
  argv[argc++] = "FRAME#333333";
  argv[argc++] = "-h";
  argv[argc++] = "480";
  argv[argc++] = "-w";
  argv[argc++] = "1024";
  argv[argc++] = "-l";
  argv[argc++] = "0";
  argv[argc++] = "-a";
  argv[argc++] = "PNG";
  argv[argc++] = "-v";
  argv[argc++] = "W";
  sprintf (energy, "DEF:energy=%s:energy:AVERAGE", rrd);
  argv[argc++] = energy;
  argv[argc++] = "VDEF:min=energy,MINIMUM";
  argv[argc++] = "VDEF:max=energy,MAXIMUM";
  argv[argc++] = "VDEF:avg=energy,AVERAGE";
  argv[argc++] = "VDEF:lst=energy,LAST";
  argv[argc++] = "COMMENT: \\l";
  argv[argc++] = "COMMENT:               ";
  argv[argc++] = "COMMENT:Minimum    ";
  argv[argc++] = "COMMENT:Maximum    ";
  argv[argc++] = "COMMENT:Average    ";
  argv[argc++] = "COMMENT:Current    \\l";
  argv[argc++] = "COMMENT:   ";
  argv[argc++] = "AREA:energy#EDA362:Usage  ";
  argv[argc++] = "LINE1:energy#F47200";
  argv[argc++] = "GPRINT:min:%5.1lf %sW   ";
  argv[argc++] = "GPRINT:max:%5.1lf %sW   ";
  argv[argc++] = "GPRINT:avg:%5.1lf %sW   ";
  argv[argc++] = "GPRINT:lst:%5.1lf %sW   \\l";

  optind=0; opterr=0;
  rrd_clear_error ();
  rrd_graph_v (argc, argv);
  if (rrd_test_error ())
  {
    log ("RRD_graph: %s\n", rrd_get_error ());
    return;
  }
  log ("Created graph rrd %s\n", path);
  return;
}

