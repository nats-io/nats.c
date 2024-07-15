#!/bin/sh

# $1 is compiler (gcc, clang)
# $2 is "coverage" for coverage
# $3 is build options to pass to cmake
# $4 is test options to pass to ctest

echo "compiler   = " $1
echo "coverage   = " $2
echo "build opts = " $3
echo "test opts  = " $4

if [ "$NATS_TEST_SERVER_VERSION" != "" ]; then
  rm -fr $HOME/nats-server*
  rel=$NATS_TEST_SERVER_VERSION
  mkdir -p $HOME/nats-server-$rel
  if [ "$rel" = "latest" ]; then
    rel=$(curl -s https://api.github.com/repos/nats-io/nats-server/releases/latest | jq -r '.tag_name')
  fi

  if [ "$rel" != "${rel#v}" ] && wget https://github.com/nats-io/nats-server/releases/download/$rel/nats-server-$rel-linux-amd64.tar.gz; then
    tar -xzf nats-server-$rel-linux-amd64.tar.gz
    mv nats-server-$rel-linux-amd64 nats-server
  else
    for c in 1 2 3 4 5
    do
      echo "Attempt $c to download binary for main"
      rm ./nats-server
      curl -sf "https://binaries.nats.dev/nats-io/nats-server/v2@$rel" | PREFIX=. sh
      # We are sometimes getting nats-server of size 0. Make sure we have a
      # working nats-server by making sure we get a version number.
      v="$(./nats-server -v)"
      if [ "$v" != "" ]; then
        break
      fi
    done
  fi
  mv nats-server $HOME/nats-server-$rel
  PATH=$HOME/nats-server-$rel:$PATH
fi

export NATS_TEST_SERVER_VERSION="$(nats-server -v)"
if [ "$NATS_TEST_SERVER_VERSION" = "" ]; then
  echo "==============================================="
  echo "= Unable to get the server version, aborting! ="
  echo "==============================================="
  exit 1
fi

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
bin/nonats
res=$?
if [ $res -ne 0 ]; then
  exit $res
fi

export NATS_TEST_TRAVIS=yes
echo "Using NATS server version: $NATS_TEST_SERVER_VERSION"
ctest --timeout 60 --output-on-failure $4
res=$?
if [ $res -ne 0 ]; then
  exit $res
fi
