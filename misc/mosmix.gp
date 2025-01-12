# gnuplot -p misc/mosmix.gp

today="/tmp/fronius-mosmix-today.csv"
tomorrow="/tmp/fronius-mosmix-tomorrow.csv"
history="/tmp/fronius-mosmix-history.csv"

#set terminal wxt size 1200,400
set terminal pngcairo size 1200,400
set datafile separator whitespace
set ytics nomirror
set key autotitle columnhead

set style line 1 linecolor "navy" lw 2
set style line 2 linecolor "turquoise" lw 1
set style line 3 linecolor "steelblue" lw 1
set style line 4 linecolor "violet" lw 1
set style line 5 linecolor "gold" lw 2

set boxwidth 0.33 relative
set style fill solid 0.5
set xtics 1 nomirror
set xrange [4:20]
set yrange [0:5000]

set grid xtics ytics mytics
set mytics 2
set grid

# today
set ylabel "Today"
set output "/tmp/mosmix-today.png" 
p today u 1:3 w boxes fillcolor "#ff8c00", '' u 1:2 w impulses ls 1, '' u 1:5 w lines ls 2, '' u 1:9 w lines ls 3, '' u 1:13 w lines ls 4, \
	'' u 1:($5+$9+$13) w lines ls 5 t "sum"

# tomorrow
set ylabel "Tomorrow"
set output "/tmp/mosmix-tomorrow.png" 
p tomorrow u 1:3 w boxes fillcolor "#ff8c00", '' u 1:2 w impulses ls 1, '' u 1:5 w lines ls 2, '' u 1:9 w lines ls 3, '' u 1:13 w lines ls 4, \
	'' u 1:($5+$9+$13) w lines ls 5 t "sum"

set autoscale x
set autoscale y
set xtics ("Sun" 12, "Mon" 36, "Tue" 60, "Wed" 84, "Thu" 108, "Fri" 132, "Sat" 156) nomirror

# history per mppt
set ylabel "MPPT1"
set output "/tmp/mosmix-mppt1.png" 
p history u 1:4 w lines, '' u 1:5 w lines, '' u 1:6 w lines, '' u 1:7 w lines, '' u 1:8 w lines
set ylabel "MPPT2"
set output "/tmp/mosmix-mppt2.png" 
p history u 1:4 w lines, '' u 1:9 w lines, '' u 1:10 w lines, '' u 1:11 w lines, '' u 1:12 w lines
set ylabel "MPPT3"
set output "/tmp/mosmix-mppt3.png" 
p history u 1:4 w lines, '' u 1:13 w lines, '' u 1:14 w lines, '' u 1:15 w lines, '' u 1:16 w lines

# history errors
set ylabel "Errors"
set output "/tmp/mosmix-errors.png"
p history u 1:7 w lines, '' u 1:11 w lines, '' u 1:15 w lines

# history factors
set ylabel "Factors"
set output "/tmp/mosmix-factors.png" 
p history u 1:8 w lines, '' u 1:12 w lines, '' u 1:16 w lines
