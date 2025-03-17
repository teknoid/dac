#/bin/sh

HM=$(date +"%H%M")

WORK=/ram/webcam
WWW=/xhome/www/webcam

H="1280x720"
L="640x360"

# check if WWW directory is available
test -d $WWW || exit

# make a copy
cp $WORK/current.jpg $WORK/currentx.jpg

# daily: create image, increment index, create timestamp
DINDEX=`cat $WORK/d/.index`
DFILE=$WORK/d/`printf %04d $DINDEX`.jpg
cp $WORK/current.jpg $DFILE
DINDEX=`expr $DINDEX + 1`
echo $DINDEX > $WORK/d/.index
echo `LC_ALL=de_DE.utf8 date +"%d.%m.%Y %X"` >$WORK/.mtime

# weekly: link image once per minute
W=`expr $DINDEX % 6`
if [ $W = 0 ]
then
  WINDEX=`cat $WORK/w/.index`
  WFILE=$WORK/w/`printf %04d $WINDEX`.jpg
  ln -s $DFILE $WFILE
  WINDEX=`expr $WINDEX + 1`
  echo $WINDEX > $WORK/w/.index
fi

# once per hour
if [ "${HM%*00}" != "$HM" ]
then
  # save a copy in WWW
  test -e $WWW/${HM}00.jpg || cp $WORK/current.jpg $WWW/${HM}00.jpg

  if [ $HM = 0900 -o $HM = 1200 -o $HM = 1500 ]
  then
    # save longtime images 3x per day
    YEAR=$(date +"%Y")
    DATE=$(date +"%Y%m%d")
    FILE=$WWW/y/$YEAR/${DATE}${HM}00.jpg
    test -e $FILE || cp $WORK/current.jpg $FILE
  fi
fi
