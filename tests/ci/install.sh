#!/bin/sh

set -e

case "$(uname -s)" in
    Darwin)
        # OS X
        brew update
        brew install libevent jansson libxml2 check net-snmp
        ;;
    Linux)
        # Linux
        sudo apt-get -qqy update
        sudo apt-get -qqy install \
            automake autoconf libtool pkg-config \
            libsnmp-dev libxml2-dev libjansson-dev \
            libevent-dev libreadline-dev libbsd-dev \
            check
        # For integration tests
        sudo -H $(which python3) -m pip install -r tests/integration/requirements.txt
        ;;
esac
