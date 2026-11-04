# 테스트

`bash tests/run.sh` 하나로 전부 빌드하고 실행합니다(`g++` 또는 `clang++`, Qt 불필요). `set -e`이므로 앞 단계가 실패하면 뒤는 돌지 않습니다.

| 파일 | 대상 |
| --- | --- |
| `dsp_timing_tests.cpp`, `dsp_music_tests.cpp`, `dsp_audioops_tests.cpp`, `dsp_effects_tests.cpp` | 템포 맵, 스트레치, 신스, 드러머, 이펙트, 오디오 연산 |
| `native_plugin_tests.cpp` | 플러그인 API(`plugin-sdk`)와 예제 플러그인 |
| `core_tests.cpp` | `Session` 전체: 편집, 실행 취소, 프로젝트 파일, MIDI 연산, 템포 감지, 라우드니스, 무음 제거 |
| `platform_tests.cpp` | 무음 오디오 장치와 `Session`, 녹음, 디코드, 번역 표, MiniDAW 플러그인 호스트 |
| `ui_tests.cpp` | 화면: 그리기, 마우스/키 입력, 대화상자, 편집기, 브라우저, 노브, 자동화 |

## 화면 테스트의 원리
`NativeUi`는 `Canvas`(메모리 속 픽셀)에 그리므로 창 없이 돌릴 수 있습니다. 테스트는 가짜 프로젝트를 만들고
`mouseDown`, `mouseMove`, `key` 같은 이벤트를 직접 넣은 뒤 `Session`의 상태를 확인합니다.
같은 방식으로 PPM 스크린샷을 저장하므로, **Windows 없이 화면 모양을 눈으로 확인**할 수 있습니다.

테스트 작성 요령
- 좌표는 레이아웃 상수에서 계산합니다(`kControlH`, `kToolbarH`, `kRulerH`, `kHeaderW`, 트랙 높이 60).
- 편집 뒤에 화면 상태가 필요하면 `u.paint(c)`를 부른 다음 확인합니다(`mouseDown`은 모델을 새로 읽지만 키 입력은 마지막 `paint`의 스냅샷을 씁니다).
- 영역이 필요하면 `pianoGridRect()`, `stepGridRect()`, `autoLaneRect()`, `browserRowRect(i)` 같은 접근자를 씁니다.

## 하지 못하는 것
Windows 전용 코드(`src/native/`, `src/platform/AudioWasapi.cpp`, `Midi.cpp`, `Decode.cpp`)는 Linux에서 컴파일되지 않습니다.
이 코드를 바꿀 때는 CI의 `native` 작업 로그가 유일한 검증입니다. 그래서 이 파일들은 **작고 단순하게** 유지하고,
판단이 필요한 로직은 `src/ui`나 `src/core`에 두어 테스트할 수 있게 합니다.
