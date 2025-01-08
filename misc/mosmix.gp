# gnuplot -p misc/mosmix.gp

today="/tmp/fronius-mosmix-today.csv"
tomorrow="/tmp/fronius-mosmix-tomorrow.csv"
history="/tmp/fronius-mosmix-history.csv"

#set terminal wxt size 1200,400
set terminal pngcairo size 1200,400
set datafile separator whitespace
set ytics nomirror
set key autotitle columnhead

set boxwidth 0.33 relative
set style fill solid 0.5
set xtics 1 nomirror
set xrange [4:20]

# today
set ylabel "Today"
set output "/tmp/mosmix-today.png" 
plot today u 1:3 w boxes fillcolor "#ff8c00", '' u 1:2 w impulses lt 1 lw 2, '' u 1:5 w lines, '' u 1:9 w lines, '' u 1:13 w lines, \
	'' u 1:($5+$9+$13) w lines lw 2 t "sum"

# tomorrow
set ylabel "Tomorrow"
set output "/tmp/mosmix-tomorrow.png" 
plot tomorrow u 1:3 w boxes fillcolor "#ff8c00", '' u 1:2 w impulses lt 1 lw 2, '' u 1:5 w lines, '' u 1:9 w lines, '' u 1:13 w lines, \
	'' u 1:($5+$9+$13) w lines lw 2 t "sum"

set xtics ("Sun" 12, "Mon" 36, "Tue" 60, "Wed" 84, "Thu" 108, "Fri" 132, "Sat" 156) nomirror
set xrange [0:167]

# history per mppt
set ylabel "MPPT1"
set output "/tmp/mosmix-mppt1.png" 
plot history u 1:4 w lines, '' u 1:5 w lines, '' u 1:6 w lines, '' u 1:7 w lines, '' u 1:8 w lines
set ylabel "MPPT2"
set output "/tmp/mosmix-mppt2.png" 
plot history u 1:4 w lines, '' u 1:9 w lines, '' u 1:10 w lines, '' u 1:11 w lines, '' u 1:12 w lines
set ylabel "MPPT3"
set output "/tmp/mosmix-mppt3.png" 
plot history u 1:4 w lines, '' u 1:13 w lines, '' u 1:14 w lines, '' u 1:15 w lines, '' u 1:16 w lines

# history errors
set ylabel "Errors"
set output "/tmp/mosmix-errors.png"
plot history u 1:7 w lines, '' u 1:11 w lines, '' u 1:15 w lines

# history factors
set ylabel "Factors"
set output "/tmp/mosmix-factors.png" 
plot history u 1:8 w lines, '' u 1:12 w lines, '' u 1:16 w lines
