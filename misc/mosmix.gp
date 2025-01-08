# gnuplot -p misc/mosmix.gp

today="/tmp/fronius-mosmix-today.csv"
tomorrow="/tmp/fronius-mosmix-tomorrow.csv"
history="/tmp/fronius-mosmix-history.csv"

#set terminal wxt size 1200,400
set terminal pngcairo size 1200,400
set datafile separator whitespace
set ytics nomirror
set key autotitle columnhead

set boxwidth 0.5 relative
set style fill solid 0.5
set xtics 1 nomirror

# today
set ylabel "expected PV today"
set output "/tmp/mosmix-today.png" 
plot today using 1:2 with lines, '' using 1:3 with boxes fillcolor "#ff8c00", '' using 1:5 with lines, '' using 1:9 with lines, '' using 1:13 with lines

# tomorrow
set ylabel "expected PV tomorrow"
set output "/tmp/mosmix-tomorrow.png" 
plot tomorrow using 1:2 with lines, '' using 1:3 with boxes fillcolor "#ff8c00", '' using 1:5 with lines, '' using 1:9 with lines, '' using 1:13 with lines

set xtics ("Sun" 12, "Mon" 36, "Tue" 60, "Wed" 84, "Thu" 108, "Fri" 132, "Sat" 156) nomirror

# history per mppt
set ylabel "PV"
set output "/tmp/mosmix-mppt1.png" 
plot history using 1:4 with lines, '' using 1:5 with lines, '' using 1:6 with lines, '' using 1:7 with lines, '' using 1:8 with lines
set output "/tmp/mosmix-mppt2.png" 
plot history using 1:4 with lines, '' using 1:9 with lines, '' using 1:10 with lines, '' using 1:11 with lines, '' using 1:12 with lines
set output "/tmp/mosmix-mppt3.png" 
plot history using 1:4 with lines, '' using 1:13 with lines, '' using 1:14 with lines, '' using 1:15 with lines, '' using 1:16 with lines

# history only errors
set ylabel "Errors"
set output "/tmp/mosmix-errors.png"
plot history using 1:7 with lines, '' using 1:11 with lines, '' using 1:15 with lines

# history only factors
set ylabel "Factors"
set output "/tmp/mosmix-factors.png" 
plot history using 1:8 with lines, '' using 1:12 with lines, '' using 1:16 with lines

