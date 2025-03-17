#/bin/sh

WORK=/ram/webcam
WWW=/xhome/www/webcam

# concatenate
for X in h l
do
  rm -rf /tmp/concat.txt
  for i in $(seq 1 7)
  do
    FILE=$WORK/week/$X$i.mp4
    test -s $FILE &&  echo "file '$FILE'" >> /tmp/concat.txt
  done

  FILE=$WWW/$X/week.mp4
  rm -rf $FILE
  ffmpeg -safe 0 -loglevel error -f concat -i /tmp/concat.txt -c copy $FILE
done


# report
SIZE_H=$(ls -lh $WWW/h/week.mp4)
SIZE_L=$(ls -lh $WWW/l/week.mp4)
{
  echo
  echo timelapse-weekly@$HOSTNAME
  echo ------------------------------------------------------------------------
  echo $SIZE_H
  echo $SIZE_L
  echo
} | /usr/bin/mail -s webcam root@jecons.de
