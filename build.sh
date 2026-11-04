#!/usr/bin/env bash
# MiniDAW build script (Windows, MSVC + Qt 6). Run it from Git Bash or MSYS2.
#
#   ./build.sh [native-release|native|native-deploy|installer|qt|build|deploy|all|clean]
#
#   native-release  native + native-deploy + installer          (what CI runs; needs only MSVC and Inno Setup)
#   native          compile + link the Qt-free version           -> build/MiniDAWNative.exe
#   native-deploy   portable folder and zip of the native version -> dist-native/MiniDAW, MiniDAW-windows-x64-portable.zip
#   installer       Inno Setup (native version)                  -> installer-out/MiniDAW-<version>-setup.exe
#   build           moc + compile + link the old Qt version      -> build/MiniDAW.exe
#   deploy          windeployqt                                  -> dist/MiniDAW and MiniDAW-Qt-windows-x64-portable.zip
#   qt              build + deploy;   qt-installer: Inno Setup for the Qt version
#   all             qt + native-release;   clean: remove build outputs
#
# Requirements (the Qt target also needs Qt, the native targets do not):
#   * The MSVC x64 environment must be active (cl.exe on PATH). In CI this is done by
#     ilammy/msvc-dev-cmd; locally start Git Bash from the "x64 Native Tools Command Prompt".
#   * QT_DIR (or QT_ROOT_DIR) must point to the Qt kit, e.g. C:/Qt/6.8.3/msvc2022_64
#     (Qt Multimedia is no longer needed: audio uses WASAPI, MIDI winmm, decoding Media Foundation).
# Optional:
#   * VST3_SDK_DIR points to a checkout of https://github.com/steinbergmedia/vst3sdk (with submodules).
#     When it is set and builds cleanly, MiniDAW is built with VST3 hosting. Otherwise the build
#     continues without it.
#   * MINIDAW_VERSION overrides the version string (default 0.11).
set -euo pipefail

# Git Bash would otherwise rewrite arguments such as /nologo into file paths.
export MSYS2_ARG_CONV_EXCL='*'

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$ROOT/src"
BUILD="$ROOT/build"
DIST="$ROOT/dist/MiniDAW"
DIST_NATIVE="$ROOT/dist-native/MiniDAW"
VERSION="${MINIDAW_VERSION:-0.11}"
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
    rm -rf "$BUILD" "$ROOT/dist" "$ROOT/dist-native" "$ROOT/installer-out" "$ROOT"/MiniDAW-*portable.zip
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
        -DQT_CORE_LIB -DQT_GUI_LIB -DQT_WIDGETS_LIB -DQT_NETWORK_LIB
        "-DMINIDAW_VERSION=\"$VERSION\""
        -I"$SRC_W" -I"$BUILD_W/moc"
        -I"$QT/include" -I"$QT/include/QtCore" -I"$QT/include/QtGui" -I"$QT/include/QtWidgets"
        -I"$QT/include/QtNetwork")

    local MAIN_SOURCES=("$SRC"/*.cpp "$SRC"/dsp/*.cpp "$SRC"/core/*.cpp "$SRC"/platform/*.cpp "${generated[@]}")

    local res=()
    if [ -f "$ROOT/resources/app.rc" ]; then
        echo "== resources"
        (cd "$ROOT/resources" && rc -nologo -fo "$BUILD_W/app.res" app.rc)
        res=("$BUILD_W/app.res")
    fi
    local LIBS=(Qt6Widgets.lib Qt6Gui.lib Qt6Core.lib winmm.lib ole32.lib mfplat.lib mfreadwrite.lib mfuuid.lib shell32.lib user32.lib advapi32.lib)

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

# Builds build/MiniDAWNative.exe: the Qt-free user interface (src/ui + src/native), no Qt libraries needed.
# VST3 hosting is added when VST3_SDK_DIR points to the Steinberg SDK; if that fails the build continues without it.
do_native() {
    command -v cl >/dev/null 2>&1 || die "cl.exe not found: activate the MSVC x64 environment first"
    local SRC_W; SRC_W="$(cygpath -m "$SRC")"
    mkdir -p "$BUILD"
    rm -rf "$BUILD/native" "$BUILD/native-vst3sdk"
    local NATIVE=(cl -nologo -std:c++20 -O2 -EHsc -MT -utf-8 -permissive- -Zc:__cplusplus -W3 -DNDEBUG -DUNICODE -D_UNICODE -DWIN32 -D_CRT_SECURE_NO_WARNINGS -DNOMINMAX
        "-DMINIDAW_VERSION=\"$VERSION\"" -I"$SRC_W")
    local sources=("$SRC"/native/*.cpp "$SRC"/ui/*.cpp "$SRC"/dsp/*.cpp "$SRC"/core/*.cpp "$SRC"/platform/*.cpp)
    local res=()
    if [ -f "$ROOT/resources/app.rc" ]; then
        (cd "$ROOT/resources" && rc -nologo -fo "$(cygpath -m "$BUILD")/native_app.res" app.rc) && res=("$(cygpath -m "$BUILD")/native_app.res")
    fi
    local LIBS=(winmm.lib ole32.lib mfplat.lib mfreadwrite.lib mfuuid.lib shell32.lib user32.lib gdi32.lib comdlg32.lib advapi32.lib)

    native_link() {
        echo "== native link"
        cl -nologo "${OBJS[@]}" "${res[@]}" -Fe"$(cygpath -m "$BUILD")/MiniDAWNative.exe" -link -SUBSYSTEM:WINDOWS -ENTRY:mainCRTStartup "${LIBS[@]}"
    }

    # ---- optional VST3: the SDK sources (static CRT like the rest), then the native sources with -DMINIDAW_VST3 ----
    local vst3_ok=0 vst3_objs=() SDK=""
    if [ -n "${VST3_SDK_DIR:-}" ] && [ -d "$VST3_SDK_DIR/public.sdk" ]; then
        echo "== native VST3 SDK"
        SDK="$(cygpath -m "$VST3_SDK_DIR")"
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
        local SDKCXX=(cl -nologo -std:c++17 -O2 -EHsc -MT -utf-8 -W1 -DRELEASE=1 -DUNICODE -D_UNICODE -DWIN32 -D_WINDOWS -DNOMINMAX -I"$SDK")
        OBJS=()
        if compile_all native-vst3sdk "${SDKCXX[@]}" -- "${sdk_sources[@]}"; then
            vst3_objs=("${OBJS[@]}")
            vst3_ok=1
        else
            warn "VST3 support could not be built for the native version and is disabled."
        fi
    fi

    echo "== native compile ($JOBS jobs)"
    local ok=0
    if [ "$vst3_ok" -eq 1 ]; then
        OBJS=("${vst3_objs[@]}")
        if compile_all native "${NATIVE[@]}" -DMINIDAW_VST3 -DRELEASE=1 -I"$SDK" -- "${sources[@]}" && native_link; then
            ok=1
            echo "VST3 hosting: enabled"
        else
            warn "building the native version with VST3 failed; retrying without it"
        fi
    fi
    if [ "$ok" -eq 0 ]; then
        OBJS=()
        rm -rf "$BUILD/native"
        compile_all native "${NATIVE[@]}" -- "${sources[@]}" || die "native compilation failed"
        native_link || die "native linking failed"
        echo "VST3 hosting: disabled"
    fi
    echo "built $BUILD/MiniDAWNative.exe"
}

# Builds the example plug-ins of plugin-sdk/examples into dist/MiniDAW/plugins (failures are not fatal).
build_example_plugins() {
    local out="${1:-$DIST}/plugins" tmp="$BUILD/plugins" f name
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
        "Compress-Archive -Path '$(cygpath -w "$DIST")' -DestinationPath '$(cygpath -w "$ROOT/MiniDAW-Qt-windows-x64-portable.zip")' -Force"
    echo "deployed to dist/MiniDAW and MiniDAW-Qt-windows-x64-portable.zip"
}

# The native version is the one that gets installed: portable folder + zip, then the Inno Setup installer.
do_native_deploy() {
    [ -f "$BUILD/MiniDAWNative.exe" ] || die "build first: ./build.sh native"
    rm -rf "$ROOT/dist-native"
    mkdir -p "$DIST_NATIVE/docs"
    cp "$BUILD/MiniDAWNative.exe" "$DIST_NATIVE/MiniDAW.exe"
    cp -r "$ROOT/translations" "$DIST_NATIVE/translations"
    cp -r "$ROOT/loops" "$DIST_NATIVE/loops"
    cp -r "$ROOT/plugin-sdk" "$DIST_NATIVE/plugin-sdk"
    [ -d "$ROOT/docs/user" ] && cp -r "$ROOT/docs/user" "$DIST_NATIVE/docs/user"
    build_example_plugins "$DIST_NATIVE"
    cp "$ROOT/LICENSE" "$ROOT/README.md" "$DIST_NATIVE/"
    powershell.exe -NoProfile -Command \
        "Compress-Archive -Path '$(cygpath -w "$DIST_NATIVE")' -DestinationPath '$(cygpath -w "$ROOT/MiniDAW-windows-x64-portable.zip")' -Force"
    echo "deployed to dist-native/MiniDAW and MiniDAW-windows-x64-portable.zip"
}

find_iscc() {
    local iscc="${ISCC:-}"
    if [ -z "$iscc" ]; then
        iscc="$(command -v ISCC.exe 2>/dev/null || true)"
        [ -n "$iscc" ] || iscc="/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
    fi
    [ -f "$iscc" ] || die "Inno Setup 6 not found (install it or set ISCC=/path/to/ISCC.exe)"
    echo "$iscc"
}

do_installer() {
    [ -d "$DIST_NATIVE" ] || die "deploy first: ./build.sh native-deploy"
    "$(find_iscc)" "/DMyAppVersion=$VERSION" "$(cygpath -w "$ROOT/installer/MiniDAW.iss")"
    echo "installer written to installer-out/"
}

# The old Qt version is only shipped as a portable zip; this builds an installer for it if somebody still wants one.
do_qt_installer() {
    [ -d "$DIST" ] || die "deploy first: ./build.sh deploy"
    "$(find_iscc)" "/DMyAppVersion=$VERSION" "$(cygpath -w "$ROOT/installer/MiniDAW-Qt.iss")"
    echo "Qt installer written to installer-out/"
}

case "$TARGET" in
    build) do_build ;;
    deploy) do_deploy ;;
    native) do_native ;;
    native-deploy) do_native_deploy ;;
    installer) do_installer ;;
    qt-installer) do_qt_installer ;;
    native-release) do_native; do_native_deploy; do_installer ;;
    qt) do_build; do_deploy ;;
    all) do_build; do_deploy; do_native; do_native_deploy; do_installer ;;
    clean) do_clean ;;
    *) die "unknown target '$TARGET' (native-release | native | native-deploy | installer | qt | build | deploy | qt-installer | all | clean)" ;;
esac
