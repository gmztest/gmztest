#!/bin/bash
if [ $1 = "clean" ]
then
  rm gmztest
  cd src
  make clean
  cd ..
else
  cd src
  make
  cd ..
  file="gmztest"
  if [ -e $file ]
  then
    echo "GMZ ready"
  else
    ln -s src/gmztest
  fi
fi
