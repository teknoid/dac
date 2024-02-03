#!/bin/sh
#
# mosmix.py 	https://www.hackitu.de/dwd_mosmix/
# descriptions 	https://www.dwd.de/DE/leistungen/opendata/help/schluessel_datenformate/kml/mosmix_elemente_xls.xlsx
# stations 		https://wettwarn.de/mosmix/mosmix.html
#
# crontab:
# 55 5	* * *	hje	/usr/local/bin/mosmix.sh Rad1h
#

#ID=10577
#NAME=CHEMNITZ

ID=10579
NAME=MARIENBERG

#ID=N4464
#NAME=BRAUNSDORF

F=MOSMIX_L_LATEST_$ID.kmz
V=$1

RELOAD=1

if [ -z $1 ]; then
  echo "Usage: $0 <value>"
  exit
fi

cd /tmp

if [ $RELOAD -eq 1 ]; then
  rm -rf mosmix*json
  rm -rf MOSMIX*kmz
  rm -rf MOSMIX*kml

  wget -q http://opendata.dwd.de/weather/local_forecasts/mos/MOSMIX_L/single_stations/$ID/kml/$F
  unzip -q -o $F
fi

mosmix.py --in-file $F --out-file mosmix-timestamps.json timestamps
mosmix.py --in-file $F --out-file mosmix-forecasts.json forecasts

rm -rf "$V.txt"

case $V in

*1|*1h)
# hourly values - sum up for one day

  TS=`cat mosmix-timestamps.json | jq .[0]`
  OFFSET=`date +%H -u -d "@$TS"`
  EOD=$((24 - $OFFSET))
  X1=0
  X2=$EOD
  Y1=$X2
  Y2=$(($Y1+24))
  Z1=$Y2
  Z2=$(($Z1+24))

  S1=".$NAME.$V[$X1:$X2]"
  S2=".$NAME.$V[$Y1:$Y2]"
  S3=".$NAME.$V[$Z1:$Z2]"

  R1=`cat mosmix-forecasts.json | jq $S1' | add'`
  R2=`cat mosmix-forecasts.json | jq $S2' | add'`
  R3=`cat mosmix-forecasts.json | jq $S3' | add'`

  echo $R1 > "$V.txt"
  echo $R2 >> "$V.txt"
  echo $R3 >> "$V.txt"
  ;;

*)
# other values - print the daily 06:00 value

  for i in `seq 0 72`; do
    X=`cat mosmix-forecasts.json | jq .$NAME.$V[$i]`
    if [ "$X" != "null" ]; then
       Y=`cat mosmix-timestamps.json | jq .[$i]`
       Z=`date +%H -u -d "@$Y"`
       if [ "$Z" -eq "06" ]; then
         echo $Z=$X >> "$V.txt"
       fi
     fi
  done
  ;;

esac
