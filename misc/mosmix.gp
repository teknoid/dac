# gnuplot -p misc/mosmix.gp

set terminal wxt size 1600,400
set datafile separator whitespace
set xrange [0:168]
set key autotitle columnhead
set ylabel "PV" 
set xlabel "hour"

plot "/tmp/fronius-mosmix.csv" using 0:4 with lines, '' using 0:6 with lines, '' using 0:7 with lines
