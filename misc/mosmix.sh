#!/bin/sh
#
# using mosmix.py from https://www.hackitu.de/dwd_mosmix/
# value descriptions https://www.dwd.de/DE/leistungen/opendata/help/schluessel_datenformate/kml/mosmix_elemente_xls.xlsx
#
# crontab:
# 15 4	* * *	hje	/usr/local/bin/mosmix.sh Rad1h
#

F=MOSMIX_L_LATEST_10577.kmz
S=CHEMNITZ
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

  wget -q http://opendata.dwd.de/weather/local_forecasts/mos/MOSMIX_L/single_stations/10577/kml/$F

  mosmix.py --in-file $F --out-file mosmix-timestamps.json timestamps
  mosmix.py --in-file $F --out-file mosmix-forecasts.json forecasts
fi

rm -rf "$V.txt"

# hourly values - sum up for one day
case $V in *1h)
  TS=`cat mosmix-timestamps.json | jq .[0]`
  OFFSET=`date +%H -u -d "@$TS"`
  EOD=$((24 - $OFFSET + 1))
  X1=0
  X2=$((X1 + EOD))
  Y1=$((X2 + 1))
  Y2=$((Y1 + 23))
  Z1=$((Y2 + 1))
  Z2=$((Z1 + 23))
    
  S1=".$S.$V[$X1:$X2]"
  S2=".$S.$V[$Y1:$Y2]"
  S3=".$S.$V[$Z1:$Z2]"

  R1=`cat mosmix-forecasts.json | jq $S1' | add'`
  R2=`cat mosmix-forecasts.json | jq $S2' | add'`
  R3=`cat mosmix-forecasts.json | jq $S3' | add'`

  echo $R1 > "$V.txt"
  echo $R2 >> "$V.txt"
  echo $R3 >> "$V.txt"

  exit
esac

# other values - print the daily 06:00 value
for i in `seq 0 72`; do
  X=`cat mosmix-forecasts.json | jq .$S.$V[$i]`
  if [ "$X" != "null" ]; then
     Y=`cat mosmix-timestamps.json | jq .[$i]`
     Z=`date +%H -u -d "@$Y"`
     if [ "$Z" -eq "06" ]; then
       echo $Z=$X >> "$V.txt"
     fi
   fi
done
