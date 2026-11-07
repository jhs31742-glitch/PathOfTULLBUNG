(function () {
    "use strict";

    // Path of TULLBUNG — Stage18B Node Asset Authority
    // Generation canvas: 1254x1254.
    // Photoshop final assembly / PrismaUI production canvas: 1505x1505.
    // 1505 is required so the common border properly surrounds background/glyph.
    // State variants use layer brightness; no separate active/inactive/uninvested PNGs.

    window.TULLNodeAssets = Object.freeze({
        schemaVersion: 1,
        generationCanvasPx: 1254,
        productionCanvasPx: 1505,
        root: "assets/nodes",

        borders: Object.freeze({
            normal: "borders/노드아이콘_0000s_0000_테두리_노말.png",
            master: "borders/노드아이콘_0000s_0001_테두리_마스터.png"
        }),

        backgrounds: Object.freeze({
            warrior: "backgrounds/노드아이콘_0010s_0000_배경_전사.png",
            hunter: "backgrounds/노드아이콘_0010s_0001_배경_사냥꾼.png",
            priest: "backgrounds/노드아이콘_0010s_0003_배경_사제.png",
            mage: "backgrounds/노드아이콘_0010s_0002_배경_마법사.png"
        }),

        classFamily: Object.freeze({
            "0":"warrior", "1":"warrior", "2":"warrior",
            "3":"hunter",  "4":"hunter",  "5":"hunter",
            "6":"priest",  "7":"priest",  "8":"priest",
            "9":"mage",   "10":"mage",   "11":"mage"
        }),

        placeholder: "icons/노드아이콘_0000_999_임시.png",

        glyphs: Object.freeze({
            "0": "icons/010_Warrior_Icons/노드아이콘_0009s_0002_warrior_전투호흡.png",
            "1": "icons/010_Warrior_Icons/노드아이콘_0009s_0004_warrior_전사의기력.png",
            "2": "icons/010_Warrior_Icons/노드아이콘_0009s_0001_warrior_철벽.png",
            "3": "icons/010_Warrior_Icons/노드아이콘_0009s_0005_warrior_아드레날린.png",
            "4": "icons/010_Warrior_Icons/노드아이콘_0009s_0003_warrior_전장의질주.png",
            "5": "icons/010_Warrior_Icons/노드아이콘_0009s_0000_warrior_전투병기_MASTER.png",
            "16": "icons/011_Berserker_Icons/노드아이콘_0008s_0005_01_양손무기숙련.png",
            "17": "icons/011_Berserker_Icons/노드아이콘_0008s_0004_02_혈격.png",
            "18": "icons/011_Berserker_Icons/노드아이콘_0008s_0003_03_혈쇄.png",
            "19": "icons/011_Berserker_Icons/노드아이콘_0008s_0002_04_투쟁심.png",
            "20": "icons/011_Berserker_Icons/노드아이콘_0008s_0001_05_생존욕구.png",
            "21": "icons/011_Berserker_Icons/노드아이콘_0008s_0000_06_들끓는피_MASTER.png",
            "32": "icons/012_Guardian_Icons/노드아이콘_0007s_0005_01_방패숙련.png",
            "33": "icons/012_Guardian_Icons/노드아이콘_0007s_0004_02_최선의방어.png",
            "34": "icons/012_Guardian_Icons/노드아이콘_0007s_0003_03_방패강타.png",
            "35": "icons/012_Guardian_Icons/노드아이콘_0007s_0002_04_망치와모루.png",
            "36": "icons/012_Guardian_Icons/노드아이콘_0007s_0001_05_철혈_MASTER_A.png",
            "48": "icons/020_Hunter_Icons/노드아이콘_0006s_0004_01_경갑숙련.png",
            "49": "icons/020_Hunter_Icons/노드아이콘_0006s_0003_02_독추적.png",
            "50": "icons/020_Hunter_Icons/노드아이콘_0006s_0002_03_전술적후퇴.png",
            "51": "icons/020_Hunter_Icons/노드아이콘_0006s_0001_04_사냥꾼의경계심.png",
            "52": "icons/020_Hunter_Icons/노드아이콘_0006s_0000_05_사냥꾼의호승심_MASTER.png",
            "64": "icons/021_Assassin_Icons/노드아이콘_0005s_0005_01_단검숙련.png",
            "65": "icons/021_Assassin_Icons/노드아이콘_0005s_0004_02_혈독술.png",
            "66": "icons/021_Assassin_Icons/노드아이콘_0005s_0003_03_혈독주입.png",
            "67": "icons/021_Assassin_Icons/노드아이콘_0005s_0002_04_조용한처리.png",
            "68": "icons/021_Assassin_Icons/노드아이콘_0005s_0001_05_혈독정제.png",
            "69": "icons/021_Assassin_Icons/노드아이콘_0005s_0000_06_상처후벼파기_MASTER.png",
            "80": "icons/022_Marksman_Icons/노드아이콘_0004s_0005_01_전면전.png",
            "81": "icons/022_Marksman_Icons/노드아이콘_0004s_0004_02_빠른손놀림.png",
            "82": "icons/022_Marksman_Icons/노드아이콘_0004s_0003_03_혈독배달.png",
            "83": "icons/022_Marksman_Icons/노드아이콘_0004s_0002_04_불씨확산.png",
            "84": "icons/022_Marksman_Icons/노드아이콘_0004s_0001_05_평정심.png",
            "85": "icons/022_Marksman_Icons/노드아이콘_0004s_0000_06_숨통끊기_MASTER.png",
            "96": "icons/030_Priest_Icons/노드아이콘_0003s_0004_02_치유술.png",
            "97": "icons/030_Priest_Icons/노드아이콘_0003s_0003_03_성전.png",
            "98": "icons/030_Priest_Icons/노드아이콘_0003s_0002_04_격앙.png",
            "99": "icons/030_Priest_Icons/노드아이콘_0003s_0001_05_집중치유.png",
            "100": "icons/030_Priest_Icons/노드아이콘_0003s_0000_06_끝없는기도_MASTER.png",
            "101": "icons/030_Priest_Icons/노드아이콘_0003s_0005_01_사제자세.png",
            "112": "icons/031_Paladin_Icons/노드아이콘_0002s_0004_01_철퇴숙련.png",
            "113": "icons/031_Paladin_Icons/노드아이콘_0002s_0003_02_성기사의맹세.png",
            "114": "icons/031_Paladin_Icons/노드아이콘_0002s_0002_03_신성타격.png",
            "115": "icons/031_Paladin_Icons/노드아이콘_0002s_0001_04_신앙의무게.png",
            "116": "icons/031_Paladin_Icons/노드아이콘_0002s_0000_05_심판의철퇴_MASTER.png",
            "128": "icons/032_Inquisitor_Icons/노드아이콘_0001s_0004_01_마물학연구.png",
            "129": "icons/032_Inquisitor_Icons/노드아이콘_0001s_0003_02_데이드릭유물연구.png",
            "130": "icons/032_Inquisitor_Icons/노드아이콘_0001s_0002_03_대마물방어술.png",
            "131": "icons/032_Inquisitor_Icons/노드아이콘_0001s_0001_04_신벌.png",
            "132": "icons/032_Inquisitor_Icons/노드아이콘_0001s_0000_05_대심문관_MASTER.png",
            "144": "icons/040_Mage_Icons/노드아이콘_0003s_0002_01_마력장벽.png",
            "145": "icons/040_Mage_Icons/노드아이콘_0003s_0001_02_마도학.png",
            "146": "icons/040_Mage_Icons/노드아이콘_0003s_0000_03_마력붕괴.png",
            "147": "icons/040_Mage_Icons/노드아이콘_0003s_0003_04_대마도학자_MASTER.png",
            "160": "icons/041_VoidMage_Icons/노드아이콘_0002s_0003_03_공허강타.png",
            "161": "icons/041_VoidMage_Icons/노드아이콘_0002s_0004_02_공허무구.png",
            "162": "icons/041_VoidMage_Icons/노드아이콘_0002s_0002_04_공허침식.png",
            "163": "icons/041_VoidMage_Icons/노드아이콘_0002s_0001_05_공허포식.png",
            "164": "icons/041_VoidMage_Icons/노드아이콘_0002s_0000_06_공허의그릇_MASTER.png",
            "165": "icons/041_VoidMage_Icons/노드아이콘_0002s_0005_01_공허.png",
            "176": "icons/042_SiegeMage_Icons/노드아이콘_0001s_0003_02_마법탄도학.png",
            "177": "icons/042_SiegeMage_Icons/노드아이콘_0001s_0002_03_중력족쇄.png",
            "178": "icons/042_SiegeMage_Icons/노드아이콘_0001s_0001_04_집중포격.png",
            "179": "icons/042_SiegeMage_Icons/노드아이콘_0001s_0000_05_마력환류.png",
            "180": "icons/042_SiegeMage_Icons/노드아이콘_0001s_0005_06_마도진지_MASTER.png",
            "181": "icons/042_SiegeMage_Icons/노드아이콘_0001s_0004_01_공성전개.png"
        }),

        unresolved: Object.freeze({}),

        candidates: Object.freeze({

            "36": Object.freeze([
                "icons/012_Guardian_Icons/노드아이콘_0007s_0001_05_철혈_MASTER_A.png",
                "icons/012_Guardian_Icons/노드아이콘_0007s_0000_05_철혈_MASTER_B.png"
            ])
        })
    });
})();

