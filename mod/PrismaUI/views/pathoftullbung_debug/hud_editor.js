(() => {
    'use strict';

    if (window.TULLHudEditorProductionR1) {
        return;
    }

    const definitions = [
        {
            key:'void',
            prefix:'hudVoid',
            label:'공허 HUD',
            defaultValue:{
                visible:true,
                x:0.06,
                y:0.76,
                scale:1,
                opacity:1
            }
        },
        {
            key:'composure',
            prefix:'hudComposure',
            label:'평정심 HUD',
            defaultValue:{
                visible:true,
                x:0.06,
                y:0.63,
                scale:1,
                opacity:1
            }
        },
        {
            key:'quiet',
            prefix:'hudQuiet',
            label:'조용한 처리 HUD',
            defaultValue:{
                visible:true,
                x:0.06,
                y:0.50,
                scale:1,
                opacity:1
            }
        }
    ];

    /*
        HUD preset slot count = B2 implementation detail.
        It is not promoted to gameplay/design authority.
    */
    const presetKey =
        slot =>
            `tull.hudEditor.preset.${slot}.v1`;

    // UI selection metadata only; payload and v1 slot keys are unchanged.
    const selectedPresetKey = 'tull.hudEditor.selectedPreset.v1';

    function readSelectedPreset() {
        try {
            const raw = Number(localStorage.getItem(selectedPresetKey));
            return Number.isInteger(raw) && raw >= 1 && raw <= 3 ? raw : 1;
        } catch (_) {
            return 1;
        }
    }

    let selectedPresetSlot = readSelectedPreset();

    function selectPresetSlot(slot) {
        if (!Number.isInteger(slot) || slot < 1 || slot > 3) return false;
        selectedPresetSlot = slot;
        try { localStorage.setItem(selectedPresetKey, String(slot)); }
        catch (_) { /* visual selection still works without storage */ }
        refreshToolbar();
        return true;
    }

    let editorOpen = false;

    /*
        Lock affects transform only.
        Visible / Opacity remain editable.
    */
    let transformLocked = false;

    let selectedKey = null;
    let working = null;
    let wheelTimer = null;
    let drag = null;

    /*
        Existing production Settings renderer remains authority.
        HUD Editor wraps it only while the dedicated sub-page is open.
    */
    const previousRenderSettings =
        renderSettings;


    function clamp(
        value,
        min,
        max,
        fallback=min)
    {
        const number =
            Number(value);

        if (
            !Number.isFinite(
                number)
        ) {
            return fallback;
        }

        return Math.max(
            min,
            Math.min(
                max,
                number));
    }


    function clone(value)
    {
        return JSON.parse(
            JSON.stringify(
                value));
    }


    /*
        Native settings snapshot -> editor working copy.
        Ownership / gameplay state is intentionally not read here.
        All 3 HUDs always exist in editor.
    */
    function currentSnapshot()
    {
        if (!settingsState) {
            return null;
        }

        const hud = {};

        for (
            const def of
            definitions
        ) {
            const prefix =
                def.prefix;

            hud[def.key] = {
                visible:
                    !!settingsState[
                        `${prefix}Visible`],

                x:
                    clamp(
                        settingsState[
                            `${prefix}X`],
                        0,
                        1,
                        def.defaultValue.x),

                y:
                    clamp(
                        settingsState[
                            `${prefix}Y`],
                        0,
                        1,
                        def.defaultValue.y),

                scale:
                    clamp(
                        settingsState[
                            `${prefix}Scale`],
                        0.5,
                        2,
                        1),

                opacity:
                    clamp(
                        settingsState[
                            `${prefix}Opacity`],
                        0,
                        1,
                        1)
            };
        }

        return {
            version:1,

            masterVisible:
                !!settingsState.hudMaster,

            hud
        };
    }


    function syncWorkingFromNative()
    {
        const snapshot =
            currentSnapshot();

        if (snapshot) {
            working =
                snapshot;
        }

        return snapshot;
    }


    function itemState(key)
    {
        return (
            working?.
                hud?.
                [key] ||
            null
        );
    }


    function itemDefinition(key)
    {
        return (
            definitions.find(
                def =>
                    def.key ===
                    key) ||
            null
        );
    }


    function sendField(
        def,
        field,
        value)
    {
        if (
            typeof sendSetting !==
            'function'
        ) {
            return;
        }

        sendSetting(
            `${def.prefix}${field}`,
            String(value));
    }


    /*
        Only commit on drag end / wheel debounce /
        explicit context control changes.
        Pointer movement itself is UI-only.
    */
    function commitItem(
        key,
        fields)
    {
        const def =
            itemDefinition(
                key);

        const state =
            itemState(
                key);

        if (
            !def ||
            !state
        ) {
            return;
        }

        for (
            const field of
            fields
        ) {
            if (
                field ===
                'Visible'
            ) {
                sendField(
                    def,
                    field,
                    state.visible ?
                        '1' :
                        '0');

                continue;
            }

            const property =
                field.toLowerCase();

            sendField(
                def,
                field,
                Number(
                    state[property]
                ).toFixed(2));
        }
    }


    function commitAll(snapshot)
    {
        if (!snapshot) {
            return;
        }

        sendSetting(
            'hudMaster',
            snapshot.masterVisible ?
                '1' :
                '0');

        for (
            const def of
            definitions
        ) {
            const state =
                snapshot.hud[
                    def.key];

            if (!state) {
                continue;
            }

            sendField(
                def,
                'Visible',
                state.visible ?
                    '1' :
                    '0');

            sendField(
                def,
                'X',
                Number(
                    state.x
                ).toFixed(2));

            sendField(
                def,
                'Y',
                Number(
                    state.y
                ).toFixed(2));

            sendField(
                def,
                'Scale',
                Number(
                    state.scale
                ).toFixed(2));

            sendField(
                def,
                'Opacity',
                Number(
                    state.opacity
                ).toFixed(2));
        }
    }


    function validPreset(value)
    {
        if (
            !value ||
            value.version !== 1 ||
            !value.hud
        ) {
            return false;
        }

        return definitions.every(
            def => {

                const state =
                    value.hud[
                        def.key];

                return (
                    state &&
                    typeof state.visible ===
                        'boolean' &&
                    Number.isFinite(
                        Number(
                            state.x)) &&
                    Number.isFinite(
                        Number(
                            state.y)) &&
                    Number.isFinite(
                        Number(
                            state.scale)) &&
                    Number.isFinite(
                        Number(
                            state.opacity))
                );
            });
    }


    function savePreset(slot)
    {
        if (!working) {
            return false;
        }

        try {
            localStorage.setItem(
                presetKey(slot),
                JSON.stringify(
                    working));

            selectPresetSlot(slot);

            return true;
        }
        catch (_) {
            return false;
        }
    }


    function loadPreset(slot)
    {
        try {
            const raw =
                localStorage.getItem(
                    presetKey(slot));

            if (raw === null) {
                return false;
            }

            const parsed =
                JSON.parse(raw);

            if (
                !validPreset(
                    parsed)
            ) {
                return false;
            }

            working = {
                version:1,
                masterVisible:
                    !!parsed.masterVisible,
                hud:{}
            };

            for (
                const def of
                definitions
            ) {
                const source =
                    parsed.hud[
                        def.key];

                working.hud[
                    def.key] = {

                    visible:
                        !!source.visible,

                    x:
                        clamp(
                            source.x,
                            0,
                            1,
                            def.defaultValue.x),

                    y:
                        clamp(
                            source.y,
                            0,
                            1,
                            def.defaultValue.y),

                    scale:
                        clamp(
                            source.scale,
                            0.5,
                            2,
                            1),

                    opacity:
                        clamp(
                            source.opacity,
                            0,
                            1,
                            1)
                };
            }

            commitAll(
                working);

            selectPresetSlot(slot);
            renderEditorPage();

            return true;
        }
        catch (_) {
            return false;
        }
    }


    function resetItem(key)
    {
        const def =
            itemDefinition(
                key);

        if (
            !def ||
            !working
        ) {
            return;
        }

        working.hud[key] =
            clone(
                def.defaultValue);

        commitItem(
            key,
            [
                'Visible',
                'X',
                'Y',
                'Scale',
                'Opacity'
            ]);

        renderEditorPage();
    }


    function resetAll()
    {
        if (!working) {
            return;
        }

        working.masterVisible =
            true;

        for (
            const def of
            definitions
        ) {
            working.hud[
                def.key] =
                    clone(
                        def.defaultValue);
        }

        commitAll(
            working);

        renderEditorPage();
    }


    /*
        Real production HUD assets.
        Editor does not invent alternate visual assets.
    */
    function previewMarkup(def)
    {
        if (
            def.key ===
            'void'
        ) {
            return `
                <div class="tull-hud-editor-void-cells">
                    ${Array.from({length:4},(_,index)=>`
                        <span class="tull-hud-editor-void-slot">
                            <img class="tull-hud-editor-void-fill"
                                 src="../pathoftullbung_hud/assets/hud/void_filled.png"
                                 style="display:${index < 2 ? 'block' : 'none'}" alt="">
                            <img class="tull-hud-editor-void-frame"
                                 src="../pathoftullbung_hud/assets/hud/void_empty.png" alt="">
                        </span>`).join('')}
                </div>`;
        }

        const file =
            def.key ===
                'composure' ?
                'composure.png' :
                'quiet_processing.png';

        return `
            <img class="tull-hud-editor-image"
                 src="../pathoftullbung_hud/assets/hud/${file}"
                 alt="">`;
    }


    function editorStyles()
    {
        if (
            document.getElementById(
                'S18_HUD_EDITOR_PRODUCTION_R1_STYLE')
        ) {
            return;
        }

        const style =
            document.createElement(
                'style');

        style.id =
            'S18_HUD_EDITOR_PRODUCTION_R1_STYLE';

        style.textContent = `
#tullHudEditorViewport{
    position:relative;
    width:100%;
    max-height:68vh;
    min-height:360px;
    overflow:hidden;
    border:1px solid var(--frame-line);
    background:
        radial-gradient(
            circle at 50% 42%,
            rgba(76,83,74,.25),
            transparent 52%),
        linear-gradient(
            180deg,
            rgba(26,31,34,.95),
            rgba(10,13,16,.97));
    box-shadow:
        inset 0 0 60px rgba(0,0,0,.42);
    touch-action:none;
    user-select:none
}

.tull-hud-editor-screen-label{
    position:absolute;
    top:8px;
    left:10px;
    z-index:5;
    color:rgba(245,238,214,.62);
    font-size:10px;
    pointer-events:none
}

.tull-hud-editor-item{
    position:absolute;
    left:0;
    top:0;
    z-index:10;
    transform-origin:top left;
    cursor:move;
    touch-action:none;
    user-select:none
}

.tull-hud-editor-item.locked{
    cursor:default
}

.tull-hud-editor-item.selected{
    outline:2px solid var(--gold);
    outline-offset:4px
}

.tull-hud-editor-item.off{
    opacity:.32!important;
    filter:grayscale(.35)
}

.tull-hud-editor-item.master-off{
    opacity:.26!important
}

.tull-hud-editor-tag{
    position:absolute;
    left:0;
    bottom:calc(100% + 5px);
    padding:2px 5px;
    white-space:nowrap;
    border:
        1px solid
        rgba(230,205,147,.52);
    background:
        rgba(16,14,10,.88);
    color:var(--paper2);
    font-size:9px;
    pointer-events:none
}

.tull-hud-editor-off-x{
    position:absolute;
    inset:-8px;
    display:none;
    pointer-events:none
}

.tull-hud-editor-item.off
.tull-hud-editor-off-x{
    display:block
}

.tull-hud-editor-off-x::before,
.tull-hud-editor-off-x::after{
    content:"";
    position:absolute;
    left:50%;
    top:-5%;
    width:4px;
    height:110%;
    background:
        rgba(200,52,42,.92);
    box-shadow:
        0 0 5px
        rgba(0,0,0,.7)
}

.tull-hud-editor-off-x::before{
    transform:
        translateX(-50%)
        rotate(45deg)
}

.tull-hud-editor-off-x::after{
    transform:
        translateX(-50%)
        rotate(-45deg)
}

.tull-hud-editor-image{
    display:block;
    width:96px;
    height:96px;
    object-fit:contain;
    pointer-events:none
}

.tull-hud-editor-void-cells{
    display:flex;
    align-items:center;
    gap:0;
    pointer-events:none
}

.tull-hud-editor-void-slot{
    position:relative;
    display:block;
    flex:none;
    width:64px;
    height:64px
}
.tull-hud-editor-void-slot img{
    position:absolute;
    display:block;
    inset:0;
    width:100%;
    height:100%;
    object-fit:contain
}
.tull-hud-editor-void-fill{z-index:1}
.tull-hud-editor-void-frame{z-index:2}

.tull-hud-editor-toolbar{
    display:flex;
    flex-wrap:wrap;
    align-items:center;
    gap:7px;
    margin:10px 0 12px
}

.tull-hud-editor-toolbar button,
.tull-hud-editor-toolbar select{
    min-height:36px;
    padding:6px 11px;
    border:1px solid var(--card-line);
    border-radius:3px;
    background:var(--card-bg);
    color:var(--ink);
    font:inherit;
    font-size:14px;
    cursor:pointer
}

.tull-hud-editor-toolbar button:hover,
.tull-hud-editor-toolbar select:hover{
    border-color:var(--gold);
    background:var(--card-hover-bg)
}

.tull-hud-editor-toolbar button.active{
    border-color:var(--gold);
    background:var(--card-hover-bg)
}
.tull-hud-preset-buttons{
    display:inline-flex;gap:5px;align-items:center
}
.tull-hud-preset-buttons button{
    min-width:37px;font-weight:700
}
.tull-hud-preset-buttons button[aria-pressed="true"]{
    border:2px solid var(--gold);
    background:var(--card-hover-bg)
}

.tull-hud-editor-context{
    position:absolute;
    z-index:60;
    width:260px;
    padding:12px;
    border:1px solid var(--card-line);
    border-radius:3px;
    background:var(--paper2);
    color:var(--ink);
    font-size:14px;
    box-shadow:
        0 12px 36px
        rgba(0,0,0,.42)
}

.tull-hud-editor-context strong{
    display:block;
    margin-bottom:8px
}

.tull-hud-editor-context-row{
    display:flex;
    align-items:center;
    justify-content:space-between;
    gap:10px;
    margin-top:8px
}

.tull-hud-editor-context
input[type="range"]{
    min-width:110px
}

.tull-hud-editor-context button{
    width:100%;
    min-height:36px;
    margin-top:10px;
    padding:6px 10px;
    border:1px solid var(--card-line);
    border-radius:3px;
    background:var(--card-bg);
    color:var(--ink);
    font:inherit;
    cursor:pointer
}

.tull-hud-editor-context button:hover{
    border-color:var(--gold);
    background:var(--card-hover-bg)
}

.tull-hud-editor-context input[type="range"]{
    accent-color:var(--gold)
}

.tull-hud-editor-help{
    margin-top:8px;
    color:var(--muted);
    font-size:13px;
    line-height:1.55
}

.tull-hud-editor-master-off{
    position:absolute;
    top:8px;
    right:10px;
    z-index:5;
    padding:3px 6px;
    border:
        1px solid
        rgba(200,52,42,.7);
    background:
        rgba(60,12,10,.72);
    color:#f2a39c;
    font-size:10px;
    pointer-events:none
}
`;

        style.textContent += `
/* S18 R1-02 — game-scene HUD editor, no opaque mock viewport */
body.tull-hud-game-edit #shell{display:none!important}
#tullHudEditorGameOverlay{
    position:fixed;inset:0;width:100vw;height:100vh;
    z-index:2147483000;background:transparent!important;
    overflow:hidden;box-sizing:border-box
}
#tullHudEditorGameOverlay #tullHudEditorViewport{
    position:absolute!important;inset:0!important;
    width:100%!important;height:100%!important;
    min-height:0!important;max-height:none!important;
    box-sizing:border-box;border:0!important;
    aspect-ratio:auto!important;background:transparent!important;
    box-shadow:none!important
}
#tullHudEditorGameOverlay .tull-hud-game-toolbar{
    position:absolute;z-index:90;top:10px;left:50%;
    transform:translateX(-50%);
    width:min(94vw,1100px);max-height:28vh;
    overflow-y:auto;box-sizing:border-box;
    padding:9px 12px;border:1px solid var(--frame-line);
    border-radius:5px;background:var(--paper2);color:var(--ink);
    box-shadow:0 8px 24px rgba(0,0,0,.38)
}
#tullHudEditorGameOverlay .tull-hud-game-toolbar .tull-hud-editor-toolbar{
    margin:0;color:var(--ink)
}
#tullHudEditorGameOverlay .tull-hud-game-toolbar .tull-hud-editor-help{
    color:var(--muted)
}
#tullHudEditorGameOverlay .tull-hud-game-done{
    min-height:34px;padding:6px 12px;margin-left:auto;
    cursor:pointer;border:1px solid var(--gold);
    background:var(--paper3);color:var(--ink);font:inherit
}
#tullHudEditorGameOverlay .tull-hud-editor-screen-label{
    top:auto;bottom:8px;padding:3px 6px;
    background:rgba(0,0,0,.55);color:#f5eed6
}
#tullHudEditorGameOverlay[data-hud-theme="dark-gold"] .tull-hud-editor-tag{
    background:#181914;color:#eee4cb;
    -webkit-text-fill-color:#eee4cb;border-color:#d5b25e
}
`;

        document.head.append(
            style);
    }


    // Move existing controls/viewport outside the opaque Settings shell.
    // Normalized X/Y and Native settings remain the only authority.
    function mountGameplayOverlay()
    {
        if (!editorOpen) return;

        const center = document.getElementById('centerPanel');
        const viewport = center?.querySelector('#tullHudEditorViewport');
        const toolbar = center?.querySelector('.tull-hud-editor-toolbar');
        const help = center?.querySelector('.tull-hud-editor-help');
        if (!viewport || !toolbar || !help) return;

        const overlay = document.createElement('div');
        overlay.id = 'tullHudEditorGameOverlay';
        overlay.dataset.hudTheme = settingsState?.themeId === 'dark-gold' ? 'dark-gold' : '';
        overlay.setAttribute('role', 'region');
        overlay.setAttribute('aria-label','게임 화면 HUD 편집');

        const controls = document.createElement('div');
        controls.className = 'tull-hud-game-toolbar';
        const done = document.createElement('button');
        done.type = 'button';
        done.className = 'tull-hud-game-done';
        done.textContent = '편집 종료';
        done.addEventListener('click', closeEditor);

        controls.append(toolbar, done, help);
        overlay.append(viewport, controls);
        document.body.append(overlay);
        document.body.classList.add('tull-hud-game-edit');
        positionContextPanel();
    }


    function refreshToolbar()
    {
        const load = document.getElementById('tullHudEditorPresetLoad');
        if (!load) return;

        document.querySelectorAll('[data-tull-hud-preset]').forEach(button => {
            const slot = Number(button.dataset.tullHudPreset);
            const active = slot === selectedPresetSlot;
            button.classList.toggle('active', active);
            button.setAttribute('aria-pressed', String(active));
        });

        try {
            load.disabled = localStorage.getItem(presetKey(selectedPresetSlot)) === null;
        } catch (_) {
            load.disabled = true;
        }
    }


    function updateItemElement(
        element,
        key)
    {
        const state =
            itemState(
                key);

        if (
            !element ||
            !state
        ) {
            return;
        }

        element.style.left =
            `${state.x * 100}%`;

        element.style.top =
            `${state.y * 100}%`;

        element.style.transform =
            `scale(${state.scale})`;

        /*
            Edit screen keeps all HUDs visible.
            Native opacity is still represented,
            with a minimum editor visibility floor.
        */
        element.style.opacity =
            String(
                Math.max(
                    state.opacity,
                    0.24));

        element.classList.toggle(
            'off',
            !state.visible);

        element.classList.toggle(
            'master-off',
            !working.masterVisible);

        element.classList.toggle(
            'locked',
            transformLocked);

        element.classList.toggle(
            'selected',
            selectedKey ===
                key);
    }


    function refreshItems()
    {
        for (
            const def of
            definitions
        ) {
            updateItemElement(
                document.querySelector(
                    `[data-tull-hud-editor-item="${def.key}"]`),
                def.key);
        }

        const masterNotice =
            document.getElementById(
                'tullHudEditorMasterOff');

        if (masterNotice) {
            masterNotice.hidden =
                !!working?.
                    masterVisible;
        }
    }


    function contextPanelMarkup()
    {
        const def =
            itemDefinition(
                selectedKey);

        const state =
            itemState(
                selectedKey);

        if (
            !def ||
            !state
        ) {
            return '';
        }

        return `
            <div id="tullHudEditorContext"
                 class="tull-hud-editor-context">

                <strong>
                    ${def.label}
                </strong>

                <div class="tull-hud-editor-context-row">
                    <label>
                        표시
                    </label>

                    <input id="tullHudEditorVisible"
                           type="checkbox"
                           ${state.visible ? 'checked' : ''}>
                </div>

                <div class="tull-hud-editor-context-row">
                    <label>
                        투명도
                    </label>

                    <input id="tullHudEditorOpacity"
                           type="range"
                           min="0"
                           max="1"
                           step="0.05"
                           value="${state.opacity}">

                    <span id="tullHudEditorOpacityValue">
                        ${Math.round(
                            state.opacity *
                            100)}%
                    </span>
                </div>

                <button id="tullHudEditorResetItem"
                        type="button">
                    이 HUD 초기화
                </button>
            </div>`;
    }


    function positionContextPanel()
    {
        const viewport =
            document.getElementById(
                'tullHudEditorViewport');

        const panel =
            document.getElementById(
                'tullHudEditorContext');

        const item =
            document.querySelector(
                `[data-tull-hud-editor-item="${selectedKey}"]`);

        if (
            !viewport ||
            !panel ||
            !item
        ) {
            return;
        }

        const viewportRect =
            viewport
                .getBoundingClientRect();

        const itemRect =
            item
                .getBoundingClientRect();

        const width =
            panel.offsetWidth ||
            220;

        const height =
            panel.offsetHeight ||
            150;

        let left =
            itemRect.right -
            viewportRect.left +
            8;

        let top =
            itemRect.top -
            viewportRect.top;

        left =
            Math.max(
                8,
                Math.min(
                    left,
                    viewportRect.width -
                    width -
                    8));

        top =
            Math.max(
                8,
                Math.min(
                    top,
                    viewportRect.height -
                    height -
                    8));

        panel.style.left =
            `${left}px`;

        panel.style.top =
            `${top}px`;
    }


    function wireContextPanel()
    {
        const def =
            itemDefinition(
                selectedKey);

        const state =
            itemState(
                selectedKey);

        if (
            !def ||
            !state
        ) {
            return;
        }

        const visible =
            document.getElementById(
                'tullHudEditorVisible');

        const opacity =
            document.getElementById(
                'tullHudEditorOpacity');

        const opacityValue =
            document.getElementById(
                'tullHudEditorOpacityValue');

        const reset =
            document.getElementById(
                'tullHudEditorResetItem');


        visible?.
            addEventListener(
                'change',
                () => {

                    state.visible =
                        visible.checked;

                    refreshItems();

                    sendField(
                        def,
                        'Visible',
                        state.visible ?
                            '1' :
                            '0');
                });


        opacity?.
            addEventListener(
                'input',
                () => {

                    state.opacity =
                        clamp(
                            opacity.value,
                            0,
                            1,
                            1);

                    if (opacityValue) {
                        opacityValue.textContent =
                            `${Math.round(
                                state.opacity *
                                100)}%`;
                    }

                    refreshItems();
                });


        opacity?.
            addEventListener(
                'change',
                () => {

                    sendField(
                        def,
                        'Opacity',
                        Number(
                            state.opacity
                        ).toFixed(2));
                });


        reset?.
            addEventListener(
                'click',
                () =>
                    resetItem(
                        def.key));


        positionContextPanel();
    }


    function renderEditorPage()
    {
        if (!editorOpen) {
            return;
        }

        // Re-renders must not leave a duplicate overlay/DOM IDs.
        document.body.classList.remove('tull-hud-game-edit');
        document.getElementById('tullHudEditorGameOverlay')?.remove();

        editorStyles();

        /*
            Editor-session working state remains authoritative while open.
            Native sync occurs when the editor is opened.
        */
        clearPanels();

        const left =
            document.getElementById(
                'leftPanel');

        const center =
            document.getElementById(
                'centerPanel');


        left.innerHTML = `
            <div class="sectionLabel">설정</div>
            <div id="tullHudEditorBack" class="leftItem">
                <div class="leftTitle">UI / 입력</div>
            </div>
            <div class="leftItem active" aria-current="page">
                <div class="leftTitle">HUD</div>
                <div class="small">통합 HUD 편집</div>
            </div>`;


        const viewportRatio =
            `${
                Math.max(
                    1,
                    window.innerWidth)
            } / ${
                Math.max(
                    1,
                    window.innerHeight)
            }`;


        center.innerHTML = `
            <h1>
                HUD 편집
            </h1>

            <div class="subtitle">
                게임 화면 기준 · 모든 HUD를
                획득 여부와 무관하게 편집합니다.
            </div>

            <div class="tull-hud-editor-toolbar">

                <label>
                    <input id="tullHudEditorMaster"
                           type="checkbox"
                           ${
                               working.masterVisible ?
                                   'checked' :
                                   ''
                           }>
                    전체 HUD 표시
                </label>

                <button id="tullHudEditorLock"
                        type="button"
                        class="${
                            transformLocked ?
                                'active' :
                                ''
                        }">
                    ${
                        transformLocked ?
                            '위치 잠금 ON' :
                            '위치 잠금 OFF'
                    }
                </button>

                <button id="tullHudEditorResetAll"
                        type="button">
                    ↺ 전체 초기화
                </button>

                <div class="tull-hud-preset-buttons"
                     role="group" aria-label="HUD 프리셋 선택">
                    <button type="button" data-tull-hud-preset="1" aria-pressed="false">1</button>
                    <button type="button" data-tull-hud-preset="2" aria-pressed="false">2</button>
                    <button type="button" data-tull-hud-preset="3" aria-pressed="false">3</button>
                </div>

                <button id="tullHudEditorPresetSave"
                        type="button">
                    프리셋 저장
                </button>

                <button id="tullHudEditorPresetLoad"
                        type="button">
                    프리셋 불러오기
                </button>
            </div>

            <div id="tullHudEditorViewport"
                 style="aspect-ratio:${viewportRatio}">

                <div class="tull-hud-editor-screen-label">
                    현재 UI viewport
                    ${window.innerWidth}
                    ×
                    ${window.innerHeight}
                </div>

                <div id="tullHudEditorMasterOff"
                     class="tull-hud-editor-master-off"
                     ${
                         working.masterVisible ?
                             'hidden' :
                             ''
                     }>
                    전체 HUD OFF · 편집 표시는 유지
                </div>

                ${
                    definitions
                        .map(
                            def => `
                                <div class="tull-hud-editor-item"
                                     data-tull-hud-editor-item="${def.key}">

                                    <span class="tull-hud-editor-tag">
                                        ${def.label}
                                    </span>

                                    ${previewMarkup(def)}

                                    <span class="tull-hud-editor-off-x">
                                    </span>
                                </div>`)
                        .join('')
                }

                ${contextPanelMarkup()}
            </div>

            <div class="tull-hud-editor-help">
                HUD를 드래그하여 이동합니다.
                마우스 휠로 크기를 조절합니다.
                HUD를 클릭하면 표시·투명도·개별 초기화 메뉴가 열립니다.
                표시 OFF HUD도 편집 화면에는 빨간 X ghost로 남습니다.
                위치 잠금은 이동·휠 크기 변경만 차단합니다.
            </div>`;


        document
            .getElementById(
                'tullHudEditorBack')?.
            addEventListener(
                'click',
                closeEditor);


        document
            .getElementById(
                'tullHudEditorMaster')?.
            addEventListener(
                'change',
                event => {

                    working.masterVisible =
                        event.target.checked;

                    refreshItems();

                    sendSetting(
                        'hudMaster',
                        working.masterVisible ?
                            '1' :
                            '0');
                });


        document
            .getElementById(
                'tullHudEditorLock')?.
            addEventListener(
                'click',
                () => {

                    transformLocked =
                        !transformLocked;

                    renderEditorPage();
                });


        document
            .getElementById(
                'tullHudEditorResetAll')?.
            addEventListener(
                'click',
                resetAll);


        document
            .getElementById(
                'tullHudEditorPresetSave')?.
            addEventListener(
                'click',
                () => {

                    savePreset(selectedPresetSlot);
                });


        document
            .getElementById(
                'tullHudEditorPresetLoad')?.
            addEventListener(
                'click',
                () => {

                    loadPreset(selectedPresetSlot);
                });


        document.querySelectorAll('[data-tull-hud-preset]').forEach(button => {
            button.addEventListener('click', () => {
                selectPresetSlot(Number(button.dataset.tullHudPreset));
            });
        });


        document
            .querySelectorAll(
                '[data-tull-hud-editor-item]')
            .forEach(
                element => {

                    const key =
                        element.dataset
                            .tullHudEditorItem;

                    updateItemElement(
                        element,
                        key);


                    element.addEventListener(
                        'click',
                        event => {

                            if (
                                drag?.
                                    moved
                            ) {
                                return;
                            }

                            event.stopPropagation();

                            selectedKey =
                                selectedKey ===
                                    key ?
                                    null :
                                    key;

                            renderEditorPage();
                        });


                    element.addEventListener(
                        'pointerdown',
                        event => {

                            if (
                                transformLocked ||
                                event.button !== 0
                            ) {
                                return;
                            }

                            if (
                                event.target.closest(
                                    '#tullHudEditorContext')
                            ) {
                                return;
                            }

                            const viewport =
                                document.getElementById(
                                    'tullHudEditorViewport');

                            const state =
                                itemState(
                                    key);

                            if (
                                !viewport ||
                                !state
                            ) {
                                return;
                            }

                            event.preventDefault();

                            const rect =
                                viewport
                                    .getBoundingClientRect();

                            drag = {
                                key,
                                pointerId:
                                    event.pointerId,
                                startX:
                                    event.clientX,
                                startY:
                                    event.clientY,
                                baseX:
                                    state.x,
                                baseY:
                                    state.y,
                                moved:false,
                                rect
                            };

                            element
                                .setPointerCapture(
                                    event.pointerId);
                        });


                    element.addEventListener(
                        'pointermove',
                        event => {

                            if (
                                !drag ||
                                drag.key !==
                                    key ||
                                drag.pointerId !==
                                    event.pointerId
                            ) {
                                return;
                            }

                            const state =
                                itemState(
                                    key);

                            if (!state) {
                                return;
                            }

                            const dx =
                                (
                                    event.clientX -
                                    drag.startX
                                ) /
                                drag.rect.width;

                            const dy =
                                (
                                    event.clientY -
                                    drag.startY
                                ) /
                                drag.rect.height;

                            if (
                                Math.abs(dx) >
                                    0.002 ||
                                Math.abs(dy) >
                                    0.002
                            ) {
                                drag.moved =
                                    true;
                            }

                            const widthRatio =
                                (
                                    element.offsetWidth *
                                    state.scale
                                ) /
                                drag.rect.width;

                            const heightRatio =
                                (
                                    element.offsetHeight *
                                    state.scale
                                ) /
                                drag.rect.height;

                            state.x =
                                clamp(
                                    drag.baseX + dx,
                                    0,
                                    Math.max(
                                        0,
                                        1 -
                                        widthRatio),
                                    0);

                            state.y =
                                clamp(
                                    drag.baseY + dy,
                                    0,
                                    Math.max(
                                        0,
                                        1 -
                                        heightRatio),
                                    0);

                            updateItemElement(
                                element,
                                key);

                            positionContextPanel();
                        });


                    const endDrag =
                        event => {

                            if (
                                !drag ||
                                drag.key !==
                                    key ||
                                drag.pointerId !==
                                    event.pointerId
                            ) {
                                return;
                            }

                            const moved =
                                drag.moved;

                            drag =
                                null;

                            try {
                                if (
                                    element
                                        .hasPointerCapture(
                                            event.pointerId)
                                ) {
                                    element
                                        .releasePointerCapture(
                                            event.pointerId);
                                }
                            }
                            catch (_) {
                            }

                            if (moved) {
                                commitItem(
                                    key,
                                    [
                                        'X',
                                        'Y'
                                    ]);
                            }
                        };


                    element.addEventListener(
                        'pointerup',
                        endDrag);

                    element.addEventListener(
                        'pointercancel',
                        endDrag);


                    element.addEventListener(
                        'wheel',
                        event => {

                            if (
                                transformLocked
                            ) {
                                return;
                            }

                            event.preventDefault();
                            event.stopPropagation();

                            const state =
                                itemState(
                                    key);

                            if (!state) {
                                return;
                            }

                            const step =
                                event.deltaY <
                                    0 ?
                                    0.05 :
                                    -0.05;

                            state.scale =
                                clamp(
                                    state.scale +
                                    step,
                                    0.5,
                                    2,
                                    1);

                            updateItemElement(
                                element,
                                key);

                            positionContextPanel();

                            if (
                                wheelTimer !==
                                null
                            ) {
                                window.clearTimeout(
                                    wheelTimer);
                            }

                            wheelTimer =
                                window.setTimeout(
                                    () => {

                                        wheelTimer =
                                            null;

                                        commitItem(
                                            key,
                                            ['Scale']);
                                    },
                                    180);
                        },
                        {
                            passive:false
                        });
                });


        document
            .getElementById(
                'tullHudEditorViewport')?.
            addEventListener(
                'click',
                event => {

                    if (
                        event.target.id ===
                        'tullHudEditorViewport'
                    ) {
                        selectedKey =
                            null;

                        renderEditorPage();
                    }
                });


        wireContextPanel();
        refreshItems();
        refreshToolbar();
        mountGameplayOverlay();
    }


    // Settings sidebar owns HUD navigation.
    // The legacy HUD card is not a second editor.
    function injectEntryButton()
    {
        const host = document.getElementById('hudLayoutControls');
        const left = document.getElementById('leftPanel');
        if (!host || !left) return;

        const oldCard = host.closest('.card');
        if (oldCard) oldCard.hidden = true;

        if (document.getElementById('tullHudEditorOpen')) return;

        const entry = document.createElement('div');
        entry.id = 'tullHudEditorOpen';
        entry.className = 'leftItem';
        entry.setAttribute('role','button');
        entry.tabIndex = 0;
        entry.innerHTML = `
            <div class="leftTitle">HUD 편집</div>
            <div class="small">통합 HUD 편집</div>`;

        const uiTab = Array.from(left.querySelectorAll('.leftItem'))
            .find(item => item.querySelector('.leftTitle')?.textContent.trim() === 'UI / 입력');

        if (uiTab) uiTab.insertAdjacentElement('afterend',entry);
        else left.append(entry);

        entry.addEventListener('click',openEditor);
        entry.addEventListener('keydown',event => {
            if (event.key === 'Enter' || event.key === ' ') {
                event.preventDefault();
                openEditor();
            }
        });
    }

    function openEditor()
    {
        if (!settingsState) {
            send(
                'settings.refresh');

            return;
        }

        editorOpen =
            true;

        send(
            'hud.editor.open');

        selectedKey =
            null;

        transformLocked =
            false;

        syncWorkingFromNative();

        renderEditorPage();
    }


    function closeEditor()
    {
        if (!editorOpen) return;
        document.body.classList.remove('tull-hud-game-edit');
        document.getElementById('tullHudEditorGameOverlay')?.remove();

        editorOpen =
            false;

        send(
            'hud.editor.close');

        selectedKey =
            null;

        drag =
            null;

        if (
            wheelTimer !==
            null
        ) {
            window.clearTimeout(
                wheelTimer);

            wheelTimer =
                null;
        }

        // Route through the outermost Settings owner (including theme tab).
        renderSettings();
    }


    renderSettings =
        function()
        {
            if (editorOpen) {
                renderEditorPage();
                return;
            }

            previousRenderSettings();
            injectEntryButton();
        };


    window.addEventListener(
        'resize',
        () => {

            if (editorOpen) {
                renderEditorPage();
            }
        });


    window.addEventListener(
        'keydown',
        event => {

            if (
                event.key ===
                    'Escape' &&
                editorOpen
            ) {
                event.preventDefault();
                event.stopImmediatePropagation();

                closeEditor();
            }
        },
        true);


    editorStyles();


    window.TULLProfileOwners.hud={presetDefaults:()=>({version:1,masterVisible:true,hud:Object.fromEntries(definitions.map(x=>[x.key,{...x.defaultValue}]))}),defaults:()=>({selectedPreset:'1',nativeHud:Object.fromEntries(definitions.map(x=>[x.key,{...x.defaultValue}]))}),capture:()=>({selectedPresetSlot,working:working?JSON.parse(JSON.stringify(working)):null,selectedKey,transformLocked}),busy:()=>editorOpen||!!drag||!!wheelTimer,
      apply:()=>{selectedPresetSlot=readSelectedPreset();},
      restore:s=>{selectedPresetSlot=s.selectedPresetSlot;working=s.working;selectedKey=s.selectedKey;transformLocked=s.transformLocked;},
      verify:()=>selectedPresetSlot===readSelectedPreset()};
    window.TULLHudEditorProductionR1 =
        Object.freeze({
            open:
                openEditor,

            close:
                closeEditor,

            isOpen:
                () =>
                    editorOpen
        });
})();