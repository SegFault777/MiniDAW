# 플러그인

MiniDAW는 두 종류의 플러그인을 씁니다.

## MiniDAW 플러그인 (`.dll`)
플러그인 SDK(`plugin-sdk/`)로 만든 작은 이펙트입니다. 설치 폴더에는 예제 두 개(Tremolo, Bitcrusher)가 들어 있습니다.
- 찾는 곳: `<설치 폴더>\plugins`, `%LOCALAPPDATA%\MiniDAW\plugins`
- 파일을 넣고 **Preferences > Rescan plug-ins**(또는 프로그램 재시작) 후 브라우저의 *Effects*에 나타납니다.
- 직접 만들고 싶다면 `plugin-sdk\README.md`를 보세요. C 언어 헤더 하나(`mdplugin.h`)만 있으면 됩니다.

## VST3 플러그인
- 기본 검색 폴더: `C:\Program Files\Common Files\VST3`, `%LOCALAPPDATA%\Programs\Common\VST3`
- 다른 폴더를 쓰려면 **Preferences > VST3 folders**에 `;`로 구분해 적습니다.
- 검색은 백그라운드에서 진행됩니다. 끝나면 상태 표시줄에 "Plug-in scan finished."가 나옵니다.
- **이펙트**는 브라우저의 Effects에서 트랙에 놓고, **악기**는 Instruments에서 Software Instrument 트랙에 놓습니다.
- 플러그인 자체 화면은 Devices 탭 카드의 **E** 버튼(또는 인스펙터의 *Editor* 버튼)으로 엽니다.
- 플러그인의 상태(프리셋)는 프로젝트 파일에 저장됩니다.

> 이 설치본이 VST3 지원 없이 만들어졌다면 VST3 항목이 보이지 않고, 불러오기를 시도하면 안내 메시지가 나옵니다.

*VST는 Steinberg Media Technologies GmbH의 상표입니다.*

## 문제가 생기면
- 플러그인이 목록에 없다 → 폴더 경로와 64비트(x64) 플러그인인지 확인하고 *Rescan plug-ins*를 누르세요. 32비트 플러그인은 쓸 수 없습니다.
- 플러그인 때문에 프로그램이 멈춘다 → 그 플러그인을 폴더에서 빼고 다시 실행해 보세요.
