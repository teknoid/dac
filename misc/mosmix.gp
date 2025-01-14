# gnuplot -p misc/mosmix.gp

pstate="/tmp/fronius-pstate-minutes.csv"
gstate="/tmp/fronius-gstate-today.csv"
gstatew="/tmp/fronius-gstate-week.csv"
today="/tmp/fronius-mosmix-today.csv"
tomorrow="/tmp/fronius-mosmix-tomorrow.csv"
history="/tmp/fronius-mosmix-history.csv"

#set terminal wxt size 1200,400
#set terminal pngcairo size 1000,400
set terminal svg size 1400,600
set datafile separator whitespace
set key autotitle columnhead

# thick lines
set style line 7 linecolor "violet" lw 2
set style line 8 linecolor "goldenrod" lw 2
set style line 9 linecolor "orange-red" lw 6

# thin lines 
set style line 1 linecolor "goldenrod" lw 1
set style line 2 linecolor "spring-green" lw 1
set style line 3 linecolor "red" lw 1
set style line 4 linecolor "navy" lw 1
set style line 5 linecolor "olive" lw 1
set style line 6 linecolor "black" lw 1

set boxwidth 0.33 relative
set style fill solid 0.5

set grid xtics ytics mytics
set mytics 2
set grid
set xtics nomirror
set ytics nomirror
set tics format "%2.0s%c"


# history
set xtics ("Sun" 12, "Mon" 36, "Tue" 60, "Wed" 84, "Thu" 108, "Fri" 132, "Sat" 156) nomirror

set ylabel "MPPTs"
set output "/tmp/mosmix-mppt.svg" 
p history u 1:"mppt1" t "mppt1" w lines, '' u 1:"mppt2" t "mppt2" w lines, '' u 1:"mppt3" t "mppt3" w lines, '' u 1:"exp1" t "exp" w lines, '' u 1:"base" t "base" w lines 

set xzeroaxis linetype 16 linewidth 0.5

set ylabel "Errors"
set output "/tmp/mosmix-errors.svg"
p history u 1:"err1" t "err1" w lines, '' u 1:"err2" t "err2" w lines, '' u 1:"err3" t "err3" w lines

set ylabel "Factors"
set output "/tmp/mosmix-factors.svg" 
p history u 1:"fac1" t "fac1" w lines, '' u 1:"fac2" t "fac2" w lines, '' u 1:"fac3" t "fac3" w lines


# gstate weekly
set ylabel "GState week"
set output "/tmp/mosmix-gstate-week.svg"
p gstatew u 1:"pv" t "pv" w lines,\
       '' u 1:"↑grid" t "produced" w lines,\
       '' u 1:"↓grid" t "consumed" w lines,\
       '' u 1:"akku" t "akku" w lines,\
       '' u 1:"mppt1" t "mppt1" w lines,\
       '' u 1:"mppt2" t "mppt2" w lines,\
       '' u 1:"mppt3" t "mppt3" w lines,\
       '' u 1:"ttl" t "ttl" w lines,\
       '' u 1:"surv" t "survive" w lines,\
       '' u 1:"heat" t "heating" w lines


# gstate
set ylabel "GState"
set xrange [0:24]
set xtics 1
set output "/tmp/mosmix-gstate.svg"
p gstate u 1:"pv" t "pv" w lines,\
      '' u 1:"↑grid" t "produced" w lines,\
      '' u 1:"↓grid" t "consumed" w lines,\
      '' u 1:"akku" t "akku" w lines,\
      '' u 1:"mppt1" t "mppt1" w lines,\
      '' u 1:"mppt2" t "mppt2" w lines,\
      '' u 1:"mppt3" t "mppt3" w lines,\
      '' u 1:"ttl" t "ttl" w lines,\
      '' u 1:"surv" t "survive" w lines,\
      '' u 1:"heat" t "heating" w lines


# forecasts
set xrange [4:20]
set yrange [0:5000]

set ylabel "Today"
set output "/tmp/mosmix-today.svg" 
p today u 1:"SunD1" t "SunD1" w boxes fillcolor "#ff8c00",\
     '' u 1:"Rad1h" t "Rad1h" w impulses ls 9,\
     '' u 1:"exp1" t "exp1" w lines ls 3,\
     '' u 1:"exp2" t "exp2" w lines ls 2,\
     '' u 1:"exp3" t "exp3" w lines ls 4, \
	 '' u 1:($5+$9+$13) w lines ls 8 t "sum"

set ylabel "Tomorrow"
set output "/tmp/mosmix-tomorrow.svg" 
p tomorrow u 1:"SunD1" t "SunD1" w boxes fillcolor "#ff8c00",\
        '' u 1:"Rad1h" t "Rad1h" w impulses ls 9,\
        '' u 1:"exp1" t "exp1" w lines ls 3,\
        '' u 1:"exp2" t "exp2" w lines ls 2,\
        '' u 1:"exp3" t "exp3" w lines ls 4, \
	    '' u 1:($5+$9+$13) t "sum" w lines ls 8


# pstate
set ylabel "PState"
set xrange [0:1440]
set yrange [*:*] 
set y2range [0:1000]
set xtics time 60 format "%tM"
set xzeroaxis linetype 16 linewidth 0.5
set output "/tmp/mosmix-pstate.svg"
p pstate u 1:(0):"pv" w filledc below ls 1,\
      '' u 1:(0):"pv" w filledc above ls 1 t "pv",\
      '' u 1:(0):"akku" w filledc below ls 2,\
      '' u 1:(0):"akku" w filledc above ls 2 t "akku",\
      '' u 1:(0):"grid" w filledc below ls 3,\
      '' u 1:(0):"grid" w filledc above ls 3 t "grid",\
      '' u 1:"load" t "load" w lines ls 4,\
      '' u 1:"soc" t "soc" w lines ls 5 axes x1y2
