#!/usr/bin/env bash
# MiniDAW build script (Windows, MSVC). Run it from Git Bash or MSYS2.
#
#   ./build.sh [all|qt|native|vulkan|<step>|clean]
#
# MiniDAW is built in three flavours that share one engine (src/core, src/dsp, src/platform):
#
#   MiniDAW.exe        the Native version: Qt-free GDI user interface (src/ui + src/native)   - the one that gets installed
#   MiniDAWVulkan.exe  the Vulkan version: a new GPU user interface (src/vulkan)              - needs Vulkan headers + a GLSL compiler to build
#   MiniDAWQt.exe      the original Qt version                                                - needs Qt to build
#
# Targets:
#   all             Qt + Native + Vulkan: build, deploy, portable zips and the Native installer
#   native          compile + link the Native version              -> build/MiniDAW.exe
#   native-deploy   install folder, portable folder and zips       -> dist-native/MiniDAW, dist-portable/MiniDAW, MiniDAW-windows-x64-portable.zip
#   installer       Inno Setup (Native version)                    -> installer-out/MiniDAW-<version>-setup.exe
#   native-release  native + native-deploy + installer             (CI: needs only MSVC and Inno Setup)
#   shaders         compile the GLSL shaders                       -> build/shaders/ui.vert.spv, ui.frag.spv
#   vulkan          shaders + compile + link the Vulkan version    -> build/MiniDAWVulkan.exe
#   vulkan-deploy   install folder, portable folder and zip        -> dist-vulkan/MiniDAW (installer payload), dist-vulkan-portable/MiniDAW, MiniDAW-Vulkan-windows-x64-portable.zip
#   vulkan-installer Inno Setup for the Vulkan version             -> installer-out/MiniDAW-Vulkan-<version>-setup.exe
#   vulkan-release  vulkan + vulkan-deploy + vulkan-installer
#   build           moc + compile + link the Qt version           -> build/MiniDAWQt.exe
#   deploy          windeployqt                                    -> dist/MiniDAWQt, MiniDAW-Qt-windows-x64-portable.zip
#   qt              build + deploy;   qt-installer: Inno Setup for the Qt version
#   clean           remove build outputs
#
# Requirements:
#   * The MSVC x64 environment must be active (cl.exe on PATH). In CI this is done by
#     ilammy/msvc-dev-cmd; locally start Git Bash from the "x64 Native Tools Command Prompt".
#   * Qt target only: QT_DIR (or QT_ROOT_DIR) points to the Qt kit, e.g. C:/Qt/6.8.3/msvc2022_64.
#   * Vulkan target only: the Vulkan headers (they are header-only; vulkan-1.dll is loaded at run time) and glslc or glslangValidator.
#       VULKAN_SDK           the LunarG SDK (Include/ and Bin/glslc.exe), or
#       VULKAN_HEADERS_DIR   a checkout of https://github.com/KhronosGroup/Vulkan-Headers   (+ GLSLC / GLSLANG_VALIDATOR = path of the compiler)
#     SKIP_VULKAN=1 makes "all" leave the Vulkan version out.
# Optional:
#   * VST3_SDK_DIR points to a checkout of https://github.com/steinbergmedia/vst3sdk (with submodules).
#     When it is set and builds cleanly, the Native and Vulkan versions are built with VST3 hosting.
#   * MINIDAW_VERSION overrides the version string (default 0.12).
set -euo pipefail

# Git Bash would otherwise rewrite arguments such as /nologo into file paths.
export MSYS2_ARG_CONV_EXCL='*'

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$ROOT/src"
BUILD="$ROOT/build"
DIST="$ROOT/dist/MiniDAWQt"
DIST_NATIVE="$ROOT/dist-native/MiniDAW"
DIST_PORTABLE="$ROOT/dist-portable/MiniDAW"
DIST_VULKAN="$ROOT/dist-vulkan/MiniDAW"
DIST_VULKAN_PORTABLE="$ROOT/dist-vulkan-portable/MiniDAW"
VERSION="${MINIDAW_VERSION:-0.12}"
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
    rm -rf "$BUILD" "$ROOT/dist" "$ROOT/dist-native" "$ROOT/dist-portable" "$ROOT/dist-vulkan" "$ROOT/dist-vulkan-portable" "$ROOT/installer-out" "$ROOT"/MiniDAW-*portable.zip
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
        cl -nologo "${OBJS[@]}" "${res[@]}" -Fe"$BUILD_W/MiniDAWQt.exe" -link \
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
    echo "built $BUILD/MiniDAWQt.exe"
}

# Builds one of the Qt-free programs (no Qt libraries needed). The caller sets
#   NF_TAG      object folder name            NF_EXE      output file name in build/
#   NF_SOURCES  the sources of the program    NF_FLAGS    extra compiler flags      NF_LIBS  extra libraries
# VST3 hosting is added when VST3_SDK_DIR points to the Steinberg SDK; if that fails the build continues without it.
# Source files of the VST3 SDK whose place differs from one SDK version to the next. They are found by name or by content instead
# of a fixed path. vst3_named_sources prints threadchecker_win32.cpp (ThreadChecker::create) and commonstringconvert.cpp
# (StringConvert::convert); vst3_plugframe_source prints the file that defines IPlugFrame::iid (nothing if there is none).
vst3_named_sources() {
    local root="$1" f n
    for n in threadchecker_win32.cpp commonstringconvert.cpp; do
        f="$(find "$root/public.sdk" "$root/base" "$root/pluginterfaces" -iname "$n" 2>/dev/null | head -n 1)"
        [ -n "$f" ] && echo "$f"
    done
    return 0
}
vst3_plugframe_source() {
    local root="$1"
    grep -rl --include='*.cpp' -E 'DEF_CLASS_IID *\( *(Steinberg::)?IPlugFrame *\)' "$root/public.sdk" "$root/pluginterfaces" "$root/base" 2>/dev/null \
        | grep -v -E 'samples|test|_mac|_linux' | head -n 1
    return 0
}

native_family() {
    command -v cl >/dev/null 2>&1 || die "cl.exe not found: activate the MSVC x64 environment first"
    local SRC_W; SRC_W="$(cygpath -m "$SRC")"
    mkdir -p "$BUILD"
    rm -rf "$BUILD/$NF_TAG" "$BUILD/$NF_TAG-vst3sdk"
    local NATIVE=(cl -nologo -std:c++20 -O2 -EHsc -MT -utf-8 -permissive- -Zc:__cplusplus -W3 -DNDEBUG -DUNICODE -D_UNICODE -DWIN32 -D_CRT_SECURE_NO_WARNINGS -DNOMINMAX
        "-DMINIDAW_VERSION=\"$VERSION\"" -I"$SRC_W" "${NF_FLAGS[@]}")
    local sources=("${NF_SOURCES[@]}")
    local res=()
    if [ -f "$ROOT/resources/app.rc" ]; then
        (cd "$ROOT/resources" && rc -nologo -fo "$(cygpath -m "$BUILD")/native_app.res" app.rc) && res=("$(cygpath -m "$BUILD")/native_app.res")
    fi
    local LIBS=(winmm.lib ole32.lib mfplat.lib mfreadwrite.lib mfuuid.lib shell32.lib user32.lib gdi32.lib comdlg32.lib advapi32.lib imm32.lib "${NF_LIBS[@]}")

    native_link() {
        echo "== $NF_TAG link"
        cl -nologo "${OBJS[@]}" "${res[@]}" -Fe"$(cygpath -m "$BUILD")/$NF_EXE" -link -SUBSYSTEM:WINDOWS -ENTRY:mainCRTStartup "${LIBS[@]}"
    }

    # ---- optional VST3: the SDK sources (static CRT like the rest), then the native sources with -DMINIDAW_VST3 ----
    local vst3_ok=0 vst3_objs=() SDK=""
    VST3_DEFS=""
    if [ -n "${VST3_SDK_DIR:-}" ] && [ -d "$VST3_SDK_DIR/public.sdk" ]; then
        echo "== $NF_TAG VST3 SDK"
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
        # files that move between SDK versions (found by name / content); duplicates of the list above are skipped
        local extra plugframe
        plugframe="$(vst3_plugframe_source "$VST3_SDK_DIR")"
        for extra in $(vst3_named_sources "$VST3_SDK_DIR") $plugframe; do
            case " ${sdk_sources[*]} " in *" $extra "*) continue ;; esac
            echo "   + $extra"
            sdk_sources+=("$extra")
        done
        if [ -z "$plugframe" ]; then
            echo "   no SDK file defines IPlugFrame::iid: Vst3Native.cpp defines it"
            VST3_DEFS="-DMINIDAW_DEFINE_PLUGFRAME_IID"
        fi
        local SDKCXX=(cl -nologo -std:c++17 -O2 -EHsc -MT -utf-8 -W1 -DRELEASE=1 -DUNICODE -D_UNICODE -DWIN32 -D_WINDOWS -DNOMINMAX -I"$SDK")
        OBJS=()
        if compile_all "$NF_TAG-vst3sdk" "${SDKCXX[@]}" -- "${sdk_sources[@]}"; then
            vst3_objs=("${OBJS[@]}")
            vst3_ok=1
        else
            warn "VST3 support could not be built for $NF_EXE and is disabled."
        fi
    fi

    echo "== $NF_TAG compile ($JOBS jobs)"
    local ok=0
    if [ "$vst3_ok" -eq 1 ]; then
        OBJS=("${vst3_objs[@]}")
        if compile_all "$NF_TAG" "${NATIVE[@]}" -DMINIDAW_VST3 ${VST3_DEFS:-} -DRELEASE=1 -I"$SDK" -- "${sources[@]}" && native_link; then
            ok=1
            echo "VST3 hosting: enabled"
        else
            warn "building $NF_EXE with VST3 failed; retrying without it"
        fi
    fi
    if [ "$ok" -eq 0 ]; then
        OBJS=()
        rm -rf "$BUILD/$NF_TAG"
        compile_all "$NF_TAG" "${NATIVE[@]}" -- "${sources[@]}" || die "$NF_EXE compilation failed"
        native_link || die "$NF_EXE linking failed"
        echo "VST3 hosting: disabled"
    fi
    echo "built $BUILD/$NF_EXE"
}

do_native() {
    NF_TAG=native; NF_EXE=MiniDAW.exe; NF_FLAGS=(); NF_LIBS=()
    NF_SOURCES=("$SRC"/native/*.cpp "$SRC"/ui/*.cpp "$SRC"/dsp/*.cpp "$SRC"/core/*.cpp "$SRC"/platform/*.cpp)
    native_family
}

# ---- the Vulkan version ----
vulkan_include() {
    local d
    if [ -n "${VULKAN_HEADERS_DIR:-}" ]; then
        for d in "$VULKAN_HEADERS_DIR/include" "$VULKAN_HEADERS_DIR"; do
            [ -f "$d/vulkan/vulkan.h" ] && { echo "$d"; return; }
        done
    fi
    if [ -n "${VULKAN_SDK:-}" ] && [ -f "$VULKAN_SDK/Include/vulkan/vulkan.h" ]; then echo "$VULKAN_SDK/Include"; return; fi
    die "Vulkan headers not found: set VULKAN_SDK (LunarG SDK) or VULKAN_HEADERS_DIR (a checkout of KhronosGroup/Vulkan-Headers)"
}

do_shaders() {
    local out="$BUILD/shaders" c="" mode=""
    mkdir -p "$out"
    for c in "${GLSLC:-}" "${VULKAN_SDK:+$VULKAN_SDK/Bin/glslc.exe}" glslc; do
        [ -n "$c" ] && command -v "$c" >/dev/null 2>&1 && { mode=glslc; break; }
        c=""
    done
    if [ -z "$mode" ]; then
        for c in "${GLSLANG_VALIDATOR:-}" "${VULKAN_SDK:+$VULKAN_SDK/Bin/glslangValidator.exe}" glslangValidator; do
            [ -n "$c" ] && command -v "$c" >/dev/null 2>&1 && { mode=glslang; break; }
            c=""
        done
    fi
    [ -n "$mode" ] || die "no GLSL compiler found: install the Vulkan SDK (glslc) or set GLSLC / GLSLANG_VALIDATOR"
    local stage
    for stage in vert frag; do
        local in out_f
        in="$(cygpath -m "$SRC/vulkan/shaders/ui.$stage")"
        out_f="$(cygpath -m "$out/ui.$stage.spv")"
        if [ "$mode" = glslc ]; then "$c" -O --target-env=vulkan1.0 -o "$out_f" "$in" || die "shader compilation failed ($stage)"
        else "$c" -V --target-env vulkan1.0 -o "$out_f" "$in" || die "shader compilation failed ($stage)"
        fi
    done
    echo "shaders compiled with $mode -> $out"
}

do_vulkan() {
    local inc; inc="$(vulkan_include)"
    do_shaders
    NF_TAG=vulkan; NF_EXE=MiniDAWVulkan.exe; NF_LIBS=()
    NF_FLAGS=(-I"$(cygpath -m "$inc")")
    NF_SOURCES=("$SRC"/vulkan/*.cpp "$SRC"/ui/FontData.cpp "$SRC"/native/Vst3Native.cpp "$SRC"/dsp/*.cpp "$SRC"/core/*.cpp "$SRC"/platform/*.cpp)
    native_family
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
    [ -f "$BUILD/MiniDAWQt.exe" ] || die "build first: ./build.sh build"
    command -v windeployqt >/dev/null 2>&1 || export PATH="$(qt_root)/bin:$PATH"
    rm -rf "$ROOT/dist"
    mkdir -p "$DIST"
    cp "$BUILD/MiniDAWQt.exe" "$DIST/"
    windeployqt --release --no-translations --no-system-d3d-compiler "$(cygpath -m "$DIST/MiniDAWQt.exe")"
    cp -r "$ROOT/translations" "$DIST/translations"   # language files (plain text, one per language)
    cp -r "$ROOT/loops" "$DIST/loops"                 # sample loops for the Loop Browser
    cp -r "$ROOT/plugin-sdk" "$DIST/plugin-sdk"       # header, examples and documentation for plug-in authors
    build_example_plugins
    cp "$ROOT/LICENSE" "$ROOT/README.md" "$DIST/"
    powershell.exe -NoProfile -Command \
        "Compress-Archive -Path '$(cygpath -w "$DIST")' -DestinationPath '$(cygpath -w "$ROOT/MiniDAW-Qt-windows-x64-portable.zip")' -Force"
    echo "deployed to dist/MiniDAW and MiniDAW-Qt-windows-x64-portable.zip"
}

# Copies what every flavour ships next to its executable.
copy_payload() {
    local to="$1"
    cp -r "$ROOT/translations" "$to/translations"
    cp -r "$ROOT/loops" "$to/loops"
    cp -r "$ROOT/plugin-sdk" "$to/plugin-sdk"
    mkdir -p "$to/docs"
    [ -d "$ROOT/docs/user" ] && cp -r "$ROOT/docs/user" "$to/docs/user"
    build_example_plugins "$to"
    cp "$ROOT/LICENSE" "$ROOT/README.md" "$to/"
}

# A portable copy keeps its settings, autosaves and plug-ins in a "data" folder beside the program (core/AppPaths.h).
make_portable_marker() {
    local to="$1"
    mkdir -p "$to/data"
    echo "This file turns MiniDAW into a portable program: settings, autosaves and your plug-ins are kept in the 'data' folder next to the program. Delete it to use %LOCALAPPDATA%\\MiniDAW again." > "$to/MiniDAW.portable"
    cat > "$to/PORTABLE.txt" <<'TXT'
MiniDAW - portable edition

Run the program straight from this folder (or a USB stick). Nothing is written to the computer:
settings, autosaves and your own plug-ins live in the "data" folder next to the program.
The file MiniDAW.portable is what switches this mode on; delete it to use the normal location
(%LOCALAPPDATA%\MiniDAW) again.
TXT
}

zip_dir() {   # zip_dir <folder> <zip name>
    powershell.exe -NoProfile -Command \
        "Compress-Archive -Path '$(cygpath -w "$1")' -DestinationPath '$(cygpath -w "$ROOT/$2")' -Force"
}

# The Native version is the one that gets installed: install folder (no marker), then a portable copy with the marker.
do_native_deploy() {
    [ -f "$BUILD/MiniDAW.exe" ] || die "build first: ./build.sh native"
    rm -rf "$ROOT/dist-native" "$ROOT/dist-portable"
    mkdir -p "$DIST_NATIVE"
    cp "$BUILD/MiniDAW.exe" "$DIST_NATIVE/MiniDAW.exe"
    copy_payload "$DIST_NATIVE"
    mkdir -p "$DIST_PORTABLE"
    cp -r "$DIST_NATIVE"/. "$DIST_PORTABLE"/
    make_portable_marker "$DIST_PORTABLE"
    zip_dir "$ROOT/dist-portable/MiniDAW" "MiniDAW-windows-x64-portable.zip"
    echo "deployed: dist-native/MiniDAW (installer payload), dist-portable/MiniDAW and MiniDAW-windows-x64-portable.zip"
}

# Like the Native version: an install folder (no marker, the installer payload), then a portable copy with the marker.
do_vulkan_deploy() {
    [ -f "$BUILD/MiniDAWVulkan.exe" ] || die "build first: ./build.sh vulkan"
    [ -f "$BUILD/shaders/ui.vert.spv" ] || die "shaders missing: ./build.sh shaders"
    rm -rf "$ROOT/dist-vulkan" "$ROOT/dist-vulkan-portable"
    mkdir -p "$DIST_VULKAN/shaders"
    cp "$BUILD/MiniDAWVulkan.exe" "$DIST_VULKAN/MiniDAWVulkan.exe"
    cp "$BUILD"/shaders/*.spv "$DIST_VULKAN/shaders/"
    if ls "$ROOT"/resources/fonts/*.ttf >/dev/null 2>&1; then   # optional: full Material Symbols fonts replace the built-in icons of their style
        mkdir -p "$DIST_VULKAN/fonts"
        cp "$ROOT"/resources/fonts/* "$DIST_VULKAN/fonts/"
        echo "icon fonts copied: $(ls "$DIST_VULKAN/fonts" | wc -l) files"
    fi
    mkdir -p "$DIST_VULKAN/licenses"
    cp "$ROOT"/licenses/* "$DIST_VULKAN/licenses/"   # licences of the built-in icons (Material Symbols, Apache-2.0)
    copy_payload "$DIST_VULKAN"
    mkdir -p "$DIST_VULKAN_PORTABLE"
    cp -r "$DIST_VULKAN"/. "$DIST_VULKAN_PORTABLE"/
    make_portable_marker "$DIST_VULKAN_PORTABLE"
    zip_dir "$ROOT/dist-vulkan-portable/MiniDAW" "MiniDAW-Vulkan-windows-x64-portable.zip"
    echo "deployed: dist-vulkan/MiniDAW (installer payload), dist-vulkan-portable/MiniDAW and MiniDAW-Vulkan-windows-x64-portable.zip"
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

do_vulkan_installer() {
    [ -d "$DIST_VULKAN" ] || die "deploy first: ./build.sh vulkan-deploy"
    "$(find_iscc)" "/DMyAppVersion=$VERSION" "$(cygpath -w "$ROOT/installer/MiniDAW-Vulkan.iss")"
    echo "Vulkan installer written to installer-out/"
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
    shaders) do_shaders ;;
    vulkan) do_vulkan ;;
    vulkan-deploy) do_vulkan_deploy ;;
    vulkan-installer) do_vulkan_installer ;;
    vulkan-release) do_vulkan; do_vulkan_deploy; do_vulkan_installer ;;
    qt) do_build; do_deploy ;;
    all)
        do_build; do_deploy
        do_native; do_native_deploy; do_installer
        if [ "${SKIP_VULKAN:-0}" = 1 ]; then warn "SKIP_VULKAN=1: the Vulkan version is not built"; else do_vulkan; do_vulkan_deploy; fi
        ;;
    clean) do_clean ;;
    *) die "unknown target '$TARGET' (all | qt | native | vulkan | native-release | vulkan-release | native-deploy | vulkan-deploy | vulkan-installer | shaders | installer | build | deploy | qt-installer | clean)" ;;
esac
