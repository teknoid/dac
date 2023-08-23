#/bin/sh

WORK=/ram/webcam
WWW=/xhome/www/webcam

case "$HOSTNAME" in
  nanopct4)
    OPTS="-loglevel error -f image2 -r 30 -hwaccel drm -hwaccel_device /dev/dri/card1"
    COPTSH="-c:v h264_rkmpp -b:v 8192k"
    COPTSL="-c:v h264_rkmpp -b:v 1536k -s 640:360"
    ;;
  picam)
    OPTS="-loglevel error -f image2 -r 30"
    COPTSH="-c:v h264_omx -b:v 8192k"
    COPTSL="-c:v h264_omx -b:v 1536k -s 640:360"
    ;;
  tron)
# with hardware acceleration: run 'vainfo' --> VAEntrypointEncSlice must be present
#   OPTS="-loglevel error -f image2 -r 30 -threads 4 -vaapi_device /dev/dri/renderD128"
#   COPTSH="-vf format=nv12,hwupload -c:v h264_vaapi -b:v 8M"
#   COPTSL="-vf format=nv12,hwupload,scale_vaapi=w=640:h=360 -c:v h264_vaapi -b:v 2M"
    OPTS="-loglevel error -f image2 -r 30 -threads 8"
    COPTSH="-c:v libx264 -preset fast -crf 22"
    COPTSL="-c:v libx264 -preset fast -crf 22 -vf scale=640:360"
    ;;
  *)
    echo "can only run on tron|picam|nanopct4"
    exit
esac

DATE=$(date +"%Y%m%d")
DOW=$(date +"%u")

# generate daily videos
FILE=$WWW/h/$DOW.mp4
test -e $FILE && rm $FILE
ffmpeg $OPTS -i $WORK/d/%04d.jpg $COPTSH $FILE
SIZE_H=$(ls -lh $FILE)

FILE=$WWW/l/$DOW.mp4
test -e $FILE && rm $FILE
ffmpeg $OPTS -i $WORK/d/%04d.jpg $COPTSL $FILE
SIZE_L=$(ls -lh $FILE)

# encode weekly files
FILE=$WORK/week/h$DOW.mp4
test -e $FILE && rm $FILE
ffmpeg $OPTS -i $WORK/w/%04d.jpg $COPTSH $FILE

FILE=$WORK/week/l$DOW.mp4
test -e $FILE && rm $FILE
ffmpeg $OPTS -i $WORK/w/%04d.jpg $COPTSL $FILE

# report
FRAMES=$(ls -l $WORK/d/*.jpg | wc -l)
FIRST=$(ls -la $WORK/d/0000.jpg)
LAST=$(ls -la $WORK/d/*.jpg | tail -n 1)
{
  echo
  echo timelapse-daily@$HOSTNAME
  echo ------------------------------------------------------------------------
  echo $FRAMES frames
  echo
  echo $SIZE_H
  echo $SIZE_L
  echo
  echo $FIRST
  echo $LAST
  echo
} | /usr/bin/mail -s webcam root@jecons.de
