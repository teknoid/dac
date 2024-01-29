#!/bin/sh

# using mosmix.py from https://www.hackitu.de/dwd_mosmix/
# value descriptions https://www.dwd.de/DE/leistungen/opendata/help/schluessel_datenformate/kml/mosmix_elemente_xls.xlsx

# crontab:
# 55 23	* * *	hje	/usr/local/bin/mosmix.sh SunD

F=MOSMIX_L_LATEST_10577.kmz
S=CHEMNITZ
V=$1

if [ -z $1 ]; then
  echo "Usage: $0 <value>"
  exit
fi

cd /tmp

rm -rf mosmix*json
rm -rf MOSMIX*kmz
rm -rf "$V.txt"

wget -q http://opendata.dwd.de/weather/local_forecasts/mos/MOSMIX_L/single_stations/10577/kml/$F

mosmix.py --in-file $F --out-file mosmix-timestamps.json timestamps
mosmix.py --in-file $F --out-file mosmix-forecasts.json forecasts

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
