#!/bin/bash

set -e -u -x

OUTPUT_DIR=RELEASE
# clean up output
mkdir -p $OUTPUT_DIR
rm $OUTPUT_DIR/* || true

# create version tag
VERSION=$1
DATE=$(date "+%y%m%d")
VERSION_TAG=$(echo "$VERSION-$DATE")

echo "Build as: $VERSION $OUTPUT_DIR $DATE"

##
# Clean targets once
##
CLEAN_DIRS=(MapsWithMeLite MapsWithMeLite.Samsung MapsWithMePro)
for DIR in ${CLEAN_DIRS[*]}
  do
    pushd $DIR
    ant clean
    popd
  done

##
# MapsWithMeLite: Google, Amazon, Yandex, SlideMe, AndroidPit
##
SOURCE_DIR=MapsWithMeLite
LITE_TARGETS=(google amazon yandex slideme androidpit)
BASE_NAME=MapsWithMeLite

pushd $SOURCE_DIR
  for TARGET in ${LITE_TARGETS[*]}
    do
      ant ${TARGET}-production
      SRC_NAME=${BASE_NAME}-${TARGET}-production.apk
      DEST_NAME=${BASE_NAME}-${VERSION_TAG}-${TARGET}.apk
      cp bin/$SRC_NAME ../$OUTPUT_DIR/$DEST_NAME
    done
popd

##
# MapsWithMeLite: Samsung
##
SOURCE_DIR=MapsWithMeLite.Samsung
TARGET=samsung
pushd $SOURCE_DIR
  ant ${TARGET}-production
  NAME=$(echo "${BASE_NAME}-${VERSION_TAG}-${TARGET}")
  cp bin/*-production.apk ../$OUTPUT_DIR/${NAME}.apk
popd

##
# MapsWithMePro: common version
##
SOURCE_DIR=MapsWithMePro
BASE_NAME=MapsWithMePro
pushd $SOURCE_DIR
  ant production
  NAME=$(echo "${BASE_NAME}-${VERSION_TAG}")
  cp bin/*-production.apk ../$OUTPUT_DIR/${NAME}.apk
popd

echo "Builded!"
ls -lh $OUTPUT_DIR
