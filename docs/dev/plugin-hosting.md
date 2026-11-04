# 플러그인 호스팅

## MiniDAW 플러그인 (`src/platform/PluginHost.*`, `src/dsp/NativeEffect.*`, `plugin-sdk/`)
- 플러그인은 `plugin-sdk/mdplugin.h`의 C API를 구현한 공유 라이브러리입니다(`mdplugin_count`, `mdplugin_get`).
- `scanNativePlugins(folders)`가 폴더의 `.dll/.so/.dylib`를 열고 설명자(`MdEffectDesc`)를 검사해(`dsp::validDescriptor`) 목록에 넣습니다.
  이미 연 파일은 다시 열지 않고, 쓰이는 라이브러리는 프로그램이 끝날 때까지 열어 둡니다(설명자 포인터가 유효해야 하므로).
- 프로젝트에는 `native:<플러그인 id>`로 저장되고, `createNativePlugin(id)`가 `dsp::NativeEffect`로 감싼 `dsp::Effect`를 돌려줍니다.
- 테스트(`platform_tests.cpp`)는 예제를 공유 라이브러리로 컴파일해 폴더를 검색하고 오디오를 처리해 봅니다. **Linux에서도 전부 검증됩니다.**
- 네이티브 앱은 시작할 때 `<설치 폴더>\plugins`와 `%LOCALAPPDATA%\MiniDAW\plugins`를 **동기로** 검색합니다
  (프로젝트를 불러올 때 곧바로 필요할 수 있으므로).

## VST3 (`src/native/Vst3Native.*`)
- Steinberg VST3 SDK의 호스팅 도우미(`Module`, `hostclasses`)를 씁니다. SDK는 저장소에 없고 `VST3_SDK_DIR`로 가리킵니다.
- 헤더(`Vst3Native.h`)에는 SDK 타입이 없습니다. `MINIDAW_VST3`가 없으면 모두 인라인 빈 함수여서 나머지 코드는 항상 컴파일됩니다.
- API: `scan`, `createEffect`, `createInstrument`, `openEditor`, `saveState`, `loadState`, `isVst3`.
- 프로젝트에는 `vst3:<모듈 경로>|<클래스 id>`로 저장되고, 플러그인 상태는 base64 문자열로 함께 저장됩니다.
- **편집 창**: `openEditor`가 일반 Win32 창을 만들고 그 `HWND`에 `IPlugView`를 붙입니다. `IPlugFrame::resizeView`는 창 크기를 맞춥니다.
  플러그인당 창은 하나(이미 열려 있으면 앞으로 가져옵니다). 창을 닫으면 `removed()`와 `setFrame(nullptr)` 뒤에 뷰를 놓습니다.
- 이 파일은 **Windows와 SDK가 있어야 컴파일**되므로 Linux 테스트로는 검증되지 않습니다. CI 로그가 유일한 확인 수단이고,
  빌드가 실패해도 `build.sh`가 VST3 없이 다시 빌드합니다.
- 오디오 스레드에서 플러그인 `process`를 부르므로 플러그인 호출에 UI 객체를 섞지 않습니다.

## 앱 쪽 연결 (`Win32App.cpp`)
`Session::Hooks`의 `makeEffect`, `makeInstrument`, `saveState`, `loadState`가 위 호스트들로 이어집니다.
UI는 `Host::plugins`(목록), `Host::rescanPlugins`, `Host::openPluginEditor`로만 접근하므로 `src/ui`는 플러그인 SDK를 몰라도 됩니다.
VST3 검색은 느릴 수 있어 별도 스레드에서 돌고, 끝나면 `WM_APP + 2`로 알립니다.
