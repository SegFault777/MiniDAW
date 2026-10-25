#!/usr/bin/env bash
# MiniDAW build script (Windows, MSVC + Qt 6). Run it from Git Bash or MSYS2.
#
#   ./build.sh [build|deploy|installer|all|clean]
#
#   build      moc + compile + link  -> build/MiniDAW.exe        (default)
#   deploy     windeployqt           -> dist/MiniDAW/ and MiniDAW-windows-x64-portable.zip
#   installer  Inno Setup            -> installer-out/MiniDAW-<version>-setup.exe
#   all        build + deploy + installer
#   clean      remove build/, dist/ and installer-out/
#
# Requirements:
#   * The MSVC x64 environment must be active (cl.exe on PATH). In CI this is done by
#     ilammy/msvc-dev-cmd; locally start Git Bash from the "x64 Native Tools Command Prompt".
#   * QT_DIR (or QT_ROOT_DIR) must point to the Qt kit, e.g. C:/Qt/6.8.3/msvc2022_64
#     with the Qt Multimedia module installed.
#   * MINIDAW_VERSION optionally overrides the version string (default 0.2.0).
set -euo pipefail

# Git Bash would otherwise rewrite arguments such as /nologo into file paths.
export MSYS2_ARG_CONV_EXCL='*'

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$ROOT/src"
BUILD="$ROOT/build"
DIST="$ROOT/dist/MiniDAW"
VERSION="${MINIDAW_VERSION:-0.2.0}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
TARGET="${1:-build}"

die() { echo "error: $*" >&2; exit 1; }

qt_root() {
    local q="${QT_DIR:-${QT_ROOT_DIR:-}}"
    [ -n "$q" ] || die "set QT_DIR to your Qt kit directory (for example C:/Qt/6.8.3/msvc2022_64)"
    cygpath -m "$q"
}

do_clean() {
    rm -rf "$BUILD" "$ROOT/dist" "$ROOT/installer-out" "$ROOT"/MiniDAW-windows-x64-portable.zip
}

do_build() {
    command -v cl >/dev/null 2>&1 || die "cl.exe not found: activate the MSVC x64 environment first"
    local QT; QT="$(qt_root)"
    [ -x "$QT/bin/moc.exe" ] || die "moc.exe not found in $QT/bin"

    rm -rf "$BUILD"
    mkdir -p "$BUILD/moc" "$BUILD/obj"
    local SRC_W BUILD_W
    SRC_W="$(cygpath -m "$SRC")"
    BUILD_W="$(cygpath -m "$BUILD")"

    echo "== moc"
    local generated=()
    local f out
    for f in "$SRC"/*.h; do
        if grep -q Q_OBJECT "$f"; then
            out="$BUILD_W/moc/moc_$(basename "${f%.h}").cpp"
            "$QT/bin/moc.exe" -I"$SRC_W" -o "$out" "$(cygpath -m "$f")"
            generated+=("$out")
        fi
    done
    for f in "$SRC"/*.cpp; do   # classes declared inside .cpp files produce "<name>.moc"
        if grep -q Q_OBJECT "$f"; then
            "$QT/bin/moc.exe" -I"$SRC_W" -o "$BUILD_W/moc/$(basename "${f%.cpp}").moc" "$(cygpath -m "$f")"
        fi
    done

    local CXX=(cl -nologo -std:c++20 -O2 -EHsc -MD -utf-8 -permissive- -Zc:__cplusplus -W3
        -DNDEBUG -DUNICODE -D_UNICODE -DWIN32 -DQT_NO_DEBUG
        -DQT_CORE_LIB -DQT_GUI_LIB -DQT_WIDGETS_LIB -DQT_MULTIMEDIA_LIB -DQT_NETWORK_LIB
        "-DMINIDAW_VERSION=\"$VERSION\""
        -I"$SRC_W" -I"$BUILD_W/moc"
        -I"$QT/include" -I"$QT/include/QtCore" -I"$QT/include/QtGui" -I"$QT/include/QtWidgets"
        -I"$QT/include/QtMultimedia" -I"$QT/include/QtNetwork")

    echo "== compile ($JOBS jobs)"
    local sources=("$SRC"/*.cpp "$SRC"/dsp/*.cpp "${generated[@]}")
    local objs=() pids=() s o
    for s in "${sources[@]}"; do
        o="$BUILD_W/obj/$(basename "${s%.cpp}").obj"
        objs+=("$o")
        while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 0.2; done
        "${CXX[@]}" -c "$(cygpath -m "$s")" -Fo"$o" &
        pids+=($!)
    done
    local failed=0 p
    for p in "${pids[@]}"; do wait "$p" || failed=1; done
    [ "$failed" -eq 0 ] || die "compilation failed"

    local res=()
    if [ -f "$ROOT/resources/app.rc" ]; then
        echo "== resources"
        (cd "$ROOT/resources" && rc -nologo -fo "$BUILD_W/app.res" app.rc)
        res=("$BUILD_W/app.res")
    fi

    echo "== link"
    # Linking through cl avoids picking up GNU link.exe from Git Bash instead of the MSVC linker.
    cl -nologo "${objs[@]}" "${res[@]}" -Fe"$BUILD_W/MiniDAW.exe" -link \
        -SUBSYSTEM:WINDOWS -ENTRY:mainCRTStartup -LIBPATH:"$QT/lib" \
        Qt6Widgets.lib Qt6Gui.lib Qt6Multimedia.lib Qt6Core.lib
    echo "built $BUILD/MiniDAW.exe"
}

do_deploy() {
    [ -f "$BUILD/MiniDAW.exe" ] || die "build first: ./build.sh build"
    command -v windeployqt >/dev/null 2>&1 || export PATH="$(qt_root)/bin:$PATH"
    rm -rf "$ROOT/dist"
    mkdir -p "$DIST"
    cp "$BUILD/MiniDAW.exe" "$DIST/"
    windeployqt --release --no-translations --no-system-d3d-compiler "$(cygpath -m "$DIST/MiniDAW.exe")"
    powershell.exe -NoProfile -Command \
        "Compress-Archive -Path '$(cygpath -w "$DIST")' -DestinationPath '$(cygpath -w "$ROOT/MiniDAW-windows-x64-portable.zip")' -Force"
    echo "deployed to dist/MiniDAW and MiniDAW-windows-x64-portable.zip"
}

do_installer() {
    [ -d "$DIST" ] || die "deploy first: ./build.sh deploy"
    local iscc="${ISCC:-}"
    if [ -z "$iscc" ]; then
        iscc="$(command -v ISCC.exe 2>/dev/null || true)"
        [ -n "$iscc" ] || iscc="/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
    fi
    [ -f "$iscc" ] || die "Inno Setup 6 not found (install it or set ISCC=/path/to/ISCC.exe)"
    "$iscc" "/DMyAppVersion=$VERSION" "$(cygpath -w "$ROOT/installer/MiniDAW.iss")"
    echo "installer written to installer-out/"
}

case "$TARGET" in
    build) do_build ;;
    deploy) do_deploy ;;
    installer) do_installer ;;
    all) do_build; do_deploy; do_installer ;;
    clean) do_clean ;;
    *) die "unknown target '$TARGET' (build | deploy | installer | all | clean)" ;;
esac
