#!/bin/sh

BIN="/usr/guimenu"



export LD_LIBRARY_PATH=/mnt/SDCARD:$LD_LIBRARY_PATH



while [ -f $BIN ]
do
#	sleep 2
	$BIN
done

