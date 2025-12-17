# /usr/bin/gnuplot -p /home/hje/workspace-cpp/dac/misc/solar-hourly.gp

history="/run/mcp/mosmix-history.csv"
factors="/run/mcp/mosmix-factors.csv"
today="/run/mcp/mosmix-today.csv"
tomorrow="/run/mcp/mosmix-tomorrow.csv"
pstate="/run/mcp/pstate-minutes.csv"
gstate="/run/mcp/gstate-minutes.csv"
gstateh="/run/mcp/gstate-hours.csv"
avg247="/run/mcp/pstate-avg-247.csv"

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
set style line 7 linecolor "magenta" lw 1				# 
set style line 8 linecolor "black" lw 1					# 
set style line 9 linecolor "orange-red" lw 12			# Rad1h over SunD1

set style line 10 linecolor rgb "#ae5a41" lw 1			# MPPT1
set style line 11 linecolor rgb "#c3cb71" lw 1			# MPPT2
set style line 12 linecolor rgb "#1b85b8" lw 1			# MPPT3

set style line 20 linecolor rgb "#ae5a41" lw 1 dt 2		# MPPT1 dashed
set style line 21 linecolor rgb "#c3cb71" lw 1 dt 2		# MPPT2 dashed
set style line 22 linecolor rgb "#1b85b8" lw 1 dt 2		# MPPT3 dashed

set style fill solid 1.0
set boxwidth 0.33 relative

set grid
set xtics nomirror
set ytics nomirror
set tics format "%2.0s%c"


# mosmix history
set ylabel "MPPTs"
set xtics ("Sun" 12, "Mon" 36, "Tue" 60, "Wed" 84, "Thu" 108, "Fri" 132, "Sat" 156) nomirror
set output "/run/mcp/mosmix-mppt.svg" 
p history u 1:"mppt1" t "mppt1" w lines ls 10,\
       '' u 1:"mppt2" t "mppt2" w lines ls 11,\
       '' u 1:"mppt3" t "mppt3" w lines ls 12,\
       '' u 1:"exp1"  t "exp1"  w lines ls 20,\
       '' u 1:"exp2"  t "exp2"  w lines ls 21,\
       '' u 1:"exp3"  t "exp3"  w lines ls 22

set ylabel "Errors Wh"
set xzeroaxis linetype 16 linewidth 0.5
set output "/run/mcp/mosmix-errors-diff.svg"
p history u 1:"diff1" t "diff1" w lines ls 10,\
       '' u 1:"diff2" t "diff2" w lines ls 11,\
       '' u 1:"diff3" t "diff3" w lines ls 12

set ylabel "Errors %"
set xzeroaxis linetype 16 linewidth 0.5
set output "/run/mcp/mosmix-errors-percent.svg"
p history u 1:"err1" t "err1" w lines ls 10,\
       '' u 1:"err2" t "err2" w lines ls 11,\
       '' u 1:"err3" t "err3" w lines ls 12


# gstate weekly
set ylabel "GState week"
set xrange [0:168]
set output "/run/mcp/gstate-week.svg"
p gstateh u 1:"soc"   t "soc"      w lines ls 6,\
       '' u 1:"ttl"   t "ttl"      w lines lt 4,\
       '' u 1:"surv"  t "survive"  w lines lt 1


# mosmix factors
set ylabel "Factors"
set xrange [5:22]
set xtics 1
set output "/run/mcp/mosmix-factors.svg" 
p factors u 1:"r1" t "r1" w lines ls 10,\
       '' u 1:"r2" t "r2" w lines ls 11,\
       '' u 1:"r3" t "r3" w lines ls 12,\
       '' u 1:"e1" t "e1" w lines ls 20,\
       '' u 1:"e2" t "e2" w lines ls 21,\
       '' u 1:"e3" t "e3" w lines ls 22


# mosmix forecast today
set ylabel "Today"
set yrange [0:10000]
set y2range [0:100]
set output "/run/mcp/mosmix-today.svg" 
p today    u 1:"SunD1" t "SunD1" w boxes fillcolor "orange" axes x1y2,\
        '' u 1:"Rad1h" t "Rad1h" w impulses ls 9,\
        '' u 1:"exp1"  t "exp1"  w lines lt 1,\
        '' u 1:"exp2"  t "exp2"  w lines lt 2,\
        '' u 1:"exp3"  t "exp3"  w lines lt 3, \
	    '' u 1:($9+$10+$11) w lines ls 1 lw 2 t "sum"


# mosmix forecast tomorrow
set ylabel "Tomorrow"
set output "/run/mcp/mosmix-tomorrow.svg" 
p tomorrow u 1:"SunD1" t "SunD1" w boxes fillcolor "orange" axes x1y2,\
        '' u 1:"Rad1h" t "Rad1h" w impulses ls 9,\
        '' u 1:"exp1"  t "exp1"  w lines lt 1,\
        '' u 1:"exp2"  t "exp2"  w lines lt 2,\
        '' u 1:"exp3"  t "exp3"  w lines lt 3, \
	    '' u 1:($9+$10+$11) w lines ls 1 lw 2 t "sum"


# pstate
set ylabel "PState Day"
set xrange [0:1440]
set yrange [*:*] 
set xtics time 60 format "%tM"
set xzeroaxis linetype 16 linewidth 0.5
set output "/run/mcp/pstate.svg"
p pstate u 1:(0):"pv"        w filledc below ls 1 t "pv",\
      '' u 1:(0):"pv"        w filledc above ls 1,\
      '' u 1:(0):"akku"      w filledc below ls 2 t "akku",\
      '' u 1:(0):"akku"      w filledc above ls 2,\
      '' u 1:(0):"grid"      w filledc below ls 3 t "grid",\
      '' u 1:(0):"grid"      w filledc above ls 4,\
      '' u 1:"load" t "load" w lines ls 5


# gstate
set ylabel "GState"
set xrange [0:1440]
set yrange [0:2050] 
set y2range [0:*]
set xtics time 60 format "%tM"
set output "/run/mcp/gstate.svg"
p pstate u 1:(0):"pv" t "pv"    w filledc ls 1 axes x1y2,\
  gstate u 1:"pvmin"  t "pvmin" w lines   ls 2 axes x1y2,\
      '' u 1:"pvmax"  t "pvmax" w lines   ls 3 axes x1y2,\
      '' u 1:"pvavg"  t "pvavg" w lines   ls 4 axes x1y2,\
      '' u 1:"soc"    t "soc"   w lines   ls 6,\
      '' u 1:"surv"   t "surv"  w lines   lt 1,\
      '' u 1:"succ"   t "succ"  w lines   lt 3,\
      '' u 1:"foca"   t "foca"  w lines   lt 6,\
      1000            t "100%"  w lines   lt 8
      
set ylabel "Grid - Power"
set yrange [*:*]
unset y2range
set terminal svg size 1920,400
set output "/run/mcp/pstate-power.svg"
p pstate u 1:(0):"grid" w filledc below ls 3 t "↓sum",\
      '' u 1:(0):"grid" w filledc above ls 2 t "↑sum",\
      '' u 1:"p1"       w lines lt 1 t "P1",\
      '' u 1:"p2"       w lines lt 2 t "P2",\
      '' u 1:"p3"       w lines lt 3 t "P3"

set ylabel "Grid - Voltage"
set yrange [220:250] 
set output "/run/mcp/pstate-voltage.svg"
p pstate u 1:"v1" w lines t "V1", '' u 1:"v2" w lines t "V2", '' u 1:"v3" w lines t "V3"

set ylabel "Grid - Frequency +/-"
set yrange [-50:50]
set output "/run/mcp/pstate-frequency.svg"
p pstate u 1:"f"   w lines t "f"


# 24/7 pstate averages
set ylabel "PState Avg 24/7"
set xrange [0:24]
set yrange [*:*]
set xtics 1 format "%s"
set output "/run/mcp/pstate-average-247.svg"
p avg247 u 1:"grid"  t "grid"     w fsteps ls 4,\
      '' u 1:"akku"  t "akku"     w fsteps ls 2,\
      '' u 1:"load"  t "load"     w fsteps ls 5
