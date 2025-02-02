#!/bin/bash
#
# mosmix.py     https://www.hackitu.de/dwd_mosmix/
# descriptions  https://www.dwd.de/DE/leistungen/opendata/help/schluessel_datenformate/kml/mosmix_elemente_xls.xlsx
# stations      https://wettwarn.de/mosmix/mosmix.html
#
# crontab:
# mosmix calculation at 06:00 (04:00 GMT)
# 31 6    * * *   hje     /usr/local/bin/mosmix.sh N4464 TTT Rad1h SunD1
# 32 6    * * *   hje     /usr/local/bin/mosmix.sh 10579 TTT Rad1h SunD1
# 33 6    * * *   hje     /usr/local/bin/mosmix.sh 10577 TTT Rad1h SunD1
#

# ID = 10577 = CHEMNITZ
# ID = 10579 = MARIENBERG
# ID = N4464 = BRAUNSDORF

RELOAD=1

if [ -z $1 ] || [ -z $2 ]; then
  echo "Usage: $0 <station> <value> {value} {value}"
  exit
fi

ID=$1
P1=$2
P2=$3
P3=$4
P4=$5

cd /work
F=MOSMIX_L_LATEST_$ID.kmz
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

NAME=$(jq -r 'keys[] as $k | $k' $FORECASTS)
OUT="$NAME.csv"

K=($(jq .[] $TIMESTAMPS))

if [ ! -z $P1 ]; then
  V1=($(jq .$NAME.$P1[] $FORECASTS))
fi
if [ ! -z $P2 ]; then
  V2=($(jq .$NAME.$P2[] $FORECASTS))
fi
if [ ! -z $P3 ]; then
  V3=($(jq .$NAME.$P3[] $FORECASTS))
fi
if [ ! -z $P4 ]; then
  V4=($(jq .$NAME.$P4[] $FORECASTS))
fi

echo "idx, ts, $P1, $P2, $P3, $P4" > $OUT
for i in "${!K[@]}"; do
  echo "${i}, ${K[$i]}, ${V1[$i]}, ${V2[$i]}, ${V3[$i]}, ${V4[$i]}" >> $OUT
done

mkdir -p mosmix/
mv *.kml mosmix/

