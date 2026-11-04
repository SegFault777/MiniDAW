# 개발자 문서

| 문서 | 내용 |
| --- | --- |
| [architecture.md](architecture.md) | 전체 구조와 계층 규칙, 스레드, 디렉터리 지도 |
| [building.md](building.md) | Windows에서 빌드하기, `build.sh`의 대상(target), CI |
| [testing.md](testing.md) | 테스트 구성, 실행 방법, 스크린샷 테스트 |
| [ui-toolkit.md](ui-toolkit.md) | Qt 없는 UI 툴킷(`src/ui`)의 설계: 캔버스, 즉시 모드 히트 테스트, 다이얼로그, 번역 |
| [plugin-hosting.md](plugin-hosting.md) | MiniDAW 플러그인(SDK)과 VST3 호스팅 |
| [adding-features.md](adding-features.md) | 새 편집 기능, 대화상자, 번역 문구를 추가하는 순서 |
| [coding-rules.md](coding-rules.md) | 코딩 규칙, 스레드 규칙, MSVC에서 자주 나는 오류 |

시작하려면: `git clone` -> [building.md](building.md)의 "Linux에서 테스트만" -> `bash tests/run.sh`.
