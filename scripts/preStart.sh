#!/bin/sh

echo "Running fpp-lor-inputpup PreStart Script"

BASEDIR=$(dirname $0)
cd $BASEDIR
cd ..
make
