# 릴리스 절차

## 1. 준비
- [ ] 이번 버전에 넣을 것이 모두 들어갔다 (`CHANGELOG.md`의 "Unreleased" 성격의 항목이 정리됐다)
- [ ] `bash tests/run.sh` 통과
- [ ] 마지막 CI(`main`)의 세 작업(`tests`, `native`, `qt`)이 초록색이다
- [ ] 새 영어 문구가 네 언어 `translations/*.tsv`에 있다
- [ ] 문서(`docs/user`, `docs/dev`)가 바뀐 동작과 맞다

## 2. 버전 올리기
모든 곳을 한 번에 바꿉니다 (`docs/dev/coding-rules.md`의 목록):
`src/main.cpp`, `src/MainWindow.cpp`, `src/native/Win32App.cpp`, `build.sh`, `.github/workflows/build.yml`, `installer/MiniDAW.iss`, `installer/MiniDAW-Qt.iss`, `README.md`.
그리고 `CHANGELOG.md`에 `## [버전] - 날짜` 항목을 추가합니다.

## 3. 태그
```
git commit -am "Release 0.11"
git tag v0.11
git push && git push origin v0.11
```
핫픽스는 `v0.11_1`처럼 밑줄 번호를 붙입니다. 태그를 푸시하면 CI가 설치 프로그램과 포터블 zip을 만들어 릴리스에 붙입니다.

## 4. 릴리스 후 점검
- [ ] 설치 프로그램을 **깨끗한 PC(또는 VM)** 에 설치해 실행한다
- [ ] 이전 버전 위에 설치해(업그레이드) 설정과 프로젝트가 살아 있는지 본다
- [ ] 프로젝트를 저장하고 다시 열어 본다, 녹음해 본다, WAV로 내보내 본다
- [ ] 플러그인 하나를 불러 본다 (VST3가 켜진 빌드인지는 빌드 로그의 `VST3 hosting:` 줄로 확인)
- [ ] 제거하면 사용자 데이터(`%LOCALAPPDATA%\MiniDAW`)가 남는지 확인

## 5. 알려진 문제 기록
릴리스 노트에 이번 버전의 알려진 문제를 적습니다(예: "OGG 파일은 Windows 기본 코덱으로 열리지 않을 수 있음").
