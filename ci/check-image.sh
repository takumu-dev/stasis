#!/bin/sh
# Checks that an image of Stasis's CI keeps its promises, and reports every one it breaks rather
# than just the first. An image's build runs it last, so an image that breaks one is never pushed.
#
#   check-image.sh <program>=<version>...
#
# Each argument names a program that the presets, the tests or the sanitizers call by its plain
# name, and the version its --version must report. A version is given as it is, such as gcc=16.2.0,
# or as the name of a tool that tools/install.cmake installed under /opt, such as clang=llvm, for
# the version pinned when it was installed. The script also checks that WASI_SDK_PATH names the
# wasi-sdk installed under /opt, as the tests find it.
set -u

failures=0

fail() {
    echo "check-image: $*" >&2
    failures=$((failures + 1))
}

# Prints the version that tools/install.cmake installed `tool` at, from the pin it leaves beside it:
# the tool's name, its version, the URL of its archive and the archive's SHA-256.
pinned_version() {
    [ -r "/opt/$1/.pin" ] && cut -d ' ' -f 2 "/opt/$1/.pin"
}

for argument in "$@"; do
    case $argument in
        ?*=?*) ;;
        *)
            fail "$argument isn't <program>=<version>"
            continue
            ;;
    esac
    program=${argument%%=*}
    version=${argument#*=}
    case $version in
        [0-9]*) ;;
        *)
            tool=$version
            if ! version=$(pinned_version "$tool"); then
                fail "$program: tools/install.cmake didn't install $tool under /opt"
                continue
            fi
            ;;
    esac
    if ! output=$("$program" --version 2>&1); then
        fail "$program: \`$program --version\` failed: $output"
        continue
    fi
    # The version must stand apart from any other number, so that 1.10.1 doesn't pass for 1.1.
    pattern=$(printf '%s\n' "$version" | sed 's/\./\\./g')
    if ! printf '%s\n' "$output" | grep -Eq "(^|[^0-9.])$pattern([^0-9.]|\$)"; then
        fail "$program isn't $version: \`$program --version\` printed: $output"
    fi
done

if [ "${WASI_SDK_PATH-}" != /opt/wasi-sdk ]; then
    fail "WASI_SDK_PATH is \"${WASI_SDK_PATH-}\", not /opt/wasi-sdk"
elif ! version=$(pinned_version wasi-sdk); then
    fail "tools/install.cmake didn't install wasi-sdk under /opt"
elif [ "$(head -n 1 /opt/wasi-sdk/VERSION)" != "$version" ]; then
    fail "/opt/wasi-sdk/VERSION doesn't start with wasi-sdk's pinned version, $version"
fi

if [ "$failures" -ne 0 ]; then
    echo "check-image: promises broken: $failures" >&2
    exit 1
fi
echo "check-image: every promise holds"
