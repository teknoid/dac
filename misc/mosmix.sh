#!/bin/sh
#
# mosmix.py 	https://www.hackitu.de/dwd_mosmix/
# descriptions 	https://www.dwd.de/DE/leistungen/opendata/help/schluessel_datenformate/kml/mosmix_elemente_xls.xlsx
# stations 		https://wettwarn.de/mosmix/mosmix.html
#
# crontab:
# 55 5	* * *	hje	/usr/local/bin/mosmix.sh 10579 Rad1h
#

# ID = 10577 = CHEMNITZ
# ID = 10579 = MARIENBERG
# ID = N4464 = BRAUNSDORF

RELOAD=1

if [ -z $1 ] || [ -z $2 ]; then
  echo "Usage: $0 <station> <value>"
  exit
fi

ID=$1
V=$2
F=MOSMIX_L_LATEST_$ID.kmz

cd /tmp

if [ $RELOAD -eq 1 ]; then
  rm -rf mosmix*json
  rm -rf MOSMIX*kmz
  rm -rf MOSMIX*kml

  wget -q http://opendata.dwd.de/weather/local_forecasts/mos/MOSMIX_L/single_stations/$ID/kml/$F
  unzip -q -o $F
fi

TIMESTAMPS=mosmix-timestamps-$ID.json
FORECASTS=mosmix-forecasts-$ID.json

mosmix.py --in-file $F --out-file $TIMESTAMPS timestamps
mosmix.py --in-file $F --out-file $FORECASTS forecasts

NAME=`jq -r 'keys[] as $k | $k' $FORECASTS`

OUT="$V-$NAME.txt"
rm -rf $OUT

case $V in

*1|*1h)
# hourly values - sum up for one day

  TS=`jq .[0] $TIMESTAMPS`
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

  jq $S1' | add' $FORECASTS > $OUT
  jq $S2' | add' $FORECASTS >> $OUT
  jq $S3' | add' $FORECASTS >> $OUT
  ;;

*)
# other values - print the daily 06:00 value

  for i in `seq 0 72`; do
    X=`jq .$NAME.$V[$i] $FORECASTS`
    if [ "$X" != "null" ]; then
       Y=`jq .[$i] $TIMESTAMPS`
       Z=`date +%H -u -d "@$Y"`
       if [ "$Z" -eq "06" ]; then
         echo $Z=$X >> "$V.txt"
       fi
     fi
  done
  ;;

esac
