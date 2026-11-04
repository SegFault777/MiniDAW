# UI 툴킷 (`src/ui`)

외부 라이브러리 없이 만든 작은 소프트웨어 렌더링 UI입니다. Windows 쪽에서는 만들어진 픽셀 버퍼를 창에 복사하기만 합니다.

## 구성 요소
| 파일 | 역할 |
| --- | --- |
| `Canvas.*` | ARGB 픽셀 버퍼, 클리핑, 안티앨리어싱 도형(둥근 사각형, 원, 호, 삼각형, 선), 텍스트, **번역 조회** |
| `FontData.*` | 내장 글꼴 표(Liberation Sans 11/13 px 보통·굵게). `tools/gen_font.py`로 생성. ASCII만 |
| `Ui.h`, `Ui.cpp` | `NativeUi`: 컨트롤 바, 툴바, 편곡 영역, 믹서, 상태 표시줄, 메뉴 그리기 |
| `UiInput.cpp` | 마우스, 키보드, 메뉴 정의, 파일 작업 |
| `UiDialogs.cpp` | 매개변수 대화상자들(변환, 퀀타이즈, 바운스, 환경설정 ...) |
| `UiEditors.cpp` | 아래 패널: 인스펙터, 디바이스(노브), 피아노 롤, 스텝, 드러머, 오토메이션 |
| `UiBrowser.cpp` | 브라우저 패널과 드래그앤드롭 |
| `Dialog.*` | 모달 대화상자 위젯 |

## 핵심 아이디어 1: 즉시 모드 히트 테스트
`paint()`가 컨트롤을 그릴 때마다 **클릭 영역과 동작을 함께 등록**합니다(`addHit`, `uiSlider`, `uiAction`, `uiKnob`).
마우스 이벤트는 직전 `paint()`가 만든 목록에서 가장 위에 있는 항목을 찾아 처리합니다.
그래서 그림과 클릭 영역이 어긋날 수 없고, 컨트롤 하나를 추가하는 데 코드가 한 곳이면 됩니다.

```cpp
uiAction(c, {x, y, 76, 22}, "Mute", [this, ti, ri] { m_s.setRegionMute(ti, ri, true); });
uiSlider(c, rect, "Volume", t01, "-3.0 dB", [this, ti](double t) { m_s.setVolume(ti, faderToGain(t)); }, color);
uiKnob(c, cx, cy, 20, "Cutoff", t01, "5000 Hz", setFn, resetFn, color);
```
자체 기하를 가진 편집기(피아노 롤, 스텝 격자, 오토메이션 레인)는 영역만 등록하고(`Hit::PianoGrid` 등) 마우스를 직접 계산합니다.

## 핵심 아이디어 2: 화면 폭이 둘
`m_winW`는 창 전체, `m_w`는 **편곡 영역과 아래 패널의 폭**(창 폭 − 브라우저 폭)입니다. 컨트롤 바, 툴바, 상태 표시줄, 메뉴, 대화상자는
`m_winW`를, 나머지는 `m_w`를 씁니다. 브라우저를 켜고 끄면 `relayout()`이 `m_w`를 다시 계산합니다.

## 핵심 아이디어 3: 번역은 그리는 곳에서
`Canvas::text`, `textWidth`, `fit`은 문자열을 그리기 전에 번역 표(`Canvas::setTranslations`)에서 **영문 원문 그대로** 조회합니다.
그러므로 UI 코드에는 `tr()`을 붙이지 않고 영어를 그대로 씁니다. 번역은 `translations/*.tsv`에 "원문<TAB>번역"을 추가하면 끝입니다.
숫자나 이름이 섞이는 메시지는 `NativeUi::msg("Saved %1", path)`처럼 **`%1` 자리표시자 패턴**을 쓰고, 패턴 자체를 번역 표에 넣습니다.

## 텍스트 렌더러
기본은 내장 글꼴(ASCII). Windows에서는 `Win32App.cpp`가 GDI 렌더러(`GdiText`)를 설치해 한글을 포함한 유니코드를 그립니다.
`ui::TextRenderer`만 구현하면 다른 렌더러(DirectWrite 등)로 바꿀 수 있습니다.

## 대화상자
`Dialog`는 값을 모으기만 합니다. 호출하는 쪽이 OK 콜백에서 `Session`에 적용합니다.
```cpp
openDialog(std::make_unique<Dialog>("Transpose",
    Dialog::Values{Field::number("Interval", last("transpose", 12), -48, 48, 1, 0, " semitones")},
    [this, t, r](const Dialog::Values& v) { m_s.transposeRegion(t, r, int(v[0].value)); }));
```
필드 종류: `number`, `slider`, `check`, `choice`, `input`(텍스트), `note`(설명), `button`. 마지막으로 쓴 값은 `m_last`에 기억합니다.
키보드 입력은 `WM_CHAR`가 `NativeUi::text()`로, 나머지 키는 `key()`로 들어옵니다.

## 좌표와 상수
`Ui.h` 맨 아래: `kControlH=54`, `kToolbarH=32`, `kRulerH=26`, `kHeaderW=236`(트랙 머리 폭이자 타임라인의 x 원점), `kStatusH=22`,
`kMixerH=344`(아래 패널 전체), `kBrowserW=250`. 트랙 높이는 `m_trackH`(34/60/96).

## 새 화면 요소를 추가하려면
1. `Ui.h`에 필요한 `Hit` 종류와 상태 변수를 추가하고 `drawXxx`를 `paint()` 흐름에 끼웁니다.
2. 컨트롤은 `uiAction`/`uiSlider`/`uiKnob`으로 그립니다. 마우스 반응이 특별하면 `mouseDown`의 `switch`에 `case Hit::Xxx`를 추가합니다.
3. 상태 표시줄 도움말이 필요하면 `hintFor()`에 한 줄을 추가합니다.
4. `tests/ui_tests.cpp`에 입력을 넣어 확인하는 테스트와 스크린샷을 추가합니다.
5. 새 영어 문구는 네 언어의 `.tsv`에 넣습니다(`tests/platform_tests.cpp`가 일부 키를 검사합니다).
