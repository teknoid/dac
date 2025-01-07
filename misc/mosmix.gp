# gnuplot -p misc/mosmix.gp

file="/tmp/fronius-mosmix.csv"

#set terminal wxt size 1600,400
set terminal pngcairo size 1600,400
set datafile separator whitespace
set ytics nomirror
set xtics ("Sun" 12, "Mon" 36, "Tue" 60, "Wed" 84, "Thu" 108, "Fri" 132, "Sat" 156) nomirror
set key autotitle columnhead

# per mppt
set ylabel "PV"
set output "/tmp/mosmix-mppt1.png" 
plot file using 1:4 with lines, '' using 1:5 with lines, '' using 1:6 with lines, '' using 1:7 with lines, '' using 1:8 with lines
set output "/tmp/mosmix-mppt2.png" 
plot file using 1:4 with lines, '' using 1:9 with lines, '' using 1:10 with lines, '' using 1:11 with lines, '' using 1:12 with lines
set output "/tmp/mosmix-mppt3.png" 
plot file using 1:4 with lines, '' using 1:13 with lines, '' using 1:14 with lines, '' using 1:15 with lines, '' using 1:16 with lines

# only errors
set ylabel "Errors"
set output "/tmp/mosmix-errors.png"
plot file using 1:7 with lines, '' using 1:11 with lines, '' using 1:15 with lines

# only factors
set ylabel "Factors"
set output "/tmp/mosmix-factors.png" 
plot file using 1:8 with lines, '' using 1:12 with lines, '' using 1:16 with lines
