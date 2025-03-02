#!/bin/bash

set -e -u -x

# TODO: Add "--untagged --tags android" when tags are properly set.
# TODO: Add validate-strings-file call to check for duplicates (and avoid Android build errors) when tags are properly set.
./tools/twine/twine --format android generate-all-string-files ./strings.txt ./android/res/
./tools/twine/twine --format apple generate-all-string-files ./strings.txt ./iphone/Maps/
./tools/twine/twine --format apple --file-name InfoPlist.strings generate-all-string-files ./iphone/plist.txt ./iphone/Maps/
./tools/twine/twine --format jquery generate-all-string-files ./data/cuisines.txt ./data/cuisine-strings/
#./tools/twine/twine --format tizen generate-all-string-files ./strings.txt ./tizen/MapsWithMe/res/ --tags tizen

