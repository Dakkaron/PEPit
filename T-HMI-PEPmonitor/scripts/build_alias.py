# Compatibility shim for `pio run -t build`.
#
# The pioarduino espressif32 platform (and the standard PlatformIO
# espressif32 platform) only define the `buildprog` and `size` SCons
# targets, so `platformio run -t build` fails with:
#     *** Do not know how to make File target `build' (.../build). Stop.
#
# This script is loaded in the "post" scope, i.e. after the platform
# build script has registered its default targets, and aliases the
# conventional `build` target to those defaults so that
# `platformio run -t build` behaves exactly like `platformio run`.

from SCons.Script import DEFAULT_TARGETS  # pylint: disable=unused-import

Import("env")

env.Alias("build", DEFAULT_TARGETS)
