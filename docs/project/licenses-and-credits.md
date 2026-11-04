# 라이선스와 크레딧

## MiniDAW
**GPL-3.0-or-later**. 전문은 루트의 `LICENSE`에 있습니다.

## 함께 들어가는 것
| 구성 요소 | 라이선스 | 쓰이는 곳 |
| --- | --- | --- |
| **Liberation Sans** (글꼴) | SIL Open Font License 1.1 | `src/ui/FontData.cpp`에 문자 모양 표로 포함(스크린샷과 테스트용 내장 글꼴). Windows 앱은 시스템 글꼴(Segoe UI)을 쓴다 |
| **Steinberg VST3 SDK** | GPL-3.0 / 상용 이중 라이선스 | VST3 지원 빌드에서만 정적으로 링크 (저장소에는 들어 있지 않다) |
| 플러그인 SDK 헤더(`plugin-sdk/mdplugin.h`) | 프로젝트와 같은 GPL-3.0-or-later(예제 포함) | 플러그인 개발용 |
| 예제 루프(`loops/`) | 프로젝트에 포함된 예제 소리(저장소의 `loops/` 참고) | 브라우저의 기본 루프 |
| Windows API, Media Foundation, WASAPI | 운영체제 | 오디오, MIDI, 디코딩 |

## 상표
- Logic Pro, macOS는 Apple Inc.의 상표입니다.
- Ableton Live는 Ableton AG의 상표입니다.
- VST는 Steinberg Media Technologies GmbH의 상표입니다.
- Windows는 Microsoft Corporation의 상표입니다.

MiniDAW는 위 회사들과 제휴하거나 보증을 받은 프로젝트가 아닙니다. 설계 비교([ui-design.md](ui-design.md))는 공개된 사용 경험을 바탕으로 한
일반적인 평가이고, 해당 제품의 코드나 그림을 사용하지 않았습니다.

## 기여하기
기여자는 자신의 기여가 GPL-3.0-or-later로 배포되는 데 동의합니다. 기여 전에 `docs/dev/coding-rules.md`와 `docs/dev/adding-features.md`를 읽어 주세요.
