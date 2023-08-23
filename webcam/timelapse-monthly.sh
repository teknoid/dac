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
    COPTSH="-c:v h264_rkmpp -b:v 8192k"
    COPTSL="-c:v h264_rkmpp -b:v 1536k -s 640:360"
    VF=""
    ;;
  tron)
    OPTS="-loglevel error -f image2 -r 30 -threads 4 -vaapi_device /dev/dri/renderD128"
    COPTSH="-vf format=nv12,hwupload -c:v h264_vaapi -b:v 8M"
    COPTSL="-vf format=nv12,hwupload,scale_vaapi=w=640:h=360 -c:v h264_vaapi -b:v 2M"
    ;;
  *)
    echo "can only run on tron|picam|nanopct4"
    exit
esac

YEAR=$(date +"%Y")
LONGTIME=$WWW/y/$YEAR
FRAMES=$(ls -l $LONGTIME | wc -l)

# generate monthly videos
FILE=$WWW/h/month.mp4
test -e $FILE && rm $FILE
ffmpeg $OPTS -i $LONGTIME/%*.jpg $COPTSH $FILE
SIZE_H=$(ls -lh $FILE)

FILE=$WWW/l/month.mp4
test -e $FILE && rm $FILE
ffmpeg $OPTS -i $LONGTIME/%*.jpg $COPTSL $FILE
SIZE_L=$(ls -lh $FILE)

# report
{
  echo
  echo timelapse-monthly
  echo ------------------------------------------------------------------------
  echo $FRAMES frames
  echo
  echo $SIZE_H
  echo $SIZE_L
  echo
} | /usr/bin/mail -s webcam root@jecons.de
