# /usr/bin/gnuplot -p /home/hje/workspace-cpp/dac/misc/solar-minly.gp

pstates="/run/mcp/pstate-seconds.csv"

#pstat="/run/mcp/pstat.csv"
#slope="/run/mcp/slope.csv"
#ahead="/run/mcp/ahead.csv"

set terminal svg size 1920,800
set datafile separator whitespace
set key autotitle columnhead

set style line 1 linecolor rgb "#FAD967" lw 1			# PV
set style line 2 linecolor rgb "#93CF82" lw 1			# Akku
set style line 3 linecolor "orange-red" lw 1			# Grid
set style line 4 linecolor "turquoise" lw 1				# Surplus
set style line 5 linecolor "navy" lw 1					# Load
set style line 6 linecolor "olive" lw 1					# DTL

set style line 22 linecolor rgb "#93CF82" lw 1 dt 3		# Akku dashes
set style line 25 linecolor "navy" lw 1 dt 3		    # Target dashes

set grid
set xtics nomirror
set ytics nomirror
set tics format "%2.0s%c"


# pstate seconds
set ylabel "PState Hour"
set xrange [0:3600]
set yrange [*:*] 
set xtics time 300 format "%tM"
set xzeroaxis linetype 16 linewidth 0.5
set output "/run/mcp/pstate-seconds.svg"
p pstates u 1:"pv"      t "pv"      w lines ls 1,\
       '' u 1:"akku"    t "akku"    w lines ls 2,\
       '' u 1:"steal"   t "steal"   w lines ls 22,\
       '' u 1:"grid"    t "grid"    w lines ls 3,\
       '' u 1:"surp"    t "surp"    w lines ls 4,\
       '' u 1:"load"    t "load"    w lines ls 5,\
       '' u 1:"tgt"     t "target"  w lines ls 25,\
       '' u 1:"dtl"     t "dtl"     w lines ls 6,\
       '' u 1:"ramp"    t "ramp"    w lines lt 1

set terminal svg size 4320,1080
set output "/run/mcp/pstate-seconds-wide.svg"
p pstates u 1:"pv"      t "pv"      w lines ls 1,\
       '' u 1:"akku"    t "akku"    w lines ls 2,\
       '' u 1:"steal"   t "steal"   w lines ls 22,\
       '' u 1:"grid"    t "grid"    w lines ls 3,\
       '' u 1:"surp"    t "surp"    w lines ls 4,\
       '' u 1:"load"    t "load"    w lines ls 5,\
       '' u 1:"tgt"     t "target"  w lines ls 25,\
       '' u 1:"dtl"     t "dtl"     w lines ls 6,\
       '' u 1:"ramp"    t "ramp"    w lines lt 1

#set output "/run/mcp/pstate-slopes-ahead.svg"
#set y2range [-500:500]
#set y2tics
#p 0                     t "0"       w lines lt 0 axes x1y2,\
#  slope   u 1:"pv"      t "slope"   w lines ls 3 axes x1y2,\
#  ahead   u 1:"pv"      t "ahead"   w lines ls 2,\
#  pstat   u 1:"pv"      t "pstat"   w lines ls 1
      