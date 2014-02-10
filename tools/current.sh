#!/bin/sh

DB_PATH="$1"
DATA_PATH="$2"
PERIOD="$3"

echo "Usage: <DB_PATH> <DATA_PATH> <PERDIOD>"
echo "Period available: hour, day, week, month, year"

[ -z "$DB_PATH" ] && DB_PATH=/var/lib/tesla.rrd
[ -z "$DATA_PATH" ] && DATA_PATH=$(pwd)/data.xml

rrdtool xport --start now-1hour -e now \
DEF:energy=${DB_PATH}:energy:LAST \
XPORT:energy:"Energy" > ${DATA_PATH}

