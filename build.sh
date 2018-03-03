#!/bin/bash
aqfile="gmzaq"

if [ $1 = "clean" ]
then
  cd aq
  rm gmzaq
  cd aq_src
  make clean
  cd ../..
elif [ $1 = "aq" ] 
then
  cd aq/aq_src
  make
  cd ..
  if [ -e $aqfile ]
  then
    echo "GMZ AQ ready"
  else
    ln -s aq_src/gmztest gmzaq
    echo "GMA AQ linked"
  fi
fi
