#!/bin/sh

rrdtool dump "$1" | perl -ne 'BEGIN {$t=`date +%s`; chomp($t);} $a=$_; if ($a =~ /lastupdate.\d+..lastupdate/) { $a =~ s/(lastupdate.)\d+(..lastupdate)/$1$t$2/; } print $a' | rrdtool restore -f - "$1"
