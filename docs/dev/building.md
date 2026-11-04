# 빌드

## Linux에서 테스트만 (Qt, Windows 필요 없음)
```
sudo apt-get install g++
bash tests/run.sh
```
`dsp`, `core`, `platform`, `ui`가 모두 `g++`로 컴파일되고 모든 테스트가 돕니다. 화면 테스트의 스크린샷은 `/tmp/minidaw_ui*.ppm`에 남습니다
(PNG로 바꾸려면 `python3 -c "from PIL import Image; Image.open('x.ppm').save('x.png')"`).

## Windows에서 네이티브 버전 (제품)
필요한 것: Visual Studio의 MSVC(x64), Git Bash, (설치 프로그램을 만들려면) Inno Setup 6. **Qt는 필요 없습니다.**
x64 Native Tools 프롬프트에서 Git Bash를 열고:

```
bash build.sh native-release     # 빌드 + 포터블 폴더/zip + 설치 프로그램
```

| 대상 | 하는 일 | 결과 |
| --- | --- | --- |
| `native` | 컴파일과 링크 | `build/MiniDAWNative.exe` |
| `native-deploy` | 번역, 루프, SDK, 예제 플러그인, 문서를 모아 포터블 폴더와 zip 만들기 | `dist-native/MiniDAW/`, `MiniDAW-windows-x64-portable.zip` |
| `installer` | Inno Setup | `installer-out/MiniDAW-<버전>-setup.exe` |
| `native-release` | 위 셋을 차례로 | |
| `qt`, `build`, `deploy`, `qt-installer` | 예전 Qt 버전 (Qt 6.5+ 필요) | `MiniDAW-Qt-windows-x64-portable.zip` |
| `all` | `qt` + `native-release` | |
| `clean` | 산출물 삭제 | |

환경 변수: `MINIDAW_VERSION`(기본값은 `build.sh` 맨 위), `VST3_SDK_DIR`(VST3 지원), `ISCC`(Inno Setup 위치), `JOBS`(병렬 컴파일 수).

## VST3 지원
Steinberg VST3 SDK를 내려받아 `VST3_SDK_DIR`로 가리키면 `native`가 SDK 소스를 정적 CRT(`-MT`)로 컴파일하고
`src/native/Vst3Native.cpp`를 `-DMINIDAW_VST3`로 컴파일합니다. **실패하면 경고를 내고 VST3 없이 다시 빌드**하므로
SDK 문제가 전체 빌드를 막지 않습니다. 빌드 로그의 마지막에 `VST3 hosting: enabled / disabled`가 찍힙니다.
```
git clone --depth 1 --recurse-submodules https://github.com/steinbergmedia/vst3sdk.git
export VST3_SDK_DIR=$PWD/vst3sdk
```

## 컴파일러 옵션 메모
네이티브는 `-std:c++20 -MT -utf-8 -permissive- -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS`로 빌드됩니다.
`windows.h`를 쓰는 파일은 그 **앞에** `NOMINMAX`가 정의되어 있어야 합니다(`dsp/Param.h`의 멤버 이름이 `min`/`max`).

## CI (`.github/workflows/build.yml`)
| 작업 | 러너 | 하는 일 |
| --- | --- | --- |
| `tests` | ubuntu | `bash tests/run.sh` |
| `native` | windows | VST3 SDK 내려받기(실패해도 계속), Inno Setup 설치, `build.sh native-release`, 설치 프로그램과 포터블 zip 업로드, 태그면 릴리스 |
| `qt` | windows | Qt 설치, `build.sh qt`, Qt 포터블 zip 업로드 |

릴리스는 `v<버전>` 태그를 푸시하면 됩니다(예: `v0.11`, `v0.10_1`).
