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
# Optional:
#   * VST3_SDK_DIR points to a checkout of https://github.com/steinbergmedia/vst3sdk (with submodules).
#     When it is set and builds cleanly, MiniDAW is built with VST3 hosting. Otherwise the build
#     continues without it.
#   * MINIDAW_VERSION overrides the version string (default 0.6).
set -euo pipefail

# Git Bash would otherwise rewrite arguments such as /nologo into file paths.
export MSYS2_ARG_CONV_EXCL='*'

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$ROOT/src"
BUILD="$ROOT/build"
DIST="$ROOT/dist/MiniDAW"
VERSION="${MINIDAW_VERSION:-0.6}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
TARGET="${1:-build}"

die() { echo "error: $*" >&2; exit 1; }
warn() { echo "warning: $*" >&2; }

qt_root() {
    local q="${QT_DIR:-${QT_ROOT_DIR:-}}"
    [ -n "$q" ] || die "set QT_DIR to your Qt kit directory (for example C:/Qt/6.8.3/msvc2022_64)"
    cygpath -m "$q"
}

do_clean() {
    rm -rf "$BUILD" "$ROOT/dist" "$ROOT/installer-out" "$ROOT"/MiniDAW-windows-x64-portable.zip
}

# Compiles the given sources in parallel into $BUILD/<dir>/ and appends the object paths to OBJS.
# usage: compile_all <objdir> <compiler args...> -- <sources...>
OBJS=()
compile_all() {
    local objdir="$1"; shift
    local args=()
    while [ "$1" != "--" ]; do args+=("$1"); shift; done
    shift
    mkdir -p "$BUILD/$objdir"
    local pids=() s o failed=0 n=0
    for s in "$@"; do
        o="$(cygpath -m "$BUILD")/$objdir/$((n++))_$(basename "${s%.*}").obj"
        OBJS+=("$o")
        while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 0.2; done
        "${args[@]}" -c "$(cygpath -m "$s")" -Fo"$o" &
        pids+=($!)
    done
    local p
    for p in "${pids[@]}"; do wait "$p" || failed=1; done
    return $failed
}

do_build() {
    command -v cl >/dev/null 2>&1 || die "cl.exe not found: activate the MSVC x64 environment first"
    local QT; QT="$(qt_root)"
    [ -x "$QT/bin/moc.exe" ] || die "moc.exe not found in $QT/bin"

    rm -rf "$BUILD"
    mkdir -p "$BUILD/moc"
    local SRC_W BUILD_W
    SRC_W="$(cygpath -m "$SRC")"
    BUILD_W="$(cygpath -m "$BUILD")"

    echo "== moc"
    local generated=() f out
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

    local COMMON=(cl -nologo -std:c++20 -O2 -EHsc -MD -utf-8 -permissive- -Zc:__cplusplus -W3
        -DNDEBUG -DUNICODE -D_UNICODE -DWIN32 -DQT_NO_DEBUG
        -DQT_CORE_LIB -DQT_GUI_LIB -DQT_WIDGETS_LIB -DQT_MULTIMEDIA_LIB -DQT_NETWORK_LIB
        "-DMINIDAW_VERSION=\"$VERSION\""
        -I"$SRC_W" -I"$BUILD_W/moc"
        -I"$QT/include" -I"$QT/include/QtCore" -I"$QT/include/QtGui" -I"$QT/include/QtWidgets"
        -I"$QT/include/QtMultimedia" -I"$QT/include/QtNetwork")

    local MAIN_SOURCES=("$SRC"/*.cpp "$SRC"/dsp/*.cpp "${generated[@]}")

    local res=()
    if [ -f "$ROOT/resources/app.rc" ]; then
        echo "== resources"
        (cd "$ROOT/resources" && rc -nologo -fo "$BUILD_W/app.res" app.rc)
        res=("$BUILD_W/app.res")
    fi
    local LIBS=(Qt6Widgets.lib Qt6Gui.lib Qt6Multimedia.lib Qt6Core.lib winmm.lib ole32.lib shell32.lib user32.lib advapi32.lib)

    link_main() {
        echo "== link"
        # Linking through cl avoids picking up GNU link.exe from Git Bash instead of the MSVC linker.
        cl -nologo "${OBJS[@]}" "${res[@]}" -Fe"$BUILD_W/MiniDAW.exe" -link \
            -SUBSYSTEM:WINDOWS -ENTRY:mainCRTStartup -LIBPATH:"$QT/lib" "${LIBS[@]}"
    }

    # ---- optional VST3 support: the SDK sources plus our host, then everything else with -DMINIDAW_VST3 ----
    local vst3_ok=0
    if [ -n "${VST3_SDK_DIR:-}" ] && [ -d "$VST3_SDK_DIR/public.sdk" ]; then
        echo "== VST3 SDK"
        local SDK; SDK="$(cygpath -m "$VST3_SDK_DIR")"
        local sdk_sources=() d g
        for d in base/source base/thread/source pluginterfaces/base public.sdk/source/vst/hosting; do
            for g in "$VST3_SDK_DIR/$d"/*.cpp; do
                [ -f "$g" ] || continue
                case "$g" in *_linux*|*_mac*|*test*) continue ;; esac
                sdk_sources+=("$g")
            done
        done
        for g in public.sdk/source/vst/vstinitiids.cpp public.sdk/source/vst/utility/stringconvert.cpp public.sdk/source/common/memorystream.cpp; do
            [ -f "$VST3_SDK_DIR/$g" ] && sdk_sources+=("$VST3_SDK_DIR/$g")
        done
        local SDKCXX=(cl -nologo -std:c++17 -O2 -EHsc -MD -utf-8 -W1 -DRELEASE=1 -DUNICODE -D_UNICODE -DWIN32 -D_WINDOWS -I"$SDK")
        OBJS=()
        if compile_all vst3sdk "${SDKCXX[@]}" -- "${sdk_sources[@]}" &&
           compile_all vst3host "${COMMON[@]}" -DRELEASE=1 -DMINIDAW_VST3 -I"$SDK" -- "$SRC/vst3/Vst3Host.cpp"; then
            vst3_ok=1
        else
            warn "VST3 support could not be built and is disabled."
        fi
    fi
    local vst3_objs=("${OBJS[@]}")

    echo "== compile ($JOBS jobs)"
    local ok=0
    if [ "$vst3_ok" -eq 1 ]; then
        OBJS=("${vst3_objs[@]}")
        if compile_all obj "${COMMON[@]}" -DMINIDAW_VST3 -- "${MAIN_SOURCES[@]}" && link_main; then
            ok=1
            echo "VST3 hosting: enabled"
        else
            warn "building with VST3 failed; retrying without it"
        fi
    fi
    if [ "$ok" -eq 0 ]; then
        OBJS=()
        rm -rf "$BUILD/obj"
        compile_all obj "${COMMON[@]}" -- "${MAIN_SOURCES[@]}" || die "compilation failed"
        link_main || die "linking failed"
        echo "VST3 hosting: disabled"
    fi
    echo "built $BUILD/MiniDAW.exe"
}

# Builds the example plug-ins of plugin-sdk/examples into dist/MiniDAW/plugins (failures are not fatal).
build_example_plugins() {
    local out="$DIST/plugins" tmp="$BUILD/plugins" f name
    mkdir -p "$out" "$tmp"
    for f in "$ROOT"/plugin-sdk/examples/*.cpp; do
        name="$(basename "${f%.cpp}")"
        if ( cd "$tmp" && cl -nologo -std:c++17 -O2 -EHsc -MD -LD "$(cygpath -m "$f")" -Fe"$(cygpath -m "$tmp/$name.dll")" >/dev/null 2>&1 ) && [ -f "$tmp/$name.dll" ]; then
            cp "$tmp/$name.dll" "$out/"
        else
            echo "warning: could not build the example plug-in $name"
        fi
    done
}

do_deploy() {
    [ -f "$BUILD/MiniDAW.exe" ] || die "build first: ./build.sh build"
    command -v windeployqt >/dev/null 2>&1 || export PATH="$(qt_root)/bin:$PATH"
    rm -rf "$ROOT/dist"
    mkdir -p "$DIST"
    cp "$BUILD/MiniDAW.exe" "$DIST/"
    windeployqt --release --no-translations --no-system-d3d-compiler "$(cygpath -m "$DIST/MiniDAW.exe")"
    cp -r "$ROOT/translations" "$DIST/translations"   # language files (plain text, one per language)
    cp -r "$ROOT/loops" "$DIST/loops"                 # sample loops for the Loop Browser
    cp -r "$ROOT/plugin-sdk" "$DIST/plugin-sdk"       # header, examples and documentation for plug-in authors
    build_example_plugins
    cp "$ROOT/LICENSE" "$ROOT/README.md" "$DIST/"
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
