#!/bin/sh
set -ex

autoreconf -f -i || exit 1
intltoolize --copy --force || exit 1 
./configure ${@}
