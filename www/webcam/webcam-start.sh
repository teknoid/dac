#/bin/sh

# 59 4	* * *	hje	/xhome/www/webcam/webcam-start.sh reset

DATE=$(date +"%Y%m%d")
YEAR=$(date +"%Y")

WWW=/xhome/www/webcam
WORK=/ram/webcam

DEV=/dev/video0

OPTS="-q95 -w -x1280 -y720 -t10 -d$DEV"

# stop webcam capturing
if [ "$(pidof uvccapture)" ] 
then
  echo stop running uvccapture process $(pidof uvccapture)
  kill $(pidof uvccapture)
  sleep 1
fi

# check / create work directories
test -d $WORK || mkdir $WORK
test -d $WORK/d || mkdir $WORK/d
test -d $WORK/w || mkdir $WORK/w
test -d $WORK/week || mkdir $WORK/week
test -d $WWW/h || mkdir $WWW/h
test -d $WWW/l || mkdir $WWW/l
test -d $WWW/y/$YEAR || mkdir $WWW/y/$YEAR
#for i in $(seq 5 21)
#do 
#  FILE=`printf %02d $i`0000.jpg
#  test -L $WWW/h/$FILE || ln -s $WORK/$FILE $WWW/h/$FILE
#  test -L $WWW/l/$FILE || ln -s $WORK/$FILE $WWW/l/$FILE
#done

# delete all hourly/daily images and reset index
if [ "$1" = "reset" ]
then
  rm -rf $WORK/*.jpg
  rm -rf $WORK/d/*
  rm -rf $WORK/w/*
  echo 0 > $WORK/d/.index
  echo 0 > $WORK/w/.index
  rm -rf $WWW/*0000.jpg
fi

# start webcam capturing
v4l2-ctl -d $DEV -l 2>&1 1>/dev/null
v4l2-ctl -d $DEV --set-fmt-video=width=1280,height=720,pixelformat=MJPG

uvccapture $OPTS -o$WORK/current.jpg -c$WWW/postprocess.sh &
sleep 1
echo start uvccapture process $(pidof uvccapture)

v4l2-ctl -d $DEV --set-ctrl power_line_frequency=1
sleep 1
v4l2-ctl -d $DEV --set-ctrl backlight_compensation=0
sleep 1
v4l2-ctl -d $DEV --set-ctrl exposure_auto=3
sleep 1
v4l2-ctl -d $DEV --set-ctrl exposure_auto_priority=0
sleep 1
v4l2-ctl -d $DEV --set-ctrl gain=16
sleep 1
v4l2-ctl -d $DEV --set-ctrl white_balance_temperature_auto=0
sleep 1
v4l2-ctl -d $DEV --set-ctrl white_balance_temperature=6000
sleep 1
v4l2-ctl -d $DEV --set-ctrl focus_auto=1
#sleep 1
#v4l2-ctl -d $DEV --set-ctrl focus_auto=0
#sleep 1
#v4l2-ctl -d $DEV --set-ctrl focus_absolute=102
