#!/bin/sh

# $1 is compiler (gcc, clang)
# $2 is "coverage" for coverage
# $3 is build options to pass to cmake
# $4 is test options to pass to ctest

echo "compiler   = " $1
echo "coverage   = " $2
echo "build opts = " $3
echo "test opts  = " $4

if [ "$1" != "gcc" ]; then
  if [ "$2" = "coverage" ]; then
    # only coverage for gcc compiler
    exit 0
  fi
fi

cmake .. $3
res=$?
if [ $res -ne 0 ]; then
  exit $res
fi

make rebuild_cache && make
res=$?
if [ $res -ne 0 ]; then
  exit $res
fi

echo "Test app using dynamic library does not crash if no NATS call is made"
test/dylib/nonats
res=$?
if [ $res -ne 0 ]; then
  exit $res
fi
export NATS_TEST_SERVER_VERSION="$(gnatsd -v)"
env NATS_TEST_TRAVIS=yes ctest --timeout 60 --output-on-failure $4
res=$?
if [ $res -ne 0 ]; then
  exit $res
fi

if [ "$2" = "coverage" ]; then
  ctest -T coverage
  res=$?
  if [ $res -ne 0 ]; then 
    exit $res
  fi
fi

