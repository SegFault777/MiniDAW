# 환경설정

**File > Preferences...** (`Ctrl+,`)

| 항목 | 설명 |
| --- | --- |
| Output device | 소리를 내보낼 장치. *System Default*는 윈도우의 기본 장치를 따릅니다. |
| Buffer size | 256 ~ 4096샘플. 작을수록 반응이 빠르고(지연이 짧고) 소리가 끊기기 쉽습니다. 기본 2048(약 46 ms). |
| Input device | 녹음에 쓸 마이크/오디오 인터페이스 |
| Recording latency compensation | 녹음된 소리를 이 밀리초만큼 앞으로 당겨 박자에 맞춥니다. ([녹음](recording.md) 참고) |
| MIDI input | MIDI 키보드. 연결한 키보드는 선택한 Software Instrument 트랙을 연주합니다. |
| Language | 자동(윈도우 언어), English, 한국어, 日本語, 简体中文, Español. **다시 시작해야** 적용됩니다. |
| Autosave every | 자동 저장 간격(분). 0이면 끕니다. 기본 2분. |
| Loops folder | 브라우저가 찾아볼 루프 폴더 |
| VST3 folders | VST3 검색 폴더(`;`로 구분). 비워 두면 기본 폴더를 씁니다. |
| Rescan plug-ins | 플러그인을 다시 검색 |

설정은 `%LOCALAPPDATA%\MiniDAW\native-settings.ini`에 저장되고, 최근 연 프로젝트 5개도 여기에 기록됩니다.

## 자동 저장과 복구
프로그램이 작업 중에 자동 저장 파일을 `%LOCALAPPDATA%\MiniDAW\autosave`에 만듭니다.
정상적으로 종료하지 못했다면(전원 꺼짐, 충돌 등) 다음 실행 때 **복구할까요?**라고 물어 봅니다.
복구한 프로젝트는 꼭 **저장**해 주세요.
