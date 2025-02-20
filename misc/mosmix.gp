# gnuplot -p misc/mosmix.gp

pstate="/run/mcp/pstate-minutes.csv"
gstate="/run/mcp/gstate-today.csv"
gstatew="/run/mcp/gstate-week.csv"
history="/run/mcp/mosmix-history.csv"
factors="/run/mcp/mosmix-factors.csv"
today="/run/mcp/mosmix-today.csv"
tomorrow="/run/mcp/mosmix-tomorrow.csv"

#set terminal wxt size 1200,400
#set terminal pngcairo size 1000,400
set terminal svg size 1920,800
set datafile separator whitespace
set key autotitle columnhead

set style line 1 linecolor rgb "#FAD967" lw 1			# PV
set style line 2 linecolor rgb "#93CF82" lw 1			# Akku
set style line 3 linecolor "orange-red" lw 1			# Grid download
set style line 4 linecolor "orchid" lw 1				# Grid upload
set style line 5 linecolor "navy" lw 1					# Load
set style line 6 linecolor "olive" lw 1					# SoC
set style line 8 linecolor "orange-red" lw 6			# Rad1h over SunD1
set style line 9 linecolor "black" lw 1

set style fill solid 1.0
set boxwidth 0.33 relative

set grid xtics ytics mytics
set mytics 2
set grid
set xtics nomirror
set ytics nomirror
set tics format "%2.0s%c"


# history
set ylabel "MPPTs"
set xtics ("Sun" 12, "Mon" 36, "Tue" 60, "Wed" 84, "Thu" 108, "Fri" 132, "Sat" 156) nomirror
set output "/run/mcp/mosmix-mppt.svg" 
p history u 1:"mppt1" t "mppt1" w lines,\
       '' u 1:"mppt2" t "mppt2" w lines,\
       '' u 1:"mppt3" t "mppt3" w lines,\
       '' u 1:"exp1"  t "exp1"  w lines linecolor rgb "red",\
       '' u 1:"exp2"  t "exp2"  w lines linecolor rgb "green",\
       '' u 1:"exp3"  t "exp3"  w lines linecolor rgb "blue"

# errors
set ylabel "Errors Wh"
set xzeroaxis linetype 16 linewidth 0.5
set output "/run/mcp/mosmix-errors-diff.svg"
p history u 1:"diff1" t "diff1" w lines, '' u 1:"diff2" t "diff2" w lines, '' u 1:"diff3" t "diff3" w lines

set ylabel "Errors %"
set xzeroaxis linetype 16 linewidth 0.5
set output "/run/mcp/mosmix-errors-percent.svg"
p history u 1:"err1" t "err1" w lines, '' u 1:"err2" t "err2" w lines, '' u 1:"err3" t "err3" w lines


# gstate weekly
set ylabel "GState week"
set yrange [0:*]
set output "/run/mcp/gstate-week.svg"
p gstatew u 1:"mppt1" t "mppt1"    w lines,\
       '' u 1:"mppt2" t "mppt2"    w lines,\
       '' u 1:"mppt3" t "mppt3"    w lines,\
       '' u 1:"pv"    t "pv"       w lines,\
       '' u 1:"akku"  t "akku"     w lines,\
       '' u 1:"↑grid" t "produced" w lines,\
       '' u 1:"↓grid" t "consumed" w lines,\
       '' u 1:"ttl"   t "ttl"      w lines,\
       '' u 1:"surv"  t "survive"  w lines ls 5,\
       '' u 1:"heat"  t "heating"  w lines ls 6


# gstate
set ylabel "GState"
set xrange [0:24]
set yrange [0:*]
set xtics 1
set output "/run/mcp/gstate.svg"
p gstate u 1:"mppt1" t "mppt1"    w lines,\
      '' u 1:"mppt2" t "mppt2"    w lines,\
      '' u 1:"mppt3" t "mppt3"    w lines,\
      '' u 1:"pv"    t "pv"       w lines,\
      '' u 1:"akku"  t "akku"     w lines,\
      '' u 1:"↑grid" t "produced" w lines,\
      '' u 1:"↓grid" t "consumed" w lines,\
      '' u 1:"ttl"   t "ttl"      w lines,\
      '' u 1:"surv"  t "survive"  w lines ls 5,\
      '' u 1:"heat"  t "heating"  w lines ls 6


# factors
set ylabel "Factors"
set xrange [0:24]
set output "/run/mcp/mosmix-factors.svg" 
p factors u 1:"r1" t "r1" w lines,\
       '' u 1:"r2" t "r2" w lines,\
       '' u 1:"r3" t "r3" w lines,\
       '' u 1:"s1" t "s1" w lines linecolor rgb "red",\
       '' u 1:"s2" t "s2" w lines linecolor rgb "green",\
       '' u 1:"s3" t "s3" w lines linecolor rgb "blue"


# forecasts
set xrange [4:20]
set yrange [0:5000]

set ylabel "Today"
set output "/run/mcp/mosmix-today.svg" 
p today u 1:"SunD1" t "SunD1" w boxes fillcolor "#ff8c00",\
     '' u 1:"Rad1h" t "Rad1h" w impulses ls 8,\
     '' u 1:"exp1"  t "exp1"  w lines lt 1,\
     '' u 1:"exp2"  t "exp2"  w lines lt 2,\
     '' u 1:"exp3"  t "exp3"  w lines lt 3, \
	 '' u 1:($7+$8+$9) w lines ls 1 lw 2 t "sum"

set ylabel "Tomorrow"
set output "/run/mcp/mosmix-tomorrow.svg" 
p tomorrow u 1:"SunD1" t "SunD1" w boxes fillcolor "#ff8c00",\
        '' u 1:"Rad1h" t "Rad1h" w impulses ls 8,\
        '' u 1:"exp1"  t "exp1"  w lines lt 1,\
        '' u 1:"exp2"  t "exp2"  w lines lt 2,\
        '' u 1:"exp3"  t "exp3"  w lines lt 3, \
	    '' u 1:($7+$8+$9) w lines ls 1 lw 2 t "sum"


# pstate
set ylabel "PState"
set xrange [0:1440]
set yrange [*:*] 
set y2range [0:1000]
set xtics time 60 format "%tM"
set xzeroaxis linetype 16 linewidth 0.5
set output "/run/mcp/pstate.svg"
p pstate u 1:(0):"pv"   w filledc below ls 1 t "pv",\
      '' u 1:(0):"pv"   w filledc above ls 1,\
      '' u 1:(0):"akku" w filledc below ls 2 t "akku",\
      '' u 1:(0):"akku" w filledc above ls 2,\
      '' u 1:(0):"grid" w filledc below ls 3 t "grid",\
      '' u 1:(0):"grid" w filledc above ls 4,\
      '' u 1:"load" t "load" w lines ls 5,\
      '' u 1:"soc"  t "soc"  w lines ls 6 axes x1y2
      
set ylabel "Grid - Power"
unset y2range
set terminal svg size 1920,400
set output "/run/mcp/pstate-power.svg"
p pstate u 1:(0):"grid" w filledc below ls 3 t "↓sum",\
      '' u 1:(0):"grid" w filledc above ls 2 t "↑sum",\
      '' u 1:"l1"       w lines lt 1 t "L1",\
      '' u 1:"l2"       w lines lt 2 t "L2",\
      '' u 1:"l3"       w lines lt 3 t "L3"
      
      
set ylabel "Grid - Voltage"
set yrange [220:250] 
set output "/run/mcp/pstate-voltage.svg"
p pstate u 1:"l1v" w lines t "L1", '' u 1:"l2v" w lines t "L2", '' u 1:"l3v" w lines t "L3"

set ylabel "Grid - Frequency +/-"
set yrange [-50:50]
set output "/run/mcp/pstate-frequency.svg"
p pstate u 1:"f"   w lines t "f"

set ylabel "Minimum / Maximum"
set yrange [*:*]
set output "/run/mcp/pstate-min-max.svg"
p pstate u 1:"pv" w lines t "pv" ls 1, '' u 1:"pvmin" w lines t "min" ls 3, '' u 1:"pvmax" w lines t "max" ls 4
