#!/bin/sh

if [ $# -eq 0 ]; then
  echo "Usage: $0 -c climit | -d dlimit | -r | -m minsoc"
  exit 0
fi

case $1 in
  -c)
    mosquitto_pub -h mqtt -t "solar/params/climit" -m $2
  ;;
  -d)
    mosquitto_pub -h mqtt -t "solar/params/dlimit" -m $2
  ;;
  -r)
    mosquitto_pub -h mqtt -t "solar/params/climit" -m 0
    mosquitto_pub -h mqtt -t "solar/params/dlimit" -m 0
  ;;
  -m)
    mosquitto_pub -h mqtt -t "solar/params/minsoc" -m $2
  ;;
esac
  
