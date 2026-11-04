# 기능 추가 순서

예: "선택한 MIDI 리전의 노트 길이를 절반으로 줄이는 *Half Lengths*" 기능

## 1. 순수 함수 (`src/core/MidiOps.*`)
입력과 출력이 모두 `std::vector<Note>`인 함수로 만듭니다. 화면도 `Session`도 몰라도 되므로 테스트하기 가장 쉽습니다.
```cpp
std::vector<Note> halveLengths(const std::vector<Note>& notes);
```

## 2. 세션 연결 (`src/core/Features.cpp`, `Session.h`)
`editNotes`가 잠금, 되돌리기 지점, 변경 알림을 처리해 주므로 한 줄입니다.
```cpp
bool Session::halveLengthsRegion(int t, int r) { return editNotes(t, r, [&](const std::vector<Note>& n, double) { return halveLengths(n); }); }
```
오디오 리전 기능은 `Region`을 직접 바꿉니다(`stripSilenceRegion` 참고). 모델을 바꾸기 전에 `checkpoint()`를 부르고 끝에 `changed(false)`를 부릅니다.

## 3. 테스트 (`tests/core_tests.cpp`)
순수 함수 한두 개와 `Session`을 거친 한 개(실행 취소 포함).

## 4. 화면
- 메뉴: `UiInput.cpp`의 `functionsMenu()`에 `item("Half Lengths", "", [...], midi)`를 추가합니다.
- 매개변수가 있으면 `UiDialogs.cpp`에 `dlgXxx()`를 만들고 `Ui.h`에 선언합니다(대화상자 패턴은 [ui-toolkit.md](ui-toolkit.md)).
- 테스트(`ui_tests.cpp`)에서 메뉴/대화상자로 불러 상태가 바뀌는지 확인합니다.

## 5. 번역
영어 문구(`"Half Lengths"`, 결과 메시지)를 `translations/ko.tsv ja.tsv zh_CN.tsv es.tsv`에 추가합니다. 같은 키를 두 번 넣지 않습니다.

## 6. 문서
`docs/user/editing.md`의 표, `CHANGELOG.md`에 한 줄.

## 체크리스트
- [ ] `bash tests/run.sh` 통과
- [ ] `src/core` 등에 `Q...` 타입이 없다
- [ ] 되돌리기 한 번으로 원래대로 돌아온다
- [ ] 빈 입력, 범위 밖 인덱스, 잘못된 값에서 죽지 않는다
- [ ] 네 언어 번역
