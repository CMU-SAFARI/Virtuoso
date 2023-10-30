#!/bin/bash

SNIPER_ROOT=/home/rahbera/mega-pages/sniper

#source $SNIPER_ROOT/sniparvars.sh
echo "Original cmd-> python2.7  $SNIPER_ROOT/run-sniper $1"
python2.7 $SNIPER_ROOT/record-trace $1
