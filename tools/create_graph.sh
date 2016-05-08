#!/bin/sh

exec 1>/dev/null
exec 2>&1

PERIOD="$1"
DB_PATH="$2"
GRAPH_PATH="$3"

echo "Usage: <PERIOD> <DB_PATH> <GRAPH_PATH>"
echo "Period available: hour, day, week, month, year"

[ -z "$PERIOD" ] && PERIOD="hour"
[ -z "$DB_PATH" ] && DB_PATH=/var/lib/tesla.rrd
[ -z "$GRAPH_PATH" ] && GRAPH_PATH=/var/www/f00.fr/tesla/graph_${PERIOD}.png

rrdtool graph ${GRAPH_PATH} --start -1${PERIOD} \
	-t "Energy usage of the last ${PERIOD}" \
	-z \
	--slope-mode \
	--font DEFAULT:7: \
	--watermark "$(date)" \
	--vertical-label "energy(watt)" \
	--lower-limit 0 \
	--right-axis 1:0 \
	--alt-y-grid --rigid \
	-h 480 -w 1024 -l 0 -a PNG \
	DEF:energy=${DB_PATH}:energy:AVERAGE:step=60 \
	VDEF:min=energy,MINIMUM \
	VDEF:max=energy,MAXIMUM \
	VDEF:avg=energy,AVERAGE \
	VDEF:lst=energy,LAST \
	LINE1:energy#000000:"energy (watt)" \
	COMMENT:"\n" \
	COMMENT:"\t\t\t\t\t\t\tMinimum" \
	COMMENT:"\tMaximum" \
	COMMENT:"\tAverage" \
	COMMENT:"\tCurrent" \
	COMMENT:"\n" \
	GPRINT:min:"\t\t\t\t\t\t\t%5.1lf%sW" \
	GPRINT:max:"\t%5.1lf%sW" \
	GPRINT:avg:"\t%5.1lf%sW" \
	GPRINT:lst:"\t%5.1lf%sW\n"
