# A host's build that generates an interface's host side with stasis_generate, from a Stasis it
# doesn't build, so that only STASIS_GENERATOR can give it a stasis-gen, as in a cross-compiling
# build.
#
#   cmake -S consumer -B <build> -DSTASIS_SOURCE_DIR=<directory> -Dstasis_VERSION=<version>
#         -DINTERFACE=<file> [-DSTASIS_GENERATOR=<stasis-gen>]
#
# stasis_VERSION is the version of Stasis that stasis_generate takes itself to belong to.

cmake_minimum_required(VERSION 3.25...4.4)

project(stasis_generate_consumer LANGUAGES NONE)

include("${STASIS_SOURCE_DIR}/cmake/StasisGenerate.cmake")
stasis_generate(host "${INTERFACE}" OUTPUT_DIRECTORY generated VARIABLE host_side)
add_custom_target(host_side ALL DEPENDS ${host_side})
