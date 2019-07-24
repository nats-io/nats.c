#!/bin/sh
set -ex
# check to see if gnatsd folder is empty
if [ ! "$(ls -A $HOME/gnatsd)" ]; then
  mkdir -p $HOME/gnatsd
  cd $HOME/gnatsd
  wget https://github.com/nats-io/nats-server/releases/download/v1.3.0/gnatsd-v1.3.0-linux-amd64.zip -O gnatsd.zip
  unzip gnatsd.zip
  mv gnatsd-v1.3.0-linux-amd64/gnatsd .
else
  echo 'Using cached directory.';
fi

cd $HOME
# check to see if nats-streaming-server folder is empty
if [ ! "$(ls -A $HOME/nats-streaming-server)" ]; then
  mkdir -p $HOME/nats-streaming-server
  cd $HOME/nats-streaming-server
  wget https://github.com/nats-io/nats-streaming-server/releases/download/v0.14.3/nats-streaming-server-v0.14.3-linux-amd64.zip -O nats-streaming-server.zip
  unzip nats-streaming-server.zip
  mv nats-streaming-server-v0.14.3-linux-amd64/nats-streaming-server .
else
  echo 'Using cached directory.';
fi

# check to see if pbuf folder is empty
if [ ! "$(ls -A $HOME/pbuf)" ]; then
  sudo echo "deb http://archive.ubuntu.com/ubuntu disco main restricted universe multiverse" >> /etc/apt/sources.list
  sudo apt-get update
  sudo apt-get install libprotobuf-c-dev
  mkdir -p $HOME/pbuf
  cp -pr /usr/include/protobuf-c $HOME/pbuf/
  cp /usr/lib/x86_64-linux-gnu/libprotobuf-c.so* $HOME/pbuf
  sudo rm /usr/lib/x86_64-linux-gnu/libprotobuf-c.*
else
  echo 'Using cached directory.';
fi
cd $HOME