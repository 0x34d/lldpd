#!/bin/sh

set -e

./autogen.sh
./configure $LLDPD_CONFIG_ARGS
make
make check
make distcheck
sudo make install
