#!/bin/bash
#
# mosmix.py     https://www.hackitu.de/dwd_mosmix/
# descriptions  https://www.dwd.de/DE/leistungen/opendata/help/schluessel_datenformate/kml/mosmix_elemente_xls.xlsx
# stations      https://wettwarn.de/mosmix/mosmix.html
#
# crontab:
# mosmix calculation at 06:00 (04:00 GMT)
# 31 6    * * *   hje     /usr/local/bin/mosmix.sh N4464
# 32 6    * * *   hje     /usr/local/bin/mosmix.sh 10579
# 33 6    * * *   hje     /usr/local/bin/mosmix.sh 10577
#

# ID = 10577 = CHEMNITZ
# ID = 10579 = MARIENBERG
# ID = N4464 = BRAUNSDORF

RELOAD=1

if [ -z $1 ]; then
  echo "Usage: $0 <station>"
  exit
fi

ID=$1

cd /tmp
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

V1=($(jq .$NAME.TTT[] $FORECASTS))
V2=($(jq .$NAME.Rad1h[] $FORECASTS))
V3=($(jq .$NAME.SunD1[] $FORECASTS))

echo "idx, ts, TTT, Rad1h, SunD1" > $OUT
for i in "${!K[@]}"; do
  echo "${i}, ${K[$i]}, ${V1[$i]}, ${V2[$i]}, ${V3[$i]}" >> $OUT
done
