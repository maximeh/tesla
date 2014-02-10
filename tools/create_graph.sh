#!/bin/sh

DB_PATH="$1"
GRAPH_PATH="$2"
PERIOD="$3"

echo "Usage: <DB_PATH> <GRAPH_PATH> <PERDIOD>"
echo "Period available: hour, day, week, month, year"

[ -z "$DB_PATH" ] && DB_PATH=/var/lib/tesla.rrd
[ -z "$GRAPH_PATH" ] && GRAPH_PATH=$(pwd)/graph.png
[ -z "$PERIOD" ] && PERIOD="hour"

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
LINE4:max#66281A::dashes \
LINE2:max#B2462E:"Max":dashes \
LINE4:min#2A616B::dashes \
LINE2:min#48A5B8:"Min":dashes \
LINE6:smooth_energy#1A1A40 \
LINE4:smooth_energy#39398C \
LINE2:smooth_energy#5353CC

# Area graph
#AREA:energy#EDA362:Usage  \
#LINE1:energy#F47200

