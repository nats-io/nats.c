#!/bin/sh
set -e
# check to see if gnatsd folder is empty
if [ ! "$(ls -A $HOME/gnatsd)" ]; then
  mkdir -p $HOME/gnatsd
  cd $HOME/gnatsd
  wget https://github.com/nats-io/gnatsd/releases/download/v1.2.0/gnatsd-v1.2.0-linux-amd64.zip -O gnatsd.zip
  unzip gnatsd.zip
  mv gnatsd-v1.2.0-linux-amd64/gnatsd .
else
  echo 'Using cached directory.';
fi

cd $HOME
# check to see if nats-streaming-server folder is empty
if [ ! "$(ls -A $HOME/nats-streaming-server)" ]; then
  mkdir -p $HOME/nats-streaming-server
  cd $HOME/nats-streaming-server
  wget https://github.com/nats-io/nats-streaming-server/releases/download/v0.10.2/nats-streaming-server-v0.10.2-linux-amd64.zip -O nats-streaming-server.zip
  unzip nats-streaming-server.zip
  mv nats-streaming-server-v0.10.2-linux-amd64/nats-streaming-server .
else
  echo 'Using cached directory.';
fi
