#!/bin/sh

rrdtool dump "$1" | sed -e "s/<lastupdate>1[0-9]\{9\}/<lastupdate>$2/" | rrdtool restore -f - "$1"
