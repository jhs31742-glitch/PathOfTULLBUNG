# Path of TULLBUNG 1.0

Skyrim Special Edition용 직업·노드·칭호 모드입니다. DLL 버전은 1.0.0입니다.

## 설치 및 실행

1. 이 폴더를 MO2에 모드 하나로 설치합니다. 기존 Dev0.9와 1.0은 같은 ESP와 DLL 이름을 사용하므로 1.0을 사용할 때는 한 버전만 활성화합니다.
2. Skyrim 런타임에 맞는 SKSE, Address Library, PrismaUI를 준비하고 SKSE로 실행합니다. 이번 릴리스의 실게임 QA 기준은 Skyrim SE 1.5.97입니다. AE 1.6.x의 실게임 호환성은 확인 중입니다.
3. 게임에서 **H**로 POT 메뉴를 엽니다. **Esc**로 현재 팝업이나 편집 화면을 닫거나 POT 메뉴에서 나옵니다. 기존 설치의 단축키 설정은 유지됩니다. 이전 Dev0.9 QA에서 사용한 **F6**는 사용자 지정 단축키였습니다.

## 기능

- 조용한 처리 HUD에는 왼쪽 위로 향한 곧은 단검과 기존 혈독 노드의 자홍빛 보라색을 적용했습니다. 세 방울은 은신 무기 직접타격으로 처치한 뒤 다음 혈독이 즉시 3단계로 적용되는 준비 상태를 표시합니다. 아이콘은 기존과 같은 1505×1505 투명 PNG입니다.

- 직업과 노드에 JP를 사용해 성장하고, 칭호를 관리하거나 캐릭터 주변에 표시합니다.
- POT 메인 메뉴, 칭호 편집, HUD 편집 중 게임 진행을 일시정지하며 메뉴를 닫으면 재개합니다.
- 테마, 폰트, UI 투명도, HUD 배치 및 표시를 조정할 수 있습니다.
- 설정 저장·복원에서 내부 프로필을 저장하거나 외부 JSON을 가져오고 내보냅니다. 일반 프로필 불러오기는 확인 후 커스텀 칭호와 즐겨찾기에 반영됩니다.
- 내부 기본값은 모드 폴더의 SKSE/Plugins/PathOfTULLBUNG/POT_Deployment_Default.json에서 불러옵니다. 기본값을 적용하면 개발자 모드가 꺼집니다. 외부 JSON은 Windows 문서의 My Games/Skyrim Special Edition/PathOfTULLBUNG/UIProfiles에 둡니다.

## H가 작동하지 않을 때

게임을 종료한 뒤 실제로 적용되는 **Data/SKSE/Plugins/PathOfTULLBUNG_UI.ini**를 확인합니다. MO2에서는 오른쪽 **Data → SKSE → Plugins**에서 파일의 제공 위치를 찾습니다. 첫 실행에서 만들어진 설정은 **Overwrite/SKSE/Plugins/PathOfTULLBUNG_UI.ini**에 있을 수 있습니다. 별도 모드가 이 파일을 제공하면 그 모드의 파일을 편집합니다.

기존 파일을 복사해 둔 뒤 아래 네 항목을 수정하고 다시 실행합니다.

    menuKey=35
    menuShift=0
    menuCtrl=0
    menuAlt=0

35는 기본 H의 스캔 코드입니다. F6를 원하면 menuKey=64로 설정합니다. UI를 열 수 있다면 설정 → UI / 입력 → 일반에서 변경할 수 있습니다.

## 서체와 소스

- 마비옛체: https://mabinogi.nexon.com/page/pds/font.asp
- 넥슨 워헤이븐체: https://brand.nexon.com/ko/ci-brand-guidelines/typeface#section-warhaven
- 던파 비트비트체 v2 및 연단된 칼날: https://df.nexon.com/data/font
- 동봉 서체의 저작권 및 배포 조건: FONT_LICENSES.txt
- GitHub 저장소: https://github.com/jhs31742-glitch/PathOfTULLBUNG

## 소스 빌드
CMake에는 기존 개발 PC의 CommonLibSSE-NG 및 vcpkg 경로가 기록되어 있습니다. 다른 환경에서 빌드하려면 해당 의존성 위치를 맞추십시오.
