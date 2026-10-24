#!/bin/bash
#
# mosmix.py     https://www.hackitu.de/dwd_mosmix/
# descriptions  https://www.dwd.de/DE/leistungen/opendata/help/schluessel_datenformate/kml/mosmix_elemente_xls.xlsx
# stations      https://wettwarn.de/mosmix/mosmix.html
#
# crontab:
# 55 5-19 * * *	hje	/usr/local/bin/mosmix.sh

# ID = 10577 = CHEMNITZ
# ID = 10579 = MARIENBERG
# ID = N4464 = BRAUNSDORF

RELOAD=1

F=MOSMIX_S_LATEST_240.kmz

cd /tmp
if [ $RELOAD -eq 1 ]; then
  rm -rf mosmix*json
  rm -rf MOSMIX*kmz
  rm -rf MOSMIX*kml

  wget -q https://opendata.dwd.de/weather/local_forecasts/mos/MOSMIX_S/all_stations/kml/$F
  unzip -q -o $F
fi

TIMESTAMPS=mosmix-timestamps.json
mosmix.py --in-file $F --out-file $TIMESTAMPS timestamps

for NAME in MARIENBERG CHEMNITZ BRAUNSDORF; do

  FORECASTS=mosmix-forecasts-$NAME.json
  mosmix.py --in-file $F --out-file $FORECASTS forecasts --limit "$NAME"

  K=($(jq .[] $TIMESTAMPS))

  V1=($(jq .$NAME.TTT[] $FORECASTS))
  V2=($(jq .$NAME.Rad1h[] $FORECASTS))
  V3=($(jq .$NAME.SunD1[] $FORECASTS))

  echo "idx, ts, TTT, Rad1h, SunD1" > $NAME.csv
  for i in "${!K[@]}"; do
    echo "${i}, ${K[$i]}, ${V1[$i]}, ${V2[$i]}, ${V3[$i]}" >> $NAME.csv
  done
done
