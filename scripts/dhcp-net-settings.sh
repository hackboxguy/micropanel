#!/bin/sh
[ -z $1 ] && echo "mode=dhcp" && echo "RESULT:OK" && exit
echo "$1 $2 $3 $4" > /mnt/old-nvm/embedded/micropanel-1/micropanel/test.txt

