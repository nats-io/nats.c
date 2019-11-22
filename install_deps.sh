#!/bin/sh
set -ex
# check to see if the deps folder is empty
if [ ! "$(ls -A $HOME/deps)" ]; then
  mkdir -p $HOME/deps
  git clone https://github.com/nats-io/nats.c.deps.git $HOME/deps
else
  echo 'Using cached directory.';
fi
cd $HOME