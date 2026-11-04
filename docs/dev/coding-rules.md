# 코딩 규칙

## 일반
- C++20. 예외는 경계(파일 읽기 등)에서만 쓰고, 오류는 `bool`/`std::string* error`로 돌려줍니다.
- 계층 규칙은 [architecture.md](architecture.md)를 따릅니다. `src/core` 아래에 UI나 창 관련 코드를 넣지 않습니다.
- 모델을 바꾸는 `Session` 메서드는 **변경 전에 `checkpoint()`**, 변경 뒤에 `changed(...)`를 부릅니다. 드래그처럼 한 동작이 여러 번 값을 바꾸면
  **처음에 한 번만** `checkpoint()`를 부르고 중간에는 값만 바꿉니다.
- UI 스레드가 아닌 곳에서 `NativeUi`를 부르지 않습니다. 다른 스레드의 결과는 `PostMessage`로 넘깁니다.
- 오디오 콜백(`fillInt16`, `pushRecorded`) 안에서는 메모리 할당, 파일 입출력, 오래 걸리는 잠금을 피합니다.

## 문자열과 번역
- UI에 보이는 영어 문구는 그대로 쓰고(`Canvas`가 번역), 숫자가 섞이면 `%1` 패턴을 씁니다.
- 코어 메시지는 `Session::tr`을 거칩니다.
- 파일 경로와 문자열은 내부에서 **UTF-8** `std::string`입니다. Windows API를 부르는 곳에서만 `widen`/`narrow`로 바꿉니다.

## 자주 나는 MSVC 오류
| 증상 | 원인과 해결 |
| --- | --- |
| `C4003 min/max`, `ParamSet` 정의 안 됨 같은 연쇄 오류 | `windows.h` 앞에 `NOMINMAX`가 없다. 파일 맨 위에서 정의하고 `-DNOMINMAX`도 쓴다 |
| `std::clamp`/`std::min`이 없다 | `#include <algorithm>` 누락(GCC는 다른 헤더가 끌어와 주므로 Linux에서 안 보인다) |
| 정의보다 먼저 쓴 `static` 함수 | 파일 위쪽에 선언을 둔다 |
| `fopen`/`sscanf` 경고 | `-D_CRT_SECURE_NO_WARNINGS`(이미 켜져 있음) |
| 링크 오류 `LNK2005` (CRT 충돌) | 네이티브와 VST3 SDK는 모두 `-MT`. 섞지 않는다 |

## 커밋 메시지와 버전
- 버전 문자열은 `src/main.cpp`, `src/MainWindow.cpp`, `src/native/Win32App.cpp`, `build.sh`, `.github/workflows/build.yml`,
  `installer/*.iss`, `README.md`에 있습니다. 올릴 때 모두 같이 바꾸고 `CHANGELOG.md`에 항목을 추가합니다.
- 핫픽스는 `0.10_1`처럼 밑줄 번호를 붙입니다.
