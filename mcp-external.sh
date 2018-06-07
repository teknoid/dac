#!/bin/sh


CMD=$1

MUSIC="/public/music"

load() {
  ORD=$1
  DIR=$2
  mpc clear
  mpc ls "$DIR" | $ORD | mpc add
  mpc play
}

move() {
  TGT=$1
  CUR=`mpc -f %file% current`
  F=`basename "$CUR"`
  FILE="$MUSIC/$TGT/$F"
  if [ ! -f "$FILE" ]; then
    echo "moving $MUSIC/$CUR --> $MUSIC/$TGT"
#    mv "$MUSIC/$CUR" "$MUSIC/$TGT/"
    cp -a "$MUSIC/$CUR" "$MUSIC/$TGT/"
    mpc next
  fi
}

case $CMD in
  RANDOM)
    mpc clear; mpc listall | shuf -n 100 | mpc add; mpc play
    ;;
  0)
    load sort "01 sortiert/00 incoming"
    ;;
  1)
    load shuf "01 sortiert/01 aktuell"
    ;;
  2)
    load shuf "01 sortiert/02 top"
    ;;
  3)
    load shuf "01 sortiert/03 modern"
    ;;
  4)
    load shuf "01 sortiert/04 eurodance"
    ;;
  5)
    load shuf "01 sortiert/05 umz"
    ;;
  6)
    load shuf "01 sortiert/06 extended"
    ;;
  7)
    load shuf "01 sortiert/07 classics"
    ;;
  8)
    load shuf "01 sortiert/08 chill"
    ;;
  9)
    load shuf "01 sortiert/09 movie"
    ;;
  F1)
#    move "01 sortiert/01 aktuell"
    move "01 sortiert/04 eurodance"
#    move "01 sortiert/13 oldies"
    ;;
  F2)
    move "01 sortiert/02 top"
#    move "01 sortiert/17 house"
    ;;
  F3)
    move "01 sortiert/03 modern"
    ;;
  F4)
#    move "01 sortiert/07 classics"
#    move "01 sortiert/06 extended"
    move "01 sortiert/05 umz"
    ;;
  *)
    ;;
esac
