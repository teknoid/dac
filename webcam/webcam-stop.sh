#/bin/sh

# 01 21	* * *	hje	/xxhome/www/webcam/webcam-stop.sh timelapse

DOW=$(date +"%u")
DOM=$(date +"%d")

WWW=/xxhome/www/webcam
WORK=/ram/webcam

# stop webcam capturing
if [ "$(pidof uvccapture)" ]
then
  kill $(pidof uvccapture)
  sleep 10
  /usr/bin/v4l2-ctl -l 2>&1 1>/dev/null
  sleep 10
  /usr/bin/v4l2-ctl -l 2>&1 1>/dev/null
fi

#  execute timelapse scripts
if [ "$1" = "timelapse" ]
then
  ssh tron -t 'bash -l -c "/xhome/www/webcam/timelapse-daily.sh"'
#  $WWW/timelapse-daily.sh
  if [ $DOW = 0 -o $DOW = 7 ]
  then
    ssh tron -t 'bash -l -c "/xhome/www/webcam/timelapse-weekly.sh"'
#    $WWW/timelapse-weekly.sh
  fi
  if [ $DOM = 01 ]
  then
    ssh tron -t 'bash -l -c "/xhome/www/webcam/timelapse-monthly.sh"'
#    $WWW/timelapse-monthly.sh
  fi
fi
