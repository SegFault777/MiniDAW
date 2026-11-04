# 구조

MiniDAW는 "Qt 프로그램"에서 **"엔진 위에 얹은 화면"** 으로 옮겨 왔습니다. 지금 제품은 Qt가 전혀 없는
**네이티브 버전**(`MiniDAW.exe`)이고, 예전 Qt 버전은 비교와 이식을 위한 포터블 빌드로만 남아 있습니다.

```
┌──────────────────────────────────────────────────────────────┐
│ ui (Qt 없음)           src/ui/    Canvas, NativeUi, Dialog,    │
│                        편집기(피아노 롤 ...), 브라우저, 번역    │
├──────────────────────────────────────────────────────────────┤
│ native (Windows)       src/native/Win32App.cpp  창, GDI 글꼴,  │
│                        파일 대화상자, 설정 파일, 자동 저장     │
│                        Vst3Native.cpp  VST3 호스트 + 편집 창    │
├──────────────────────────────────────────────────────────────┤
│ platform (Qt 없음)     src/platform/  WASAPI, winmm MIDI,      │
│                        Media Foundation 디코드, MiniDAW 플러그인│
├──────────────────────────────────────────────────────────────┤
│ core::Session          src/core/  모델, 실행 취소, 템포, 전송,│
│                        녹음, 렌더링, 프로젝트 파일, MIDI 연산 │
├──────────────────────────────────────────────────────────────┤
│ dsp                    src/dsp/  신스, 드럼킷, 이펙트, 템포 맵,│
│                        타임 스트레치, 드러머, 분석, 플러그인 API│
└──────────────────────────────────────────────────────────────┘
(Qt 버전: src/*.cpp, src/Engine.*, src/vst3/ - 더 이상 늘리지 않습니다)
```

## 규칙
1. `src/dsp`, `src/core`, `src/platform`, `src/ui`는 **C++ 표준 라이브러리만** 씁니다(`src/platform`은 Windows 전용 파일에서
   Windows API 사용 가능). `Q...` 타입 금지. `tests/run.sh`가 이 디렉터리들을 Qt 없는 `g++`로 컴파일하므로 규칙이 컴파일로 강제됩니다.
2. 코어는 하드웨어와 창을 모릅니다. 바깥과는 `core::Session::Hooks`로만 이야기합니다.
   (파일 디코드, 마이크 시작/중지, 이펙트/악기 만들기, 플러그인 상태 저장/복원, 메시지, "모델이 바뀌었다" 알림)
3. UI는 `core::Session`의 공개 API만 호출합니다. UI가 `Session` 내부 구조를 건드리지 않습니다.
4. 스레드: 오디오 스레드는 `Session::fillInt16`과 `pushRecorded`만 부릅니다. 나머지는 UI 스레드입니다.
   모델은 뮤텍스 하나로 보호되고, 템포 맵은 변하지 않는 `shared_ptr`로 통째로 교체됩니다.
   디코드 스레드와 플러그인 검색 스레드는 결과를 `PostMessage`로 UI 스레드에 넘깁니다.

## 디렉터리 지도
| 위치 | 내용 |
| --- | --- |
| `src/core/Base.h`, `Model.h` | 상수, `Text`, 트랙·리전·노트·마커 |
| `src/core/Session.*` | 엔진 본체: 편집, 실행 취소/다시 하기, 전송, 녹음, `renderBlock`, 마스터 버스 |
| `src/core/ProjectIO.cpp` | 프로젝트 파일(저장/불러오기/자동 저장), MIDI 파일, 오디오 가져오기, 바운스 |
| `src/core/Features.cpp`, `MidiOps.*` | Bounce in Place, 아르페지에이터, 퀀타이즈, 스케일 퀀타이즈, 휴머나이즈, 코드 트리거, 레가토, 트랜스포즈, 벨로시티 램프, 반복/합치기, 템포 감지, 패턴→MIDI, 겹침 수정, 중복 제거, 고정 길이, 역행, 반전, 벨로시티 배율, 무음 제거, 라우드니스 |
| `src/core/Json.*`, `Files.*`, `AudioIO.*`, `I18n.*` | JSON, UTF-8 파일, WAV/리샘플러/피크, 번역 표 |
| `src/dsp/` | 신스, 이펙트, `TempoMap`, 스트레치, `Drummer`, `TempoDetect`, `AudioAnalysis`(라우드니스, 무음 구간), `NativeEffect`(플러그인 API 래퍼) |
| `src/platform/` | `Audio*`(WASAPI/무음 장치), `Midi*`, `Decode*`, `PluginHost*`(MiniDAW 플러그인) |
| `src/ui/` | `Canvas`, `FontData`, `Ui*`(NativeUi), `Dialog`, 자세히는 [ui-toolkit.md](ui-toolkit.md) |
| `src/native/` | `Win32App.cpp`, `Vst3Native.*` |
| `plugin-sdk/` | 플러그인 C API(`mdplugin.h`)와 예제 |
| `translations/` | `ko`, `ja`, `zh_CN`, `es` (`*.tsv`, "영문<TAB>번역") |
| `installer/` | Inno Setup 스크립트(`MiniDAW.iss` = 네이티브, `MiniDAW-Qt.iss` = 예전 Qt) |
| `tests/` | Qt 없는 테스트 전부 (`run.sh`) |
| `tools/` | `gen_font.py`(내장 글꼴 표 생성) |

## 데이터 흐름 한눈에
```
마우스/키 (Win32) → NativeUi::mouseDown ... → Session::setVolume / moveRegion / ...
                                              ↓ (모델 변경, Hooks::changed)
타이머(30 fps) → NativeUi::tick() → paint(Canvas) → SetDIBitsToDevice
오디오 장치 스레드 → Session::fillInt16 (소리) / pushRecorded (녹음)
```
