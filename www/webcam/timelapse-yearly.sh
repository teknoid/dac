#/bin/sh

WORK=/ram/webcam
WWW=/xhome/www/webcam
OPTS="-loglevel error -f image2 -r 5"

H="1280:720"
L="640:360"

YEAR=$1
if [ -z $YEAR ]
then
  echo "Usage: $0 <year>"
  exit
fi
if [ ! -d $WWW/y/$YEAR ]
then
  echo "$WWW/y/$YEAR is not a directory"
  exit
fi


rm -rf $WWW/h/year-*.mp4
rm -rf $WWW/l/year-*.mp4

for i in 09 12 15
do
  SRC="$WWW/y/$YEAR/%*${i}0000.jpg"

  TGT="$WWW/h/year-$i.mp4"
  ffmpeg $OPTS -i $SRC -c:v libx264 $TGT

  TGT="$WWW/l/year-$i.mp4"
  ffmpeg $OPTS -i $SRC -vf scale=$L -c:v libx264 $TGT
done

# report
{
  echo
  echo timelapse-yearly
  echo ------------------------------------------------------------------------
  ls -lh $WWW/h/year-*.mp4
  ls -lh $WWW/l/year-*.mp4
  echo
} | /usr/bin/mail -s webcam root@jecons.de
