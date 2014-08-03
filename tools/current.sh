#!/bin/sh

DB_PATH="$1"

[ -z "$DB_PATH" ] && DB_PATH=/var/lib/tesla.rrd

rrdtool info ${DB_PATH} | grep last_ds | cut -d'"' -f2
