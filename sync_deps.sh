#!/bin/bash
cd $(dirname $0)

PROJECT_DIR=$PWD

./install_tools.sh

if [ ! $(which depctl) ]; then
  echo "depctl not found. Trying to install..."
  brew install 0x1306a94/tap/depctl > /dev/null
else
  brew upgrade 0x1306a94/tap/depctl > /dev/null
fi

depctl || exit 1