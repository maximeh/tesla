#!/bin/sh

rrdtool dump "$1" | sed -e "s/<lastupdate>.*/<lastupdate>$2<\/lastupdate>/" | rrdtool restore -f - "$1"
