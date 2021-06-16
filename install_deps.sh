#!/bin/sh
set -ex
# check to see if the deps folder is empty
if [ ! "$(ls -A $HOME/deps)" ]; then
  mkdir -p $HOME/deps
  git clone https://github.com/nats-io/nats.c.deps.git $HOME/deps

  wget --no-check-certificate https://cmake.org/files/v3.20/cmake-3.20.4-linux-x86_64.tar.gz
  tar -xvf cmake-3.20.4-linux-x86_64.tar.gz > /dev/null
  mv cmake-3.20.4-linux-x86_64 $HOME/deps/cmake-install

else
  echo 'Using cached directory.';
fi
cd $HOME