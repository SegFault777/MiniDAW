# MiniDAW 인수인계 메모 (v0.8 작업 중단 시점)

이 문서는 작업을 이어받는 사람(또는 다음 세션의 Claude)을 위한 것입니다. 현재 코드는 **v0.7 릴리스 상태 위에
v0.8 작업이 절반쯤 진행된 상태**입니다. 버전 문자열은 아직 `0.7`이고, v0.8로 올리지 않았습니다.

## 0. 가장 먼저 알아둘 것

- **작업 환경 제약**: 작업하던 샌드박스에는 Qt, MSVC, Windows가 없습니다. 그래서
  - `src/dsp/`, `src/core/`, `src/platform/`(Linux 쪽 경로) 는 `g++`로 컴파일하고 `tests/run.sh`로 검증했습니다.
  - **Qt UI(`src/*.cpp`), Qt 어댑터(`src/Engine.cpp`), Windows 전용 코드(WASAPI, Media Foundation, winmm)는
    한 번도 컴파일해 보지 못했습니다.** v0.5, v0.5_1에서 include 누락 때문에 CI가 실패한 전례가 있으니,
    다음 GitHub Actions 빌드에서 컴파일 오류가 나올 가능성이 높습니다. 로그를 보고 고치면 됩니다.
- 사용자는 한국어로 대화하고, 메모리(`/areas/minidaw.md`)는 한국어로 씁니다. 결과물은 항상 `MiniDAW.zip` 한 개로 전달합니다.
- 버전 표기는 `0.4_1`, `0.5_1`처럼 밑줄 패치 버전을 쓸 수 있습니다. 릴리스는 `v<버전>` 태그를 푸시하면 됩니다.

## 1. 지금 하던 것 (v0.8)

사용자 요청: "v0.8이다. 2단계 진행하고 Logic Pro 기능 7개 더 구현해."
(2단계 = Qt 없는 플랫폼 계층. 1단계는 v0.7에서 끝낸 Qt 없는 코어 분리.)

### 1-A. 2단계: Qt 없는 플랫폼 계층 — 코드는 거의 끝, 검증 안 됨

| 항목 | 상태 |
| --- | --- |
| `src/platform/Audio.*`, `AudioBackends.h` : 장치 목록, 출력/입력 스트림 인터페이스, 무음("null") 장치 | 완료, Linux에서 테스트 통과 |
| `src/platform/AudioWasapi.cpp` : WASAPI 공유 모드(이벤트 방식, 44.1 kHz 스테레오로 자동 변환) | 작성 완료, **Windows 빌드 미검증** |
| `src/platform/Midi.*` : winmm MIDI 입력 | 작성 완료, **Windows 빌드 미검증** |
| `src/platform/Decode.*` : WAV는 코어, MP3/AAC/M4A/FLAC/WMA는 Media Foundation | 작성 완료, **Windows 빌드 미검증** |
| `src/core/I18n.*` : 번역 TSV 파서(코어, Qt 없음) + `Translator.cpp`가 이를 사용 | 완료, 테스트 통과 |
| `src/Engine.cpp` 재작성 : QAudioSink/Source/Decoder/QThread 제거, 플랫폼 스트림 + 디코드 워커 스레드 + 3초 장치 폴링 | 작성 완료, **Qt 빌드 미검증** |
| `src/MidiInput.*`, `src/Preferences.cpp` : 플랫폼 API 사용으로 교체 | 작성 완료, **Qt 빌드 미검증** |
| `build.sh` : Qt Multimedia 제거, `src/platform/*.cpp` 추가, `mfplat mfreadwrite mfuuid` 링크 | 수정 완료, **미검증** |
| `.github/workflows/build.yml` : `modules: qtmultimedia` 제거 | 완료 |
| `src/AudioUtil.*` 삭제 (QAudioFormat 변환기) | 완료 |
| `tests/platform_tests.cpp` : 무음 장치 + Session 연동, 녹음 흐름, 디코드, i18n | 통과 (ThreadSanitizer 경고 없음) |

### 1-B. Logic Pro 기능 7개 — 코어 구현·테스트까지만 됨, UI 없음

| # | 기능 | 코어 API (`core::Session`) | 상태 |
| --- | --- | --- | --- |
| 1 | Legato (노트를 다음 노트까지 연장) | `legatoRegion` | 코어 완료, 테스트 통과 |
| 2 | Transpose (반음 이동) | `transposeRegion` | 코어 완료, 테스트 통과 |
| 3 | Crescendo / Decrescendo (벨로시티 램프) | `velocityRampRegion` | 코어 완료, 테스트 통과 |
| 4 | Repeat Regions (리전 N번 반복 복사) | `repeatRegion` | 코어 완료, 테스트 통과 |
| 5 | Join Regions (다음 리전과 합치기: MIDI, 같은 녹음의 인접 오디오) | `joinRegions` | 코어 완료, 테스트 통과 |
| 6 | 템포 감지 (Smart Tempo 비슷한 분석) | `detectRegionTempo`, `dsp::detectTempo` | **코어 작성됨, 정확도 문제로 테스트 2개 실패 (아래 2번)** |
| 7 | 드럼 머신 패턴 → MIDI 리전 변환 | `convertPatternToMidi` | 코어 완료, 테스트 통과 |

순수 MIDI 함수는 `src/core/MidiOps.*`, Session 연동은 `src/core/Features.cpp`, 템포 감지는 `src/dsp/TempoDetect.*`.

## 2. 현재 알려진 문제 (테스트 실패 2건)

`bash tests/run.sh` 결과: 모든 스위트 통과, **`core_tests`만 2건 실패**. `run.sh`는 `set -e`라서 core가 실패하면
맨 뒤에 붙은 `platform_tests`가 실행되지 않습니다. 확인하려면 `/tmp/minidaw_platform_tests`를 직접 실행하세요
(현재 통과).

1. `core_tests.cpp:428` — 클릭 트랙 **160 BPM을 80 BPM으로 검출**(옥타브 오류). 85/100/120/128/140 BPM은 1% 이내로 맞음.
   원인 추정: `detectTempo`의 콤(comb) 점수가 `a[l] + 0.5*a[2l] + 0.25*a[4l]`라서 두 배 길이 주기(절반 템포)가
   유리해짐. 수정 아이디어: 최적 lag `L`을 고른 뒤 `L/2`(두 배 템포)가 범위 안이면 `a[L/2] >= 0.7*a[L]`일 때 `L/2`를 채택.
2. `core_tests.cpp:447` — 드럼 머신 패턴(킥 0·8, 스네어 4·12, 하이햇 짝수 스텝, 100 BPM)을 **133 BPM으로 검출**
   (기대값 100). 133 BPM = 16분음표 3개 간격. 진단 중이었음: 렌더 결과의 온셋 시각이
   `0.000 0.598 0.900 1.196 1.800 2.096 ...`로 불규칙하게 보임(기대는 0.3초 간격의 하이햇 + 0.6초 간격의 킥/스네어).
   하이햇이 약해서 온셋 임계값을 못 넘는 것일 수도 있고, 콤 점수/prior 문제일 수도 있음.
   디버그용으로 `/tmp/dbg.cpp` 같은 작은 프로그램을 만들어 acf 값과 온셋 곡선을 출력해 보면 빠릅니다.
   기능이 끝까지 어렵다면 정확도 한계를 문서에 적고 테스트 허용 오차를 현실화하는 선택도 가능합니다
   (사용자에게 알릴 것).

## 3. 해야 할 것 (우선순위 순)

1. **템포 감지 두 문제 해결** (위 2번), `tests/run.sh` 전체 통과시키기.
2. **7개 기능의 UI 연결** (코어 완료분):
   - `src/Engine.h`에 전달 함수 추가: `legatoRegion, transposeRegion, velocityRampRegion, repeatRegion, joinRegions,
     detectRegionTempo, convertPatternToMidi` (모두 `m_s.…` 한 줄 전달).
   - `src/MainWindow.cpp/.h`: 액션 + 메뉴 + 대화상자. 기존 패턴(`arpeggiateDialog`, `selectedMidiRegion`)을 따라 하면 됨.
     - Edit > MIDI Transform 서브메뉴: Legato, Transpose…(반음 스핀 -48..48), Crescendo…(시작/끝 벨로시티).
     - Edit(또는 Region) 메뉴: Repeat Region…(복사 개수), Join Regions.
     - 오디오 리전: "Detect Tempo…" — 결과를 보여 주고 "프로젝트 템포를 따라가게"(`setRegionFlex(t,r,true,bpm,semis)`)
       또는 "프로젝트 템포를 이 값으로"(`setBpm`) 중 선택하게.
     - Drums 트랙 헤더 메뉴(또는 Track 메뉴): "Convert Pattern to MIDI Region".
3. **번역 추가** (ko, ja, zh_CN, es 4개 `translations/*.tsv`): 위 UI 문구 + 코어 메시지
   (`There is no region after this one to join.`, `Only regions of the same kind can be joined.`,
   `These audio regions cannot be joined: …`, `The pattern is empty.`, `%1 (MIDI)` 등).
   `tests/platform_tests.cpp`가 "최신 문자열이 모든 언어에 있는지" 일부 확인하니 키를 맞춰 넣을 것. 기존 키와 중복 금지.
4. **버전 0.8로 올리기**: `src/main.cpp`, `src/MainWindow.cpp`(`MINIDAW_VERSION`), `build.sh`(2곳),
   `.github/workflows/build.yml`(`'0.7-dev'`), `installer/MiniDAW.iss`(2곳), `README.md`(`**Version 0.7**`, `v0.7` 태그 예시).
5. **문서**: `CHANGELOG.md`에 `[0.8]` 추가, `README.md`에 "What is new in v0.8"와 단축키 표 갱신,
   그리고 아직 낡은 문구 고치기:
   - README 158행 "…with the *Qt Multimedia* module" → 더 이상 필요 없음.
   - README 198행 "Output goes through Qt Multimedia (WASAPI shared mode…)" → 자체 WASAPI 백엔드로.
   - `ARCHITECTURE.md`: 플랫폼 계층이 이제 Qt 없는 `src/platform/`이라는 점, 남은 Qt 의존(UI 위젯, QSettings,
     QTranslator 껍데기, QLibrary(플러그인/VST3), VST3 호스트 글루)을 갱신. "다음 단계"는 3단계(자체 UI 툴킷)만 남음.
6. **사용자에게 알릴 변경점**: 저장된 오디오 출력/입력 장치 ID가 Qt 형식에서 WASAPI 엔드포인트 ID로 바뀌어서
   **기존 환경설정의 장치 선택은 무시되고 시스템 기본 장치로 열립니다**(오류는 아님). OGG는 Media Foundation 기본 상태로는 열리지 않음
   (Qt Multimedia도 Windows에서는 같은 백엔드를 썼으므로 이전과 동일).
7. **Windows CI 빌드 확인** → 컴파일 오류 고치기. 특히 의심되는 곳:
   - `src/platform/AudioWasapi.cpp`: `initguid.h` + `functiondiscoverykeys_devpkey.h` 순서, `ComPtr`/`IID_PPV_ARGS`, `PropVariant*`.
   - `src/platform/Decode.cpp`: `mfapi.h/mfidl.h/mfreadwrite.h` include와 `MF_MT_*` 상수, 링크 라이브러리.
   - `src/Engine.cpp/.h`: `Engine.h`에서 `class QTimer;` 전방 선언 위치, `qint16` 사용, 람다 `mutable` 캡처.
   - `src/Preferences.cpp`: 제거한 `QMediaDevices` 나머지 참조 여부.
   - `build.sh`: `-I QtMultimedia` 제거 후 다른 모듈 경로 문제 없는지.
8. (선택) 설정 파일/`QSettings` 대체는 **UI 툴킷을 바꾸는 3단계에서** 하기로 판단함(지금은 UI만 QSettings를 씀).

## 4. 지금까지 한 것 (요약)

| 버전 | 내용 |
| --- | --- |
| v0.4_1 | `Preferences.cpp` include 누락 빌드 오류 수정 |
| v0.5 | 리전 복사/붙여넣기, 트랙 복제, 오디오 입력 장치+녹음 지연 보정, 리전 게인/페이드/리버스/노멀라이즈, 바운스 대화상자(16/24/32f, 디더, 노멀라이즈, 사이클 범위) |
| v0.5_1 | `MainWindow.cpp`의 `QPushButton` include 누락 수정 |
| v0.6 | 팬/센드 오토메이션, 자동 저장·복구, 마커, 트랙 이동/색상, 리전 음소거, 코러스·필터, 최근 프로젝트, 탭 템포, 플러그인 SDK(`plugin-sdk/`) |
| v0.7 | **Qt 없는 코어 분리**(`src/core`: Session, Json, Files, AudioIO, MidiOps), Qt `Engine`은 어댑터, Logic 기능 6개(Bounce in Place, 퀀타이즈 강도·스윙, 아르페지에이터, 스케일 퀀타이즈, 휴머나이즈, 코드 트리거) |
| v0.8 (진행 중) | 위 1-A, 1-B 참고 |

## 5. 코드 지도

```
src/dsp/        Qt 없음. 신스, 드럼킷, 이펙트(Chorus/Filter 포함), 템포맵, 스트레치, Drummer, MIDI 파일,
                AudioOps(페이드/게인/WAV 인코딩), NativeEffect(플러그인 API 래퍼), TempoDetect
src/core/       Qt 없음. Session(엔진 본체), ProjectIO(저장/불러오기/자동저장/가져오기/바운스),
                Features(Bounce in Place + MIDI 변환 연결), MidiOps(순수 MIDI 함수), Json, Files, AudioIO, I18n, Model
src/platform/   Qt 없음. Audio(WASAPI/무음), Midi(winmm), Decode(WAV + Media Foundation)
src/Engine.*    Qt 어댑터: 코어 Hooks 연결, 플랫폼 스트림 소유, 디코드 워커, Qt 시그널
src/*.cpp       Qt Widgets UI (MainWindow, Timeline, PianoRoll, Panels, Controls, Browser, Preferences, Theme)
src/QtBridge.h  std::string / core::Color <-> QString / QColor
plugin-sdk/     mdplugin.h(C API), examples/(Tremolo, Bitcrusher), README
translations/   ko, ja, zh_CN, es (*.tsv, "영문<TAB>번역", `#name` 줄에 언어 이름)
tests/          run.sh 가 전부 빌드·실행 (g++/clang++, Qt 불필요)
```

핵심 규칙(자세한 것은 `ARCHITECTURE.md`):
- `src/dsp`, `src/core`, `src/platform`에는 `Q...` 타입을 쓰지 않는다. (샌드박스에 Qt가 없어 컴파일로 강제됨)
- 코어는 하드웨어/창을 모르고 `core::Session::Hooks`로만 바깥과 통신한다.
- 오디오 스레드는 `Session::fillInt16`/`pushRecorded`만 호출한다. 모델은 mutex 하나로 보호, 템포맵은 불변 shared_ptr.
- 새 UI 문구는 `tr("English")`로 쓰고 4개 tsv에 번역을 추가한다. 코어 메시지는 `Session::tr`을 거친다.

## 6. 작업 방식 메모

- 수정할 때는 Python으로 `assert s.count(a)==1` 식의 문자열 치환을 써서 조용히 어긋나는 것을 막았다.
- Qt 쪽 코드를 고친 뒤에는 (a) 중괄호/괄호 균형 검사, (b) UI가 호출하는 `Engine` 메서드가 `Engine.h`에 다 있는지 정규식으로 대조,
  (c) 쓰는 Qt 클래스의 `#include`가 있는지 확인하는 간이 점검을 했다. 그래도 컴파일러가 없어 한계가 있다.
- 사용자가 "v0.x 해보자 / 계속"이라고 하면 이전 합의(예: 3단계 로드맵)를 이어서 진행한다.
