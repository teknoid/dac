# /usr/bin/gnuplot -p /home/hje/workspace-cpp/dac/misc/solar-minly.gp

pstates="/run/mcp/pstate-seconds.csv"

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
p pstates u 1:"pv"     t "pv"    w lines ls 1,\
       '' u 1:"akku"   t "akku"  w lines ls 2,\
       '' u 1:"grid"   t "grid"  w lines ls 3,\
       '' u 1:"diss"   t "diss"  w lines ls 4,\
       '' u 1:"load"   t "load"  w lines ls 5,\
       '' u 1:"surp"   t "surp"  w lines lt 1

set terminal svg size 4000,800
set output "/run/mcp/pstate-seconds-wide.svg"
p pstates u 1:"pv"     t "pv"    w lines ls 1,\
       '' u 1:"akku"   t "akku"  w lines ls 2,\
       '' u 1:"grid"   t "grid"  w lines ls 3,\
       '' u 1:"diss"   t "diss"  w lines ls 4,\
       '' u 1:"load"   t "load"  w lines ls 5,\
       '' u 1:"surp"   t "surp"  w lines lt 1
