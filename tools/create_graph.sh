#!/bin/sh

PERIOD="$1"
DB_PATH="$2"
GRAPH_PATH="$3"

echo "Usage: <PERIOD> <DB_PATH> <GRAPH_PATH>"
echo "Period available: hour, day, week, month, year"

[ -z "$PERIOD" ] && PERIOD="hour"
[ -z "$DB_PATH" ] && DB_PATH=/var/lib/tesla.rrd
[ -z "$GRAPH_PATH" ] && GRAPH_PATH=$(pwd)/graph_${PERIOD}.png

rrdtool graph ${GRAPH_PATH} --start -1${PERIOD} -t "Energy usage of the last ${PERIOD}" -z \
-c BACK#FFFFFF \
-c SHADEA#000000 \
-c SHADEB#FFFFFF \
-c MGRID#AAAAAA \
-c GRID#CCCCCC \
-c ARROW#333333 \
-c FONT#333333 \
-c AXIS#333333 \
-c FRAME#333333 \
-h 480 -w 1024 -l 0 -a PNG -v W \
DEF:energy=${DB_PATH}:energy:AVERAGE:step=60 \
VDEF:min=energy,MINIMUM \
VDEF:max=energy,MAXIMUM \
VDEF:avg=energy,AVERAGE \
VDEF:lst=energy,LAST \
CDEF:smooth_energy=energy,300,TREND \
COMMENT:"\n" \
COMMENT:"\tMinimum" \
COMMENT:"\tMaximum" \
COMMENT:"\tAverage" \
COMMENT:"\tCurrent" \
COMMENT:"\n" \
GPRINT:min:"\t%5.1lf%sW" \
GPRINT:max:"\t%5.1lf%sW" \
GPRINT:avg:"\t%5.1lf%sW" \
GPRINT:lst:"\t%5.1lf%sW\n" \
AREA:energy#EDA362:Usage  \
LINE1:energy#F47200

