#!/bin/bash -e

VERSION=$1

if [[ -z "$VERSION" ]]; then
  echo "ERROR: No version supplied! Required format: x.y" >&2
  exit 1
fi

echo "Checking that no staged changes exist"

CONSTANTS_H_PATH='T-HMI-PEPmonitor/src/constants.h'

if [[ $(git status -s -uno) ]]; then
  echo "ERROR: Git is not clean! Commit or revert all changes including staged changes!" >&2
  git status -s -uno >&2
  exit 1
fi

if [[ ! -f "$CONSTANTS_H_PATH" ]]; then
    echo "$CONSTANTS_H_PATH does not exist. Maybe not running from root directory of the repository?"
fi


echo "Setting version $VERSION in constants.h"

if grep '#define VERSION "'"$VERSION"'"' "$CONSTANTS_H_PATH"; then
  echo "Version already set correctly."
else
  sed -ie '0,/#define VERSION "[^"]*"/ s/#define VERSION "[^"]*"/#define VERSION "'"$VERSION"'"/' "$CONSTANTS_H_PATH"
  if [[ ! -f "$CONSTANTS_H_PATH"'e' ]]; then
    rm "$CONSTANTS_H_PATH"'e'
  fi

  git add "$CONSTANTS_H_PATH"
  git commit -m "Version bump for version $VERSION"
  git push
fi

echo 'Building via pio'
cd T-HMI-PEPmonitor
pio run
cd ..

echo 'Creating output directory'
if [ -d "release" ]; then
  rm -r release
fi

mkdir release

echo 'Copying firmware.bin to output directory'
cp T-HMI-PEPmonitor/.pio/build/lilygo-t-hmi/bootloader.bin release
cp T-HMI-PEPmonitor/.pio/build/lilygo-t-hmi/firmware.bin release
cp T-HMI-PEPmonitor/.pio/build/lilygo-t-hmi/partitions.bin release

echo 'Building SDCardContent archives'
cd SDCardContent
tar -cf ../release/SDCardContent.tar *
tar -czf ../release/SDCardContent.tar.gz *
zip -rq ../release/SDCardContent.zip *

cp ../release/firmware.bin .
tar -cf ../release/SDCardUpdate.tar --exclude=profiles.ini --exclude=systemconfig.ini *
tar -czf ../release/SDCardUpdate.tar.gz --exclude=profiles.ini --exclude=systemconfig.ini *
zip -rq ../release/SDCardUpdate.zip * -x profiles.ini -x systemconfig.ini
rm firmware.bin
cd ..

echo 'Updating path_to_latest_release'
echo -n 'https://github.com/Dakkaron/PEPit/releases/download/v'"$VERSION"'/' > path_to_latest_release
git add path_to_latest_release
git commit -m "Updated path_to_latest_release"
git push

echo "Adding git tag"
git tag "v$VERSION"
git push origin tag "v$VERSION"
