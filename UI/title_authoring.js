const POT_TITLE_MAX_CHARS = 12;

function potTitleChars(text) {
    return Array.from(text.normalize('NFC'));
}

function potTitleWriteAllowed(field, value, remaining = POT_TITLE_MAX_CHARS) {
    const count = potTitleChars(value).length;
    return count <= remaining || count < potTitleChars(field.value).length;
}

function potLimitTitleInput(field, remaining = POT_TITLE_MAX_CHARS) {
    const chars = potTitleChars(field.value);
    if (chars.length <= remaining) return;
    const caret = field.selectionStart;
    field.value = chars.slice(0, remaining).join('');
    if (caret !== null) {
        const position = Math.min(caret, field.value.length);
        field.setSelectionRange(position, position);
    }
}

(() => {
    'use strict';

    if(window.TULLTitleAuthoringProductionR1) {
        return;
    }

    const doc=document;
    const win=window;

    const titleKey=
        'tull.titleGrid.working.v1';

    const presetKey=n=>
        `tull.titleGrid.preset.${n}.v1`;

    const ids=
        Array.from(
            {length:6},
            (_,i)=>
                `BLOCK_TITLE_${String(i+1).padStart(2,'0')}`);

    const roles=[
        'PREFIX',
        'NAME_TEMPLATE',
        'SUFFIX',
        'STANDALONE'
    ];

    const roleLabels={
        PREFIX:'접두',
        NAME_TEMPLATE:'이름',
        SUFFIX:'접미',
        STANDALONE:'독립'
    };

    const directions=[
        'n','s','e','w',
        'ne','nw','se','sw'
    ];

    const defaultGeometry=()=>
        Object.fromEntries(
            ids.map(
                (id,i)=>[
                    id,
                    {
                        x:2+(i%3)*32,
                        y:4+Math.floor(i/3)*46,
                        w:28,
                        h:38
                    }
                ]));

    // Store stable IDs independently of the currently loaded font list.
    // Missing fonts must never invalidate a saved Title layout or preset.
    const validFontId=id=>
        typeof id==='string' &&
        id.length>0 &&
        id.length<=512 &&
        !/[\x00-\x1f\x7f]/.test(id);

    const fontFamilies={
        system:'"Malgun Gothic",sans-serif'
    };

    const defaultPresentation=()=>
        Object.fromEntries(
            ids.map(
                id=>[
                    id,
                    {
                        fontId:'system',
                        fontScale:100,
                        opacity:100,
                        rotationDeg:0,
                        outlineWidth:2
                    }
                ]));

    const defaults=()=>({
        version:1,
        geometry:defaultGeometry(),
        presentation:defaultPresentation()
    });

    const validRect=g=>
        g &&
        ['x','y','w','h'].every(
            k=>Number.isInteger(g[k])) &&
        g.x>=0 &&
        g.y>=0 &&
        g.w>=4 &&
        g.h>=6 &&
        g.x+g.w<=100 &&
        g.y+g.h<=100;

    const validPresentation=p=>
        p &&
        validFontId(p.fontId) &&
        Number.isInteger(p.fontScale) &&
        p.fontScale>=50 &&
        p.fontScale<=400 &&
        Number.isInteger(p.opacity) &&
        p.opacity>=0 &&
        p.opacity<=100 &&
        Number.isInteger(p.rotationDeg) &&
        p.rotationDeg>=-180 &&
        p.rotationDeg<=180 &&
        Number.isInteger(p.outlineWidth) &&
        p.outlineWidth>=0 &&
        p.outlineWidth<=8;

    const normalizePresentation=value=>{
        const fallback=defaultPresentation();
        return Object.fromEntries(
            ids.map(id=>{
                const stored=value?.[id];
                // v1 saves predate per-block outlines. Preserve old font,
                // opacity, rotation and geometry when upgrading them.
                const migrated=stored && typeof stored==='object' ? {
                    ...stored,
                    outlineWidth:Number.isInteger(stored.outlineWidth) &&
                        stored.outlineWidth>=0 && stored.outlineWidth<=8 ?
                        stored.outlineWidth : 2
                } : null;
                return [id,validPresentation(migrated) ? {
                    ...migrated,
                    opacity:Math.max(5,migrated.opacity)
                } : fallback[id]];
            }));
    };

    const validLayout=value=>
        value?.version===1 &&
        value.geometry &&
        ids.every(
            id=>validRect(value.geometry[id]));

    function clone(value){
        return JSON.parse(
            JSON.stringify(
                value));
    }

    function read(key,fallback){
        try{
            const raw=
                localStorage.getItem(
                    key);

            if(raw===null){
                return fallback;
            }

            const value=
                JSON.parse(raw);

            if(!validLayout(value)){
                return fallback;
            }

            return {
                ...value,
                presentation:
                    normalizePresentation(value.presentation)
            };
        }
        catch{
            return fallback;
        }
    }

    let layout=
        read(
            titleKey,
            defaults());

    let state=null;
    let modal=null;
    let surface=null;
    let selected=1;
    let drag=null;
    let lastFocus=null;

    let statusText='';
    let currentPreset='1';

    let busy=false;
    let connected=false;
    let pendingStateRef=null;
    let pendingPreset=null;

    function text(element,value){
        if(element &&
           element.textContent!==value){

            element.textContent=value;
        }
    }

    function button(
        label,
        action,
        parent)
    {
        const element=
            doc.createElement(
                'button');

        element.type=
            'button';

        element.textContent=
            label;

        if(action){
            element.addEventListener(
                'click',
                action);
        }

        if(parent){
            parent.append(element);
        }

        return element;
    }

    function stopCenter(){
        window.TULLCenterAuthoring
            ?.cancel?.();

        window.TULLCenterAuthoring
            ?.setEdit?.(false);
    }

    function persist(){
        try{
            localStorage.setItem(
                titleKey,
                JSON.stringify({
                    version:1,
                    geometry:
                        layout.geometry,
                    presentation:
                        layout.presentation
                }));

            win.TULLFTMinimalClient?.publish?.();
            return true;
        }
        catch{
            status(
                '배치를 저장할 수 없습니다. 저장 공간을 확인하십시오.');

            return false;
        }
    }

    function status(value){
        statusText=value;

        text(
            modal?.querySelector(
                '[data-title-status]'),
            value);
    }

    function activeMask(
        snapshot=state)
    {
        return snapshot?.slots?.reduce(
            (mask,slot)=>
                mask |
                (
                    slot.active ?
                        1 << (slot.slot-1) :
                        0
                ),
            0) || 0;
    }

    /*
        Production title-state authority:
        current C++ snapshot has no Preview-only revision field.
    */
    function validateState(snapshot){
        return (
            snapshot?.kind==='title-state' &&
            snapshot.slots?.length===6 &&
            snapshot.slots.every(
                (slot,i)=>
                    slot.slot===i+1 &&
                    typeof slot.active==='boolean' &&
                    roles.includes(
                        slot.role) &&
                    typeof slot.auto==='boolean' &&
                    typeof slot.resolvedText==='string')
        );
    }

    function syncState(){
        const next=
            (
                typeof titleState!=='undefined' &&
                validateState(titleState)
            ) ?
                titleState :
                null;

        const changed=
            next &&
            next!==state;

        // Reset presentation only when a slot changes
        // its active lifetime. Preset restore is separate.
        if(next && state && next!==state && !pendingPreset){
            const fresh=defaultPresentation();
            let reset=false;
            for(let index=0;index<ids.length;index++){
                if(state.slots[index].active!==next.slots[index].active){
                    const id=ids[index];
                    layout.presentation[id]=fresh[id];
                    reset=true;
                }
            }
            if(reset) persist();
        }

        state=next;

        connected=
            !!state &&
            typeof tullTitleCommand==='function';

        if(
            busy &&
            state &&
            state!==pendingStateRef
        ){
            busy=false;
            pendingStateRef=null;

            if(pendingPreset){
                const preset=
                    pendingPreset;

                pendingPreset=null;

                if(
                    activeMask(state)!==
                    preset.activeMask
                ){
                    status(
                        'DLL 상태와 프리셋 활성 블록 구성이 일치하지 않습니다.');
                }
                else{
                    const previous=
                        layout;

                    layout={
                        version:1,
                        geometry:
                            clone(
                                preset.geometry),
                        presentation:
                            normalizePresentation(
                                preset.presentation)
                    };

                    if(!persist()){
                        layout=previous;
                    }
                    else{
                        status('')
                    }
                }
            }
            else{
                status('')
            }
        }

        return changed;
    }

    /*
        Current implemented production bridge uses
        tullTitleCommand(dot command).
        UI never mutates semantic state optimistically.
    */
    function commandOf(payload){
        if(
            !payload ||
            typeof payload.op!=='string'
        ){
            return null;
        }

        if(payload.op==='refresh'){
            return 'title.refresh';
        }

        if(payload.op==='setNickname'){
            return (
                'title.nickname.' +
                encodeURIComponent(
                    String(
                        payload.nickname ?? ''))
            );
        }

        if(payload.op==='setAuto'){
            return (
                `title.auto.${Number(payload.slot)}.` +
                `${payload.enabled ? 1 : 0}`
            );
        }

        if(payload.op==='removeSlot'){
            return (
                `title.remove.${Number(payload.slot)}`
            );
        }

        if(
            payload.op==='manualAdd' &&
            roles.includes(
                payload.role)
        ){
            return (
                `title.add.${payload.role}`
            );
        }

        if(
            payload.op==='applyPresetPresenceMask' &&
            Number.isInteger(
                payload.activeMask) &&
            payload.activeMask>=0 &&
            payload.activeMask<=63
        ){
            return (
                `title.presence.${payload.activeMask}`
            );
        }

        return null;
    }

    function request(
        payload,
        preset=null)
    {
        syncState();

        if(
            busy ||
            !connected
        ){
            return false;
        }

        const command=
            commandOf(
                payload);

        if(!command){
            status(
                '지원하지 않는 칭호 요청입니다.');

            return false;
        }

        busy=true;

        pendingStateRef=
            state;

        pendingPreset=
            preset;

        cancelDrag();

        status(
            'DLL 응답 대기 중…');

        render();

        try{
            tullTitleCommand(
                command);

            return true;
        }
        catch{
            busy=false;
            pendingStateRef=null;
            pendingPreset=null;

            status(
                '칭호 명령을 전달하지 못했습니다.');

            render();

            return false;
        }
    }    function paintRect(element,g) {
        Object.assign(element.style,{left:`${g.x}%`,top:`${g.y}%`,width:`${g.w}%`,height:`${g.h}%`});
    }

    function titleTextSize(g,editing) {
        const referenceArea=28*38;
        const area=Math.max(1,g.w*g.h);
        const scale=Math.sqrt(area/referenceArea);

        const base=17;
        const minimum=12;
        const maximum=34;

        return Math.max(
            minimum,
            Math.min(
                maximum,
                Math.round(base*scale*10)/10));
    }

    function applyTitleTypography(
        block,
        g,
        editing,
        presentation)
    {
        const content=
            block.querySelector('.title-block-text');

        const meta=
            block.querySelector('.title-block-meta');

        const baseSize=
            titleTextSize(g,editing);

        const size=
            Math.round(
                baseSize *
                (presentation.fontScale/100) *
                10) / 10;

        if(content){
            content.style.fontSize=`${size}px`;
            content.style.lineHeight='1.08';
            content.style.fontFamily=
                win.TULLFontManager?.familyForTitle?.(
                    presentation.fontId) ||
                fontFamilies.system;
            content.style.opacity=
                String(presentation.opacity/100);
            // Width is per-block; theme/override controls own outline color.
            const width=presentation.outlineWidth;
            const diagonal=Math.round(width*0.75*100)/100;
            content.style.setProperty('--tull-block-stroke',`${width}px`);
            content.style.setProperty('--tull-block-stroke-negative',`${-width}px`);
            content.style.setProperty('--tull-block-stroke-diagonal',`${diagonal}px`);
            content.style.setProperty('--tull-block-stroke-diagonal-negative',`${-diagonal}px`);
            content.dataset.tullOutlineZero=width===0 ? '1' : '0';
            content.style.transform=
                `translate(-50%, -50%) rotate(${presentation.rotationDeg}deg)`;
            content.style.transformOrigin=
                'center center';
        }

        if(editing && meta){
            meta.style.fontSize=
                `${Math.max(
                    8,
                    Math.min(
                        14,
                        Math.round(baseSize*0.6*10)/10)
                )}px`;
        }
    }
    function renderSurface(host,editing) {
        if(!host) return;
        const active=state?.slots.filter(slot=>slot.active)||[];
        for(const existing of Array.from(host.children)) if(existing.dataset.slot && !active.some(slot=>String(slot.slot)===existing.dataset.slot)) existing.remove();
        for(const slot of active) {
            let block=host.querySelector(`[data-slot="${slot.slot}"]`);
            if(!block) {
                block=doc.createElement('div');block.className='title-internal-block';block.dataset.slot=String(slot.slot);
                block.id=editing?`title-editor-${ids[slot.slot-1]}`:ids[slot.slot-1];
                const number=doc.createElement('span');number.className='title-block-meta';block.append(number);
                const content=doc.createElement('span');content.className='title-block-text';block.append(content);
                if(editing) {
                    block.tabIndex=0;block.setAttribute('role','group');
                    for(const direction of directions){const handle=doc.createElement('span');handle.className=`title-resize title-resize-${direction}`;handle.dataset.resize=direction;handle.setAttribute('aria-hidden','true');block.append(handle);}
                    block.addEventListener('focus',()=>{selected=slot.slot;renderInspector();});
                }
                host.append(block);
            }
            const id=
                ids[slot.slot-1];

            const geometry=
                layout.geometry[id];

            const presentation=
                layout.presentation[id];

            paintRect(block,geometry);
            applyTitleTypography(
                block,
                geometry,
                editing,
                presentation);

            text(block.querySelector('.title-block-meta'),`${String(slot.slot).padStart(2,'0')} · ${roleLabels[slot.role]} · AUTO ${slot.auto?'ON':'OFF'}`);
            text(block.querySelector('.title-block-text'),editing ? (slot.resolvedText || '비어 있음') : slot.resolvedText);
            block.classList.toggle('selected',editing&&selected===slot.slot);
            block.classList.toggle('title-empty',!String(slot.resolvedText||'').trim());
            block.setAttribute('aria-label',`${ids[slot.slot-1]} ${roleLabels[slot.role]}`);
        }
    }
    function ensureSection() {
        const section=doc?.getElementById('titleSection');
        if(!section) return;

        section.classList.add(
            's18-title-authoring-runtime');

        const host=
            section.querySelector(
                '#titleGridSurface');

        if(host){
            host.style.gridTemplateColumns='';
            renderSurface(host,false);
        }

        const heading=
            section.querySelector(
                '.s18-title-heading');

        if(!heading) return;

        let group=
            heading.querySelector(
                '.s18-title-heading-main');

        if(!group){
            const name=
                heading.lastElementChild;

            group=
                doc.createElement(
                    'span');

            group.className=
                's18-title-heading-main';

            if(name){
                group.append(name);
            }

            heading.append(group);
        }

        let entry=
            group.querySelector(
                '.s18-title-authoring-open');

        if(!entry){
            entry=
                button(
                    '칭호 편집',
                    openModal,
                    group);

            entry.className=
                's18-title-authoring-open';
        }

        entry.disabled=
            !connected;
    }
    function syncTitleFontPicker() {
        if(!modal) return;
        const select=modal.querySelector(
            'select[data-presentation="fontId"]');
        if(!select) return;

        const selectedFont=
            layout.presentation[ids[selected-1]]?.fontId || 'system';
        const options=[
            ['system','기본 칭호 폰트 따르기'],
            ['system-fixed','맑은 고딕 (고정)']
        ];

        const available=win.TULLFontManager?.getOptions?.();
        if(Array.isArray(available)) {
            for(const entry of available) {
                if(typeof entry?.id!=='string' ||
                   !entry.id.startsWith('user/') ||
                   !validFontId(entry.id) ||
                   options.some(option=>option[0]===entry.id)) continue;

                const label=typeof entry.label==='string' ?
                    entry.label.slice(0,160) : entry.id.slice(0,160);
                options.push([entry.id,label]);
            }
        }

        if(!options.some(option=>option[0]===selectedFont)) {
            options.push([
                selectedFont,
                '현재 사용 불가 (맑은 고딕 대체): ' +
                    selectedFont.slice(0,80),
                true
            ]);
        }

        const signature=JSON.stringify(options);
        if(select.dataset.fontSignature!==signature) {
            select.replaceChildren(
                ...options.map(([id,label,unavailable])=>{
                    const option=new Option(label,id);
                    option.disabled=!!unavailable;
                    return option;
                })
            );
            select.dataset.fontSignature=signature;
        }
        select.value=selectedFont;
    }

    function renderInspector() {
        if(!modal) return;
        const slot=state?.slots.find(s=>s.slot===selected && s.active);
        const blockSelect=modal.querySelector('[data-block-select]');
        if(blockSelect){
            const activeSlots=(state?.slots||[]).filter(s=>s.active);
            const signature=activeSlots.map(s=>`${s.slot}:${s.role}:${s.resolvedText}`).join('|');
            if(blockSelect.dataset.signature!==signature){
                blockSelect.replaceChildren(...activeSlots.map(s=>{
                    const option=doc.createElement('option');
                    const role=s.role==='PREFIX'?'접두':s.role==='NAME_TEMPLATE'?'이름':s.role==='SUFFIX'?'접미':'독립';
                    option.value=String(s.slot);
                    option.textContent=`${String(s.slot).padStart(2,'0')} · ${role} · ${s.resolvedText||'비어 있음'}`;
                    return option;
                }));
                blockSelect.dataset.signature=signature;
            }
            blockSelect.value=String(selected);
        }
        surface?.querySelectorAll('.title-internal-block').forEach(block=>{
            block.classList.toggle('selected',Number(block.dataset.slot)===selected);
        });
        const nickname=modal.querySelector('[data-nickname]');
        if(nickname && doc.activeElement!==nickname) nickname.value=state?.nickname||'';
        nickname.disabled=busy||!connected;
        modal.querySelector('[data-nickname-apply]').disabled=busy||!connected;
        modal.querySelector('[data-nickname-clear]').disabled=busy||!connected||!state?.nickname;
        text(modal.querySelector('[data-effective-name]'),`표시 이름: ${state?.effectiveName||state?.characterName||''}`);
        text(modal.querySelector('[data-selection]'),slot?`${ids[selected-1]} · ${roleLabels[slot.role]}`:'블록을 선택하십시오.');
        const auto=modal.querySelector('[data-auto]');text(auto,`AUTO ${slot?.auto?'ON':'OFF'}`);
        auto.disabled=busy || !connected || !slot;
        modal.querySelector('[data-remove]').disabled=busy || !connected || !slot;
        modal.querySelector('[data-add]').disabled=busy || !connected || !state?.slots.some(s=>!s.active);
        for(const input of modal.querySelectorAll('[data-geometry]')) {
            input.disabled=busy||!slot;
            if(!slot) continue;
            const g=layout.geometry[ids[selected-1]],key=input.dataset.geometry;
            input.max=String(key==='x'?100-g.w:key==='y'?100-g.h:
                key==='w'?100-g.x:100-g.y);
            input.value=String(g[key]);
            const display=modal.querySelector(`[data-geom-value="${key}"]`);
            if(display) display.textContent=`${g[key]}%`;
        }
        for(const input of modal.querySelectorAll('[data-presentation]')) {
            input.disabled=busy||!slot;
            if(!slot) continue;
            const key=input.dataset.presentation,value=layout.presentation[ids[selected-1]][key];
            input.value=String(value);
            const display=modal.querySelector(`[data-pres-value="${key}"]`);
            if(display) display.textContent=`${value}${key==='rotationDeg'?'°':'%'}`;
        }
        syncTitleFontPicker();
        const opacitySlider=modal.querySelector('[data-title-opacity-slider]');
        if(opacitySlider) {
            opacitySlider.disabled=busy||!slot;
            if(slot) opacitySlider.value=String(layout.presentation[ids[selected-1]].opacity);
        }
        const outlineSlider=modal.querySelector('[data-title-outline-slider]');
        const outlineValue=modal.querySelector('[data-title-outline-value]');
        if(outlineSlider){
            outlineSlider.disabled=busy||!slot;
            const width=slot ? layout.presentation[ids[selected-1]].outlineWidth : 2;
            outlineSlider.value=String(width);
            if(outlineValue) outlineValue.textContent=`${width}px`;
        }
        for(const block of surface.children) block.classList.toggle('selected',Number(block.dataset.slot)===selected);
        for(const key of ['save','load','default']) modal.querySelector(`[data-title-${key}]`).disabled=busy||!connected;
        const preset=read(presetKey(currentPreset),null);
        modal.querySelector('[data-title-load]').disabled=busy||!connected||!preset||!Number.isInteger(preset.activeMask)||preset.activeMask<0||preset.activeMask>63;
        for(const button of modal.querySelectorAll('[data-title-slot]')) {
            const number=button.dataset.titleSlot;
            const presetData=read(presetKey(number),null);
            const stored=!!presetData && Number.isInteger(presetData.activeMask) &&
                presetData.activeMask>=0 && presetData.activeMask<=63;
            const selectedPreset=number===currentPreset;
            button.classList.toggle('is-saved',stored);
            button.classList.toggle('is-selected',selectedPreset);
            button.setAttribute('aria-pressed',String(selectedPreset));
            button.setAttribute('aria-label',`칭호 프리셋 ${number}${stored?' 저장됨':' 비어 있음'}`);
            button.disabled=busy||!connected;
        }
    }
    function render() {ensureSection();renderSurface(surface,true);renderInspector();text(modal?.querySelector('[data-title-status]'),statusText);}
    function cancelDrag() {if(drag){layout.geometry[ids[drag.slot-1]]=drag.start;drag=null;render();}}
    let modalScrollLock=null;

    function lockBackgroundScroll() {
        if(modalScrollLock) return;
        modalScrollLock=['leftPanel','centerPanel','rightPanel'].map(id=>{
            const element=doc.getElementById(id);
            if(!element) return null;
            const saved={element,overflowY:element.style.overflowY,scrollTop:element.scrollTop};
            element.style.overflowY='hidden';
            return saved;
        }).filter(Boolean);
    }

    function unlockBackgroundScroll() {
        if(!modalScrollLock) return;
        for(const saved of modalScrollLock) {
            if(!saved.element?.isConnected) continue;
            saved.element.style.overflowY=saved.overflowY;
            saved.element.scrollTop=saved.scrollTop;
        }
        modalScrollLock=null;
    }

    function closeModal() {
        cancelDrag();closeTitleContextPopup();if(modal?.open)modal.close();unlockBackgroundScroll();lastFocus?.isConnected&&lastFocus.focus();
    }
    function openModal() {
        if(!modal) createModal();
        if(modal.open) return;
        lastFocus=doc.activeElement;stopCenter();render();modal.showModal();lockBackgroundScroll();modal.querySelector('[data-title-close]').focus();
    }
    const titleRoleLabels={
        PREFIX:'접두',
        NAME_TEMPLATE:'이름',
        SUFFIX:'접미',
        STANDALONE:'독립'
    };

    function defaultTitleRole(slotNumber) {
        if(slotNumber===1) return 'PREFIX';
        if(slotNumber===2) return 'NAME_TEMPLATE';
        if(slotNumber===3) return 'SUFFIX';
        return 'STANDALONE';
    }

    function nextInactiveTitleSlot() {
        return state?.slots?.find(
            slot=>!slot.active
        ) || null;
    }

    let titleContextPopup=null;
    let titlePresentationFields=null;

    function ensureTitleContextPopup() {
        if(titleContextPopup) return titleContextPopup;

        titleContextPopup=
            doc.createElement('div');

        titleContextPopup.id=
            'titleContextPopup';

        titleContextPopup.setAttribute(
            'role',
            'menu');

        modal.append(
            titleContextPopup);

        titleContextPopup.addEventListener(
            'pointerdown',
            event=>event.stopPropagation());

        titleContextPopup.addEventListener(
            'click',
            event=>{
                const button=
                    event.target.closest(
                        '[data-title-context-action]');

                if(!button ||
                   button.disabled ||
                   busy ||
                   !connected) {
                    return;
                }

                const action=
                    button.dataset.titleContextAction;

                const slotNumber=
                    Number(
                        titleContextPopup.dataset.slot || 0);

                if(action==='auto') {
                    const slot=
                        state?.slots?.[slotNumber-1];

                    if(slot?.active) {
                        request({
                            op:'setAuto',
                            slot:slotNumber,
                            enabled:!slot.auto
                        });
                    }

                    closeTitleContextPopup();
                    return;
                }

                if(action==='remove') {
                    const slot=
                        state?.slots?.[slotNumber-1];

                    if(slot?.active) {
                        request({
                            op:'removeSlot',
                            slot:slotNumber
                        });
                    }

                    closeTitleContextPopup();
                    return;
                }

                if(action==='create-default' ||
                   action==='create-custom') {

                    const next=
                        nextInactiveTitleSlot();

                    if(!next) {
                        closeTitleContextPopup();
                        return;
                    }

                    let role=
                        defaultTitleRole(
                            next.slot);

                    if(action==='create-custom') {
                        role=
                            titleContextPopup
                                .querySelector(
                                    '[data-title-context-role]')
                                ?.value ||
                            role;
                    }

                    request({
                        op:'manualAdd',
                        role
                    });

                    closeTitleContextPopup();
                }
            });

        return titleContextPopup;
    }

    function closeTitleContextPopup() {
        if(!titleContextPopup) return;

        titleContextPopup.classList.remove(
            'open');

        delete titleContextPopup.dataset.slot;
        delete titleContextPopup.dataset.mode;
    }

    function placeTitleContextPopup(x,y) {
        const popup=
            ensureTitleContextPopup();

        popup.classList.add('open');

        const width=
            Math.max(
                225,
                popup.offsetWidth);

        const height=
            Math.max(
                90,
                popup.offsetHeight);

        popup.style.left=
            `${Math.max(
                8,
                Math.min(
                    x+8,
                    win.innerWidth-width-12)
            )}px`;

        popup.style.top=
            `${Math.max(
                8,
                Math.min(
                    y+8,
                    win.innerHeight-height-12)
            )}px`;
    }

    function openTitleBlockContext(
        block,
        x,
        y)
    {
        const slotNumber=
            Number(
                block?.dataset?.slot);

        const slot=
            state?.slots?.[
                slotNumber-1];

        if(!slot?.active) return;

        selected=slotNumber;
        block.focus();
        renderInspector();

        const popup=
            ensureTitleContextPopup();

        popup.dataset.mode='block';
        popup.dataset.slot=
            String(slotNumber);

        popup.innerHTML=`
            <div class="title-context-heading">
                ${ids[slotNumber-1]}
                · ${titleRoleLabels[slot.role] || slot.role}
            </div>

            <div class="title-context-actions">
                <button
                    type="button"
                    data-title-context-action="auto">
                    AUTO ${slot.auto ? 'ON' : 'OFF'}
                </button>

                <button
                    type="button"
                    class="title-context-remove"
                    data-title-context-action="remove">
                    블록 삭제
                </button>
            </div>
        `;

        // Reuse the existing controls and event listeners.
        popup.append(titlePresentationFields);
        renderInspector();
        placeTitleContextPopup(x,y);
    }

    function openTitleCreateContext(x,y) {
        const next=
            nextInactiveTitleSlot();

        const popup=
            ensureTitleContextPopup();

        popup.dataset.mode='create';

        if(!next) {
            popup.innerHTML=`
                <div class="title-context-heading">
                    블록 생성
                </div>

                <button
                    type="button"
                    disabled>
                    생성 가능한 슬롯 없음
                </button>
            `;

            placeTitleContextPopup(x,y);
            return;
        }

        const role=
            defaultTitleRole(
                next.slot);

        popup.dataset.slot=
            String(next.slot);

        popup.innerHTML=`
            <div class="title-context-heading">
                ${ids[next.slot-1]} 생성
            </div>

            <div class="title-context-note">
                기본 속성:
                ${titleRoleLabels[role]}
                · AUTO ON
            </div>

            <div class="title-context-actions">
                <button
                    type="button"
                    data-title-context-action="create-default">
                    기본값으로 생성
                </button>

                <select
                    data-title-context-role
                    aria-label="생성 블록 역할">
                    <option value="PREFIX">접두</option>
                    <option value="NAME_TEMPLATE">이름</option>
                    <option value="SUFFIX">접미</option>
                    <option value="STANDALONE">독립</option>
                </select>

                <button
                    type="button"
                    data-title-context-action="create-custom">
                    선택 속성으로 생성
                </button>
            </div>
        `;

        popup.querySelector(
            '[data-title-context-role]'
        ).value=role;

        placeTitleContextPopup(x,y);
    }

    function bindTitleContextMenu() {
        if(!surface ||
           surface.dataset.titleContextBound==='1') {
            return;
        }

        surface.dataset.titleContextBound='1';

        ensureTitleContextPopup();

        let pointer=null;

        surface.addEventListener(
            'pointerdown',
            event=>{
                if(event.button!==0 ||
                   busy ||
                   !connected) {
                    return;
                }

                closeTitleContextPopup();

                pointer={
                    id:event.pointerId,
                    x:event.clientX,
                    y:event.clientY,
                    moved:false,
                    resize:
                        !!event.target.closest(
                            '.title-resize'),
                    block:
                        event.target.closest(
                            '.title-internal-block')
                };
            });

        surface.addEventListener(
            'pointermove',
            event=>{
                if(!pointer ||
                   pointer.id!==event.pointerId) {
                    return;
                }

                if(Math.abs(
                       event.clientX-pointer.x)>4 ||
                   Math.abs(
                       event.clientY-pointer.y)>4) {

                    pointer.moved=true;
                }
            });

        surface.addEventListener(
            'pointerup',
            event=>{
                if(!pointer ||
                   pointer.id!==event.pointerId) {
                    return;
                }

                const hit=pointer;
                pointer=null;

                if(hit.moved || hit.resize) {
                    closeTitleContextPopup();
                    return;
                }

                queueMicrotask(()=>{
                    if(hit.block) {
                        openTitleBlockContext(
                            hit.block,
                            event.clientX,
                            event.clientY);
                    } else {
                        openTitleCreateContext(
                            event.clientX,
                            event.clientY);
                    }
                });
            });

        surface.addEventListener(
            'pointercancel',
            ()=>{
                pointer=null;
                closeTitleContextPopup();
            });

        modal.addEventListener(
            'close',
            closeTitleContextPopup);

        modal.addEventListener(
            'pointerdown',
            event=>{
                if(!titleContextPopup ||
                   !titleContextPopup.classList.contains(
                       'open')) {
                    return;
                }

                if(!titleContextPopup.contains(
                       event.target) &&
                   !surface.contains(
                       event.target)) {

                    closeTitleContextPopup();
                }
            });
    }
    function createModal() {
        modal=doc.createElement('dialog');modal.id='titleInternalModal';modal.setAttribute('aria-labelledby','title-editor-heading');
        modal.innerHTML=`<header><div><h2 id="title-editor-heading">칭호 내부 편집</h2><p>칭호 블록을 이동하거나 가장자리와 모서리를 끌어 크기를 조절하십시오.</p></div><button type="button" data-title-close aria-label="칭호 편집 닫기">×</button></header>
        <div class="title-modal-tools"><div class="title-preset-slots" role="group" aria-label="칭호 레이아웃 프리셋"><button type="button" data-title-slot="1" aria-pressed="true">1</button><button type="button" data-title-slot="2" aria-pressed="false">2</button><button type="button" data-title-slot="3" aria-pressed="false">3</button></div><button type="button" data-title-save>칭호 배치 저장</button><button type="button" data-title-load>칭호 배치 불러오기</button><button type="button" data-title-default>배치 기본값</button><label>선택 블록 <select data-block-select aria-label="편집할 칭호 블록"></select></label></div><div class="title-nickname-tools"><label>닉네임 <input data-nickname type="text" autocomplete="off" placeholder="비우면 캐릭터명"></label><button type="button" data-nickname-apply>적용</button><button type="button" data-nickname-clear>캐릭터명</button><span data-effective-name></span><small>공백 포함 최대 ${POT_TITLE_MAX_CHARS}자</small></div>
        <div id="titleEditorSurface" aria-label="칭호 내부 배치 영역"></div>
        <div data-pot-ft-controls></div>
        <div class="title-modal-inspector"><strong data-selection></strong><button type="button" data-auto>AUTO</button><button type="button" data-remove>선택 블록 비활성화</button><label>새 블록 역할 <select data-role aria-label="새 블록 역할"><option value="PREFIX">접두</option><option value="NAME_TEMPLATE">이름</option><option value="SUFFIX">접미</option><option value="STANDALONE">독립</option></select></label><button type="button" data-add>블록 생성</button></div>
        <div class="title-geometry-fields"><label>X <input data-geometry="x" type="range" min="0" max="96" step="1"><output data-geom-value="x">0%</output></label><label>Y <input data-geometry="y" type="range" min="0" max="94" step="1"><output data-geom-value="y">0%</output></label><label>너비 <input data-geometry="w" type="range" min="4" max="100" step="1"><output data-geom-value="w">0%</output></label><label>높이 <input data-geometry="h" type="range" min="6" max="100" step="1"><output data-geom-value="h">0%</output></label><span>선택 후 방향키로 이동 · Shift + 방향키로 크기 조절</span></div>
        <div class="title-presentation-fields"><label>폰트 <select data-presentation="fontId" aria-label="칭호 폰트"><option value="system">기본 칭호 폰트 따르기</option></select></label><label>글자 크기 % <input data-presentation="fontScale" type="range" min="50" max="400" step="5"><output data-pres-value="fontScale">100%</output></label><label>투명도 % <input data-presentation="opacity" data-title-opacity-slider type="range" min="5" max="100" step="1" value="100" aria-label="칭호 투명도 슬라이더"><output data-pres-value="opacity">100%</output></label><label>회전 ° <input data-presentation="rotationDeg" type="range" min="-180" max="180" step="5"><output data-pres-value="rotationDeg">0°</output></label><label>외곽선 굵기 px <input data-title-outline-slider type="range" min="0" max="8" step="1" value="2" aria-label="선택 칭호 블록 외곽선 굵기"><output data-title-outline-value>2px</output></label><span>Ctrl + 휠: 크기 · Shift + 휠: 회전 · 일반 휠: 블록 순환</span></div>
        <footer><span data-title-status role="status" aria-live="polite"></span><small>칭호 의미 상태는 DLL/TIT1 권위 · 내부 배치는 UI Grid 권위</small></footer>`;
        doc.body.append(modal);surface=modal.querySelector('#titleEditorSurface');
        window.TULLFTMinimalClient?.mount();
        window.TULLImeControls?.attach(modal);
        titlePresentationFields=modal.querySelector('.title-presentation-fields');
        bindTitleContextMenu();
        ensureTitleContextPopup().append(titlePresentationFields);
        modal.querySelector('[data-title-close]').addEventListener('click',closeModal);
        modal.addEventListener('cancel',event=>{event.preventDefault();if(window.TULLImeControls?.blocksEscape())return;closeModal();});
        modal.querySelector('[data-block-select]').addEventListener('change',event=>{
            selected=Number(event.target.value);
            const block=surface?.querySelector(`.title-internal-block[data-slot="${selected}"]`);
            if(block) block.focus();
            renderInspector();
        });
        const nicknameInput=modal.querySelector('[data-nickname]');
        window.TULLImeControls?.setWriteLimit?.(nicknameInput,value=>potTitleWriteAllowed(nicknameInput,value));
        const applyNickname=()=>{
            if(nicknameComposing)return;
            if(potTitleChars(nicknameInput.value).length>POT_TITLE_MAX_CHARS){
                status('닉네임은 공백 포함 최대 '+POT_TITLE_MAX_CHARS+'자입니다.');
                return;
            }
            request({op:'setNickname',nickname:nicknameInput.value});
        };
        modal.querySelector('[data-nickname-apply]').addEventListener('click',applyNickname);
        modal.querySelector('[data-nickname-clear]').addEventListener('click',()=>{nicknameInput.value='';request({op:'setNickname',nickname:''});});
        // IME may report compositionend before the Enter keydown used to commit.
        // Retain the guard until the terminating key is released.
        let nicknameComposing=false;
        let nicknameCommitKeyPending=false;
        nicknameInput.addEventListener('compositionstart',()=>{
            nicknameComposing=true;
            nicknameCommitKeyPending=false;
        });
        nicknameInput.addEventListener('compositionend',()=>{
            nicknameComposing=false;
            potLimitTitleInput(nicknameInput);
            nicknameCommitKeyPending=true;
        });
        nicknameInput.addEventListener('input',event=>{
            if(!nicknameComposing&&!event.isComposing&&!window.TULLImeControls?.isLocalInput?.(nicknameInput))potLimitTitleInput(nicknameInput);
        });
        nicknameInput.addEventListener('keydown',event=>{
            if(event.key!=='Enter') return;
            if(nicknameComposing || event.isComposing ||
               event.keyCode===229 || event.which===229 ||
               nicknameCommitKeyPending) {
                // Let the input method finalize the composing character.
                return;
            }
            event.preventDefault();
            applyNickname();
        });
        nicknameInput.addEventListener('keyup',()=>{
            nicknameCommitKeyPending=false;
        });
        nicknameInput.addEventListener('blur',()=>{
            if(nicknameComposing)potLimitTitleInput(nicknameInput);
            nicknameComposing=false;
            nicknameCommitKeyPending=false;
        });
        modal.querySelector('[data-auto]').addEventListener('click',()=>{const slot=state.slots[selected-1];request({op:'setAuto',slot:selected,enabled:!slot.auto});});
        modal.querySelector('[data-remove]').addEventListener('click',()=>request({op:'removeSlot',slot:selected}));
        modal.querySelector('[data-add]').addEventListener('click',()=>request({op:'manualAdd',role:modal.querySelector('[data-role]').value}));
        for(const slotButton of modal.querySelectorAll('[data-title-slot]')) slotButton.addEventListener('click',()=>{if(busy||!connected)return;currentPreset=slotButton.dataset.titleSlot;renderInspector();});
        modal.querySelector('[data-title-save]').addEventListener('click',()=>{
            if(busy||!connected) return;
            try{localStorage.setItem(presetKey(currentPreset),JSON.stringify({version:1,activeMask:activeMask(),geometry:layout.geometry,presentation:layout.presentation}));status('');}catch{status('프리셋 저장 실패');}renderInspector();
        });
        modal.querySelector('[data-title-load]').addEventListener('click',()=>{
            const preset=read(presetKey(currentPreset),null);if(!preset || !Number.isInteger(preset.activeMask) || preset.activeMask<0 || preset.activeMask>63) return;
            request({op:'applyPresetPresenceMask',activeMask:preset.activeMask},preset);
        });
        modal.querySelector('[data-title-default]').addEventListener('click',()=>{if(busy||!connected)return;layout=defaults();persist();render();status('');});
        for(const input of modal.querySelectorAll('[data-geometry]')) input.addEventListener('input',()=>{
            if(busy||!state?.slots[selected-1]?.active) return;
            const next={...layout.geometry[ids[selected-1]],[input.dataset.geometry]:Number(input.value)};
            if(validRect(next)){layout.geometry[ids[selected-1]]=next;persist();render();}else{status('블록이 내부 영역을 벗어나거나 최소 크기보다 작습니다.');renderInspector();}
        });
        for(const input of modal.querySelectorAll('[data-presentation]')) input.addEventListener(input.type==='range'?'input':'change',()=>{
            if(busy||!state?.slots[selected-1]?.active) return;

            const id=ids[selected-1];
            const key=input.dataset.presentation;
            const value=
                key==='fontId' ?
                    input.value :
                    key==='opacity' ?
                        Math.max(5,Math.min(100,Math.round(Number(input.value)))) :
                        Math.round(Number(input.value));

            const next={
                ...layout.presentation[id],
                [key]:value
            };

            if(validPresentation(next)){
                layout.presentation[id]=next;
                persist();
                render();
            }
            else{
                status('칭호 표시값이 허용 범위를 벗어났습니다.');
                renderInspector();
            }
        });
        const outlineSlider=modal.querySelector('[data-title-outline-slider]');
        outlineSlider.addEventListener('input',()=>{
            if(busy||!state?.slots[selected-1]?.active) return;
            const id=ids[selected-1];
            const value=Math.max(0,Math.min(8,Math.round(Number(outlineSlider.value))));
            const next={...layout.presentation[id],outlineWidth:value};
            if(!validPresentation(next)) return;
            layout.presentation[id]=next;
            persist();
            render();
        });
        surface.addEventListener('wheel',event=>{
            if(busy||!connected) return;

            const slot=
                state?.slots?.[selected-1];

            if(
                (event.ctrlKey || event.shiftKey) &&
                slot?.active
            ){
                const direction=
                    event.deltaY>0 ? -1 :
                    event.deltaY<0 ? 1 : 0;

                if(direction===0) return;

                event.preventDefault();

                const id=ids[selected-1];
                const next={...layout.presentation[id]};

                if(event.ctrlKey){
                    next.fontScale=
                        Math.max(
                            50,
                            Math.min(
                                400,
                                next.fontScale + direction*5));
                }
                else{
                    next.rotationDeg=
                        Math.max(
                            -180,
                            Math.min(
                                180,
                                next.rotationDeg + direction*5));
                }

                layout.presentation[id]=next;
                persist();
                render();
                return;
            }

            const slots=(state?.slots||[])
                .filter(slot=>slot.active)
                .map(slot=>slot.slot);

            if(slots.length<2) return;

            event.preventDefault();
            const current=slots.indexOf(selected);
            const step=event.deltaY>0?1:-1;
            selected=current<0
                ? slots[0]
                : slots[(current+step+slots.length)%slots.length];

            const block=surface.querySelector(`.title-internal-block[data-slot="${selected}"]`);
            if(block) block.focus();
            renderInspector();
        },{passive:false});

        surface.addEventListener('pointerdown',event=>{
            if(busy||!connected||event.button!==0) return;
            const block=event.target.closest('.title-internal-block');if(!block)return;
            event.preventDefault();selected=Number(block.dataset.slot);block.focus();renderInspector();
            drag={slot:selected,start:{...layout.geometry[ids[selected-1]]},x:event.clientX,y:event.clientY,rect:surface.getBoundingClientRect(),direction:event.target.dataset.resize||'move',pointer:event.pointerId};
            surface.setPointerCapture(event.pointerId);
        });
        surface.addEventListener('pointermove',event=>{
            if(!drag||event.pointerId!==drag.pointer)return;
            const dx=Math.round((event.clientX-drag.x)*100/drag.rect.width),dy=Math.round((event.clientY-drag.y)*100/drag.rect.height);
            const g=drag.start,d=drag.direction,next={...g},clamp=(n,a,b)=>Math.max(a,Math.min(b,n));
            if(d==='move'){next.x=clamp(g.x+dx,0,100-g.w);next.y=clamp(g.y+dy,0,100-g.h);}
            else {
                if(d.includes('w')){next.x=clamp(g.x+dx,0,g.x+g.w-4);next.w=g.x+g.w-next.x;}
                if(d.includes('e'))next.w=clamp(g.w+dx,4,100-g.x);
                if(d.includes('n')){next.y=clamp(g.y+dy,0,g.y+g.h-6);next.h=g.y+g.h-next.y;}
                if(d.includes('s'))next.h=clamp(g.h+dy,6,100-g.y);
            }
            layout.geometry[ids[drag.slot-1]]=next;render();
        });
        surface.addEventListener('pointerup',event=>{if(!drag||event.pointerId!==drag.pointer)return;drag=null;persist();if(surface.hasPointerCapture(event.pointerId))surface.releasePointerCapture(event.pointerId);});
        surface.addEventListener('pointercancel',cancelDrag);
        surface.addEventListener('lostpointercapture',cancelDrag);
        surface.addEventListener('keydown',event=>{
            const arrows={ArrowLeft:[-1,0],ArrowRight:[1,0],ArrowUp:[0,-1],ArrowDown:[0,1]},delta=arrows[event.key];
            if(!delta||busy||!connected||!state.slots[selected-1].active)return;
            event.preventDefault();const next={...layout.geometry[ids[selected-1]]};
            next[event.shiftKey?'w':'x']+=delta[0];next[event.shiftKey?'h':'y']+=delta[1];
            if(validRect(next)){layout.geometry[ids[selected-1]]=next;persist();render();}
        });
    }
    function installStyle(){
        if(
            doc.getElementById(
                'S18_TITLE_AUTHORING_PRODUCTION_R1_STYLE')
        ){
            return;
        }

        const style=
            doc.createElement(
                'style');

        style.id=
            'S18_TITLE_AUTHORING_PRODUCTION_R1_STYLE';

        style.textContent=`
#titleSection.s18-title-authoring-runtime{
    display:flex!important;
    flex-direction:column;
    align-items:stretch!important;
    justify-content:flex-start!important;
    overflow:visible!important;
    padding:7px 10px!important;
    background:var(--title-bg)!important;
    border-color:var(--title-line)!important
}

.s18-title-heading-main{
    display:inline-flex;
    align-items:center;
    gap:6px;
    min-width:0
}

.s18-title-authoring-open{
    min-height:24px;
    padding:2px 7px;
    border:1px solid var(--card-line);
    color:var(--ink);
    background:var(--card-bg);
    font:inherit;
    font-size:11px;
    cursor:pointer
}

.s18-title-authoring-open:disabled{
    opacity:.42;
    cursor:default
}

#titleGridSurface{
    position:relative!important;
    display:block!important;
    min-height:0;
    flex:1
}

#titleGridSurface .title-internal-block{
    position:absolute;
    box-sizing:border-box;
    display:flex;
    flex-direction:column;
    justify-content:center;
    align-items:center;
    overflow:visible;
    padding:0;
    background:transparent!important;
    border-color:transparent!important;
    box-shadow:none!important;
    color:var(--ink)!important
}

#titleGridSurface .title-block-meta{
    display:none!important
}

#titleGridSurface .title-internal-block:hover{
    outline:2px solid var(--gold);
    outline-offset:-2px;
    background:rgba(223,199,143,.18)!important;
    box-shadow:0 0 12px rgba(182,123,32,.65)!important
}

#titleGridSurface .title-block-text{
    display:inline-block;
    flex-shrink:0;
    max-width:none;
    margin:0;
    white-space:pre;
    overflow:visible;
    text-overflow:clip
}

/* Shared Title lettering — editor and main UI */
#titleGridSurface .title-block-text,
#titleEditorSurface .title-block-text{
    color:#000!important;
    -webkit-text-fill-color:#000;
    -webkit-text-stroke:2px #fff;
    /* Prisma renderer fallback: visible 2px white contour even
       if its WebKit text-stroke implementation is unavailable. */
    text-shadow:
        -2px 0 #fff, 2px 0 #fff,
        0 -2px #fff, 0 2px #fff,
        -1.5px -1.5px #fff, 1.5px -1.5px #fff,
        -1.5px 1.5px #fff, 1.5px 1.5px #fff;
    paint-order:stroke fill
}
#titleInternalModal{
    box-sizing:border-box;
    width:min(1000px,94vw);
    max-height:94vh;
    margin:auto;
    padding:22px;
    overflow:auto;
    border:2px solid var(--frame-line);
    border-radius:6px;
    color:var(--ink);
    background:rgba(244,235,201,.98);
    box-shadow:0 18px 80px rgba(0,0,0,.48);
    font:13px var(--tull-ui-font-family, 'Malgun Gothic',sans-serif)
}

#titleInternalModal::backdrop{
    background:rgba(0,0,0,.64)
}

#titleInternalModal header{
    display:flex;
    justify-content:space-between;
    align-items:flex-start;
    gap:20px
}

#titleInternalModal h2{
    margin:0;
    font-size:22px;
    color:var(--ink)
}

#titleInternalModal p{
    margin:7px 0 18px;
    color:var(--muted);
    font-size:12px
}

#titleInternalModal button,
#titleInternalModal select,
#titleInternalModal input,
#titleContextPopup button,
#titleContextPopup select{
    color:var(--ink);
    background:rgba(255,249,226,.82);
    border:1px solid var(--card-line);
    border-radius:3px;
    padding:7px 10px;
    font:inherit
}

#titleInternalModal button:disabled,
#titleInternalModal input:disabled,
#titleInternalModal select:disabled{
    opacity:.4;
    cursor:not-allowed
}

#titleInternalModal button:focus-visible,
#titleInternalModal select:focus-visible,
#titleInternalModal input:focus-visible{
    outline:2px solid var(--gold)
}

#titleInternalModal button:not(:disabled),
#titleContextPopup button:not(:disabled){
    cursor:pointer
}

#titleInternalModal [data-title-close]{
    font-size:22px;
    padding:0 8px
}

.title-modal-tools,
.title-modal-inspector,
.title-geometry-fields,
.title-presentation-fields,
.title-nickname-tools{
    display:flex;
    flex-wrap:wrap;
    gap:10px;
    align-items:center;
    margin:10px 0
}

.title-preset-slots{
    display:inline-flex;
    align-items:center;
    gap:5px
}
#titleInternalModal .title-preset-slots button{
    width:34px;
    height:34px;
    min-width:34px;
    padding:0;
    border:1px solid var(--card-line);
    background:var(--card-bg);
    color:var(--ink);
    font-weight:700;
    border-radius:3px
}
#titleInternalModal .title-preset-slots button.is-saved{
    background:var(--card-hover-bg);
    border-color:var(--gold)
}
#titleInternalModal .title-preset-slots button.is-selected{
    outline:2px solid var(--gold);
    outline-offset:1px
}
.title-nickname-tools input{
    min-width:220px
}

.title-nickname-tools [data-effective-name]{
    font-size:11px;
    color:var(--muted)
}

#titleEditorSurface{
    position:relative;
    height:310px;
    border:1px solid var(--card-line);
    background-color:rgba(255,249,226,.44);
    background-image:
        linear-gradient(
            rgba(89,71,46,.14) 1px,
            transparent 1px),
        linear-gradient(
            90deg,
            rgba(89,71,46,.14) 1px,
            transparent 1px);
    background-size:5% 10%;
    touch-action:none;
    user-select:none
}

#titleEditorSurface .title-internal-block{
    position:absolute;
    box-sizing:border-box;
    display:flex;
    flex-direction:column;
    justify-content:center;
    align-items:center;
    cursor:move;
    overflow:visible;
    color:var(--ink)!important;
    background:transparent!important;
    border:
        1px dashed
        rgba(89,71,46,.62)!important;
    box-shadow:none!important
}

#titleEditorSurface .title-internal-block.selected{
    border:
        3px solid
        var(--gold)!important;
    outline:
        2px solid
        rgba(182,123,32,.72)!important;
    outline-offset:2px;
    z-index:50!important;
    background:
        rgba(223,199,143,.52)!important;
    box-shadow:
        0 0 0 2px
        rgba(89,71,46,.35)!important
}

#titleEditorSurface .title-block-meta{
    position:absolute;
    top:4px;
    left:6px;
    right:6px;
    font-size:11px;
    pointer-events:none
}

#titleEditorSurface .title-block-text{
    display:inline-block;
    flex-shrink:0;
    max-width:none;
    white-space:pre;
    overflow:visible;
    text-overflow:clip
}

.title-resize{
    position:absolute;
    z-index:3;
    display:block
}

.title-resize-n,
.title-resize-s{
    height:9px;
    left:10px;
    right:10px;
    cursor:ns-resize
}

.title-resize-n{top:-4px}
.title-resize-s{bottom:-4px}

.title-resize-e,
.title-resize-w{
    width:9px;
    top:10px;
    bottom:10px;
    cursor:ew-resize
}

.title-resize-e{right:-4px}
.title-resize-w{left:-4px}

.title-resize-ne,
.title-resize-nw,
.title-resize-se,
.title-resize-sw{
    width:12px;
    height:12px;
    background:var(--gold);
    border:1px solid var(--line)
}

.title-resize-ne{
    top:-5px;
    right:-5px;
    cursor:nesw-resize
}

.title-resize-nw{
    top:-5px;
    left:-5px;
    cursor:nwse-resize
}

.title-resize-se{
    bottom:-5px;
    right:-5px;
    cursor:nwse-resize
}

.title-resize-sw{
    bottom:-5px;
    left:-5px;
    cursor:nesw-resize
}

.title-geometry-fields input[type="range"],
.title-presentation-fields input[type="range"]{
    width:142px;
    min-width:100px;
    cursor:pointer;
    accent-color:var(--gold);
}
.title-geometry-fields label,
.title-presentation-fields label{
    display:flex;
    gap:6px;
    align-items:center;
}
.title-geometry-fields output,
.title-presentation-fields output{
    display:inline-block;
    min-width:36px;
    color:var(--ink);
    font-size:12px;
    text-align:right;
    font-variant-numeric:tabular-nums;
}

.title-presentation-fields select{
    min-width:170px
}

.title-geometry-fields span,
.title-presentation-fields span{
    font-size:11px;
    color:var(--muted)
}

#titleInternalModal footer{
    display:flex;
    flex-direction:column;
    gap:6px;
    margin-top:18px;
    padding-top:12px;
    border-top:
        1px solid
        var(--divider-line)
}

#titleInternalModal footer small{
    font-size:11px;
    color:var(--muted)
}

.title-modal-inspector{
    display:none!important
}

#titleContextPopup{
    position:fixed;
    z-index:99999;
    display:none;
    box-sizing:border-box;
    min-width:220px;
    max-width:310px;
    padding:10px;
    border:1px solid var(--card-line);
    border-radius:4px;
    background:rgba(244,235,201,.98);
    color:var(--ink);
    box-shadow:
        0 10px 34px
        rgba(0,0,0,.45)
}

#titleContextPopup.open{
    display:block
}

#titleContextPopup{
    max-height:75vh;
    overflow-y:auto
}

#titleContextPopup .title-presentation-fields{
    display:grid;
    grid-template-columns:repeat(2,minmax(0,1fr));
    gap:7px;
    align-items:stretch;
    border-top:1px solid var(--card-line);
    margin:10px 0 0;
    padding-top:10px
}

#titleContextPopup .title-presentation-fields label{
    display:flex;
    flex-direction:column;
    min-width:0;
    gap:3px
}

#titleContextPopup .title-presentation-fields label:first-child,
#titleContextPopup .title-presentation-fields span{
    grid-column:1 / -1
}

#titleContextPopup .title-presentation-fields input,
#titleContextPopup .title-presentation-fields select{
    min-width:0;
    width:100%;
    box-sizing:border-box
}
#titleContextPopup [data-title-opacity-slider]{
    display:block;
    width:100%;
    margin:7px 0 2px;
    cursor:pointer;
    accent-color:var(--gold)
}

.title-context-heading{
    margin-bottom:8px;
    color:var(--ink);
    font-weight:700
}

.title-context-note{
    margin-bottom:8px;
    color:var(--muted);
    font-size:11px
}

.title-context-actions{
    display:flex;
    flex-wrap:wrap;
    gap:7px;
    align-items:center
}

#titleContextPopup .title-context-remove{
    border-color:#87563e
}
/* S18_B5C2B_SHARED_TITLE_PALETTE:
   Common UI colors; preserve the black/white title lettering. */
#titleSection.s18-title-authoring-runtime{
    background:var(--card-bg)!important;
    border-color:var(--card-line)!important
}
.s18-title-authoring-open{
    font-size:14px;
    border-radius:3px
}
#titleInternalModal,
#titleContextPopup{
    background:var(--paper2);
    color:var(--ink);
    font-family:inherit;
    font-size:14px
}
#titleInternalModal p,
.title-context-note,
.title-nickname-tools [data-effective-name],
.title-geometry-fields span,
.title-presentation-fields span,
#titleInternalModal footer small{
    font-size:13px;
    line-height:1.5
}
#titleInternalModal button,
#titleInternalModal select,
#titleInternalModal input,
#titleContextPopup button,
#titleContextPopup select{
    background:var(--card-bg);
    color:var(--ink);
    border-color:var(--card-line);
    font-family:inherit;
    font-size:14px;
    min-height:32px
}
#titleInternalModal button:not(:disabled):hover,
#titleContextPopup button:not(:disabled):hover,
#titleInternalModal select:not(:disabled):hover,
#titleContextPopup select:not(:disabled):hover{
    border-color:var(--gold);
    background:var(--card-hover-bg)
}
#titleInternalModal select option,
#titleContextPopup select option{
    background:var(--paper2);
    color:var(--ink)
}
#titleInternalModal input[type="range"],
#titleContextPopup input[type="range"]{
    background:transparent;
    accent-color:var(--gold)
}
#titleEditorSurface{
    background-color:var(--card-bg);
    background-image:
        linear-gradient(var(--divider-line) 1px,transparent 1px),
        linear-gradient(90deg,var(--divider-line) 1px,transparent 1px)
}
#titleEditorSurface .title-internal-block.selected{
    outline-color:var(--gold)!important;
    background:var(--card-hover-bg)!important;
    box-shadow:0 0 0 2px var(--divider-line)!important
}

`;

        style.textContent += `
/* R1-08: Anchor text at the center of its own title block. */
#titleGridSurface .title-block-text,
#titleEditorSurface .title-block-text{
    position:absolute;
    left:50%;
    top:50%;
    margin:0;
    transform-origin:center center;
    pointer-events:none
}
`;
        doc.head.append(
            style);
    }

    const priorRenderTitleSection=
        renderTitleSection;

    renderTitleSection=
        function(){
            priorRenderTitleSection();

            syncState();

            ensureSection();

            renderSurface(
                surface,
                true);

            renderInspector();
        };

    win.addEventListener(
        'tull-grid-ready',
        ()=>{
            syncState();
            ensureSection();
        });

    /*
        Window capture runs before the existing
        document Escape close handler.
    */
    win.addEventListener(
        'keydown',
        event=>{
            if(
                event.key==='Escape' &&
                modal?.open
            ){
                event.preventDefault();
                event.stopImmediatePropagation();
                if(window.TULLImeControls?.blocksEscape()) return;
                closeModal();
            }
        },
        true);

    win.addEventListener(
        'blur',
        cancelDrag);

    installStyle();

    syncState();

    ensureSection();

    window.TULLProfileOwners.title={defaults:()=>clone(defaults()),presetDefaults:()=>({...clone(defaults()),activeMask:activeMask()}),capture:()=>({layout:clone(layout),selected,currentPreset,statusText}),busy:()=>!!modal?.open||!!drag||busy||!!pendingPreset,
      apply:()=>{const raw=localStorage.getItem(titleKey);if(raw!==null)layout=read(titleKey,defaults());render();},
      restore:s=>{layout=clone(s.layout);selected=s.selected;currentPreset=s.currentPreset;statusText=s.statusText;render();},
      expected:raw=>{const value=JSON.parse(raw);return {...value,presentation:normalizePresentation(value.presentation)};},verify:raw=>raw===null||JSON.stringify(layout)===JSON.stringify(window.TULLProfileOwners.title.expected(raw))};
    window.TULLTitleAuthoringProductionR1=
        Object.freeze({
            open:openModal,
            refresh:render,
            isOpen:()=>
                modal?.open===true,
            getCommittedLayout:()=>{
                if(drag||busy||pendingPreset)return null;
                try{const raw=localStorage.getItem(titleKey);
                    const value=raw===null?defaults():JSON.parse(raw);
                    if(!validLayout(value))return null;
                    const saved={version:1,geometry:value.geometry,presentation:normalizePresentation(value.presentation)};
                    return JSON.stringify(saved)===JSON.stringify(layout)?clone(saved):null;
                }catch{return null;}
            },
            getLayout:()=>
                clone(layout)
        });
})();
(() => {
    'use strict';

    if (window.TULLCustomTitleManagerR1) return;

    const FIRST = 1000;
    const COUNT = 20;
    const PAGE_SIZE = 5;
    const MAX_BYTES = 1024;

    let dialog = null;
    let selected = FIRST;
    let currentPage = 0;
    let pending = null;
    let pendingTimer = null;
    let deleteConfirm = false;
    let composing = false;
    let lastFocus = null;

    const code = id =>
        `C${Math.floor((id - FIRST) / 10)}${(id - FIRST) % 10}`;

    const name = () =>
        String(titleState?.effectiveName ||
               titleState?.characterName || '이름');

    const records = () =>
        Array.isArray(titleState?.customTitles) ?
            titleState.customTitles : [];

    const record = id =>
        records().find(x => Number(x?.id) === id) || null;

    const isOpen = () => !!dialog?.open;

    const validID = id =>
        Number.isInteger(id) &&
        id >= FIRST && id < FIRST + COUNT;

    const encode = text =>
        encodeURIComponent(text).replace(/\./g, '%2E');

    function showStatus(value) {
        const node = dialog?.querySelector('[data-status]');
        if (node) node.textContent = value;
    }

    function installStyle() {
        if (document.getElementById('tullCustomTitleStyle')) return;

        const style = document.createElement('style');
        style.id = 'tullCustomTitleStyle';

        style.textContent = `
#tullCustomTitleDialog{
    width:min(850px,calc(100vw - 30px));
    max-height:calc(100vh - 30px);
    padding:16px;box-sizing:border-box;
    color:var(--ink);background:var(--paper2);
    border:1px solid var(--card-line);
    border-radius:8px;box-shadow:0 16px 45px #0008;
    font:13px/1.5 var(--tull-ui-font-family, 'Malgun Gothic',sans-serif)
}
#tullCustomTitleDialog::backdrop{background:rgba(0,0,0,.62)}
#tullCustomTitleDialog button,
#tullCustomTitleDialog input,
#tullCustomTitleDialog textarea{
    font:inherit;color:var(--ink);
    border:1px solid var(--card-line);
    background:var(--card-bg);
    border-radius:4px;box-sizing:border-box
}
#tullCustomTitleDialog button{padding:6px 10px;cursor:pointer}
#tullCustomTitleDialog button:disabled{
    opacity:.5;cursor:not-allowed
}
#tullCustomTitleDialog :focus-visible{
    outline:2px solid var(--gold);outline-offset:1px
}
#tullCustomTitleDialog .ct-head,
#tullCustomTitleDialog .ct-actions{
    display:flex;align-items:center;gap:8px;flex-wrap:wrap
}
#tullCustomTitleDialog .ct-head{
    justify-content:space-between;margin-bottom:12px
}
#tullCustomTitleDialog h2{margin:0;font-size:19px}
#tullCustomTitleDialog .ct-layout{
    display:grid;
    grid-template-columns:minmax(185px,250px) minmax(0,1fr);
    gap:14px
}
#tullCustomTitleDialog .ct-list{
    height:min(50vh,400px);min-height:220px;
    overflow:auto;display:flex;flex-direction:column;gap:3px;
    padding:5px;border:1px solid var(--card-line)
}
#tullCustomTitleDialog .ct-slot{
    width:100%;display:flex;align-items:center;
    justify-content:space-between;gap:8px;text-align:left
}
#tullCustomTitleDialog .ct-slot[aria-selected=true]{
    border-color:var(--gold);
    background:var(--paper3);font-weight:700
}
#tullCustomTitleDialog .ct-slot span:last-child{
    flex:0 0 auto;color:var(--muted);font-size:11px
}
#tullCustomTitleDialog .ct-edit{
    min-width:0;display:flex;flex-direction:column;gap:9px
}
#tullCustomTitleDialog label{
    display:flex;flex-direction:column;gap:4px;font-weight:600
}
#tullCustomTitleDialog textarea{
    resize:vertical;width:100%;min-height:65px;
    padding:8px;overflow-wrap:anywhere
}
#tullCustomTitleDialog .ct-info{
    color:var(--muted);font-size:11px
}
#tullCustomTitleDialog .ct-preview{
    margin-top:14px;border:1px solid var(--card-line);
    padding:12px;background:var(--card-bg);
    min-height:50px;overflow-wrap:anywhere;white-space:pre-wrap
}
#tullCustomTitleDialog .ct-preview strong{
    display:block;font-size:11px;
    color:var(--muted);margin-bottom:6px
}
#tullCustomTitleDialog [data-preview]{
    font-size:18px;font-weight:700
}
#tullCustomTitleDialog [data-status]{
    min-height:20px;margin-top:6px;color:var(--muted)
}
#tullCustomTitleDialog [hidden]{display:none!important}
.tull-custom-title-entry{
    cursor:pointer;white-space:nowrap;
    border:1px solid var(--card-line);
    border-radius:4px;padding:4px 8px;
    background:var(--card-bg);color:var(--ink)
}
#tullCustomTitleSettings{margin-top:10px}
@media(max-width:590px){
    #tullCustomTitleDialog .ct-layout{grid-template-columns:1fr}
    #tullCustomTitleDialog .ct-list{
        height:130px;min-height:130px
    }
}
/* R1-01: the modal itself never owns the overflowing scrollbar. */
#tullCustomTitleDialog[open]{
    display:flex;flex-direction:column;overflow:hidden
}
#tullCustomTitleDialog:not([open]){display:none}
#tullCustomTitleDialog .ct-layout{
    flex:1 1 auto;min-width:0;min-height:0;overflow:hidden
}
#tullCustomTitleDialog .ct-layout > div:first-child{
    display:flex;flex-direction:column;
    min-width:0;min-height:0;overflow:hidden
}
#tullCustomTitleDialog .ct-list{
    flex:1 1 auto;height:auto;min-height:0;max-height:none;
    overflow-y:auto;overflow-x:hidden
}
#tullCustomTitleDialog .ct-edit{
    min-height:0;overflow-y:auto;overflow-x:hidden
}
#tullCustomTitleDialog .ct-preview{
    flex:0 0 auto;max-height:min(20vh,160px);
    overflow-y:auto;overflow-x:hidden
}
@media(max-width:590px){
    #tullCustomTitleDialog .ct-layout{
        grid-template-columns:1fr;overflow-y:auto;overflow-x:hidden
    }
    #tullCustomTitleDialog .ct-layout > div:first-child{
        min-height:140px;overflow:visible
    }
    #tullCustomTitleDialog .ct-list{
        flex:0 0 auto;height:130px;min-height:130px;max-height:130px
    }
    #tullCustomTitleDialog .ct-edit{overflow:visible}
}
`;

        // S18 Custom Title Tabs: five slots per page, accessible paging.
        style.textContent += `
#tullCustomTitleDialog{
    width:min(940px,calc(100vw - 30px));
    border-radius:18px
}
#tullCustomTitleDialog .ct-families,
#tullCustomTitleDialog .ct-paging{
    display:flex;align-items:center;gap:6px;
    flex:0 0 auto;margin-bottom:8px
}
#tullCustomTitleDialog .ct-family-tab{flex:1 1 0;min-width:0}
#tullCustomTitleDialog .ct-family-tab[aria-selected=true]{
    background:var(--paper3);border-color:var(--gold);
    font-weight:700
}
#tullCustomTitleDialog .ct-paging{
    justify-content:space-between;margin-top:8px;margin-bottom:0
}
#tullCustomTitleDialog .ct-paging [data-page-label]{
    color:var(--ink);font-variant-numeric:tabular-nums
}
#tullCustomTitleDialog .ct-list{
    flex:0 1 auto;height:auto;min-height:0;max-height:none;
    overflow-x:hidden;overflow-y:auto
}
#tullCustomTitleDialog .ct-slot{min-height:38px}
#tullCustomTitleDialog .ct-slot span:last-child{
    opacity:.72;font-size:12px;white-space:nowrap
}
@media(max-width:590px){
    #tullCustomTitleDialog .ct-list{
        flex:0 0 auto;height:auto;min-height:0;max-height:none
    }
}
`;

        document.head.appendChild(style);
    }

    function ensureDialog() {
        if (dialog) return dialog;
        installStyle();

        dialog = document.createElement('dialog');
        dialog.id = 'tullCustomTitleDialog';
        dialog.setAttribute(
            'aria-labelledby', 'tullCustomTitleHeading');

        dialog.innerHTML = `
<div class="ct-head">
    <h2 id="tullCustomTitleHeading">커스텀 칭호 관리</h2>
    <button type="button" data-close aria-label="관리창 닫기">×</button>
</div>
<div class="ct-layout">
    <div>
        <div class="ct-families" role="tablist" aria-label="커스텀 칭호 종류">
            <button type="button" class="ct-family-tab" data-family="0" role="tab">독립형 C00–C09</button>
            <button type="button" class="ct-family-tab" data-family="1" role="tab">이름형 C10–C19</button>
        </div>
        <div class="ct-list" data-slots
             role="listbox" aria-label="커스텀 칭호 슬롯"></div>
        <div class="ct-paging" aria-label="커스텀 칭호 페이지">
            <button type="button" data-page-prev>이전 5개</button>
            <span data-page-label aria-live="polite">1 / 2</span>
            <button type="button" data-page-next>다음 5개</button>
        </div>
    </div>
    <div class="ct-edit">
        <strong data-heading></strong>
        <div class="ct-info" data-info></div>
        <label data-before-label>
            칭호 문구
            <textarea data-before maxlength="4096"
                      spellcheck="false"></textarea>
        </label>
        <label data-after-label>
            이름 뒤 문구
            <textarea data-after maxlength="4096"
                      spellcheck="false"></textarea>
        </label>
        <div class="ct-actions">
            <button type="button" data-save>생성 / 수정</button>
            <button type="button" data-delete>삭제</button>
            <button type="button" data-favorite>☆ 즐겨찾기</button>
        </div>
        <div class="ct-info">
            <span data-title-limit>공백 포함 최대 ${POT_TITLE_MAX_CHARS}자</span>
        </div>
    </div>
</div>
<div class="ct-preview">
    <strong>실시간 미리보기 · 현재 캐릭터 이름 사용</strong>
    <span data-preview></span>
</div>
<div data-status role="status" aria-live="polite"></div>
`;

        document.body.appendChild(dialog);
        window.TULLImeControls?.attach(dialog);

        dialog.querySelector('[data-close]')
            .addEventListener('click', close);

        dialog.addEventListener('cancel', event => {
            event.preventDefault();
            if (window.TULLImeControls?.blocksEscape()) return;
            close();
        });

        // Cover dialog.close() invoked by code outside this manager.
        dialog.addEventListener('close', unlockCustomModalBackgroundScroll);

        dialog.querySelector('[data-slots]')
            .addEventListener('click', event => {
                const button =
                    event.target.closest('[data-slot-id]');

                if (button && !pending) {
                    select(Number(button.dataset.slotId));
                }
            });

        // A page/role change selects a valid slot; no storage mutation.
        dialog.querySelectorAll('[data-family]').forEach(button => {
            button.addEventListener('click', () => {
                if (pending) return;
                const family = Number(button.dataset.family);
                if (family !== 0 && family !== 1) return;
                select(FIRST + family * 10);
            });
        });

        dialog.querySelector('[data-page-prev]')
            .addEventListener('click', () => {
                if (pending || currentPage === 0) return;
                select(FIRST + (selected < FIRST + 10 ? 0 : 10));
            });
        dialog.querySelector('[data-page-next]')
            .addEventListener('click', () => {
                if (pending || currentPage !== 0) return;
                select(FIRST + (selected < FIRST + 10 ? 0 : 10) + PAGE_SIZE);
            });

        dialog.querySelector('[data-save]')
            .addEventListener('click', save);

        dialog.querySelector('[data-delete]')
            .addEventListener('click', remove);

        dialog.querySelector('[data-favorite]')
            .addEventListener('click', toggleFavorite);

        const beforeField = dialog.querySelector('[data-before]');
        const afterField = dialog.querySelector('[data-after]');
        for (const field of [beforeField, afterField]) {
            const other = field === beforeField ? afterField : beforeField;
            const remaining = () => Math.max(0, POT_TITLE_MAX_CHARS -
                (selected < FIRST + 10 ? 0 : potTitleChars(other.value).length));
            const limit = () => potLimitTitleInput(field, remaining());
            window.TULLImeControls?.setWriteLimit?.(field,value=>potTitleWriteAllowed(field,value,remaining()));

            field.addEventListener('compositionstart', () => {
                composing = true;
                deleteConfirm = false;
                updateButtons();
            });

            field.addEventListener('compositionend', () => {
                composing = false;
                limit();
                updatePreview();
                updateButtons();
            });

            field.addEventListener('input', event => {
                if (!composing && !event.isComposing && !window.TULLImeControls?.isLocalInput?.(field)) limit();
                deleteConfirm = false;
                updatePreview();
                updateButtons();
            });
        }

        return dialog;
    }

    function updatePreview() {
        if (!dialog) return;

        const before =
            dialog.querySelector('[data-before]').value;

        const after =
            dialog.querySelector('[data-after]').value;

        dialog.querySelector('[data-preview]').textContent =
            selected < FIRST + 10 ?
                before : before + name() + after;
    }

    function updateButtons() {
        if (!dialog) return;

        const stored = record(selected);
        const present = stored?.present === true;

        const saveButton = dialog.querySelector('[data-save]');
        saveButton.disabled =
            !!pending || composing ||
            !Array.isArray(titleState?.customTitles);
        saveButton.textContent =
            present ? '수정 저장' : '생성';

        const removeButton = dialog.querySelector('[data-delete]');
        removeButton.disabled = !!pending || !present;
        removeButton.textContent =
            deleteConfirm ? '삭제 확정' : '삭제';

        const star = dialog.querySelector('[data-favorite]');
        star.disabled = !!pending || !present;
        star.textContent = stored?.favorite ?
            '★ 즐겨찾기 해제' : '☆ 즐겨찾기';
    }

    function updateRows() {
        if (!dialog) return;

        const host = dialog.querySelector('[data-slots]');
        const elements = [];

        const family = selected < FIRST + 10 ? 0 : 1;
        const start = FIRST + family * 10 + currentPage * PAGE_SIZE;
        for (let id = start; id < start + PAGE_SIZE; ++id) {
            const entry = record(id);
            const item = document.createElement('button');

            item.type = 'button';
            item.className = 'ct-slot';
            item.dataset.slotId = String(id);
            item.setAttribute('role', 'option');
            item.setAttribute(
                'aria-selected', String(id === selected));
            item.disabled = !!pending;

            const display = entry?.present ?
                id < FIRST + 10 ?
                    String(entry.before || '') :
                    String(entry.before || '') +
                        '<이름>' + String(entry.after || '') :
                '빈 슬롯';

            const caption = document.createElement('span');
            caption.textContent = `${code(id)} · ${display}`;
            caption.style.cssText =
                'overflow:hidden;text-overflow:ellipsis;white-space:nowrap';

            const marker = document.createElement('span');
            marker.textContent =
                `${entry?.present ? (entry.favorite ? '★ ' : '● ') : ''}` +
                (family === 0 ? '독립형' : '이름형');

            item.append(caption, marker);
            elements.push(item);
        }

        host.replaceChildren(...elements);
        dialog.querySelectorAll('[data-family]').forEach(button => {
            const active = Number(button.dataset.family) === family;
            button.setAttribute('aria-selected', String(active));
            button.tabIndex = 0;
            button.disabled = !!pending;
        });
        dialog.querySelector('[data-page-prev]').disabled =
            !!pending || currentPage === 0;
        dialog.querySelector('[data-page-next]').disabled =
            !!pending || currentPage === 1;
        dialog.querySelector('[data-page-label]').textContent =
            `${currentPage + 1} / 2`;
    }

    function select(id) {
        if (!validID(id)) return;

        selected = id;
        currentPage = Math.floor(((id - FIRST) % 10) / PAGE_SIZE);
        deleteConfirm = false;

        const entry = record(id);

        dialog.querySelector('[data-heading]').textContent =
            `${code(id)} · ${
                id < FIRST + 10 ? '독립형' : '이름 포함형'
            }`;

        dialog.querySelector('[data-info]').textContent =
            entry?.present ?
                '현재 저장된 문구를 편집합니다.' :
                '미사용 슬롯 · 생성 시 이 위치에 저장됩니다.';

        dialog.querySelector('[data-before-label]')
            .firstChild.textContent =
                id < FIRST + 10 ?
                    '독립형 칭호 문구' : '이름 앞 문구';

        dialog.querySelector('[data-after-label]').hidden =
            id < FIRST + 10;
        dialog.querySelector('[data-title-limit]').textContent =
            (id < FIRST + 10 ? '공백 포함 최대 ' : '앞뒤 합계 최대 ')+POT_TITLE_MAX_CHARS+'자'+
            (id < FIRST + 10 ? '' : ' (공백 포함)');

        dialog.querySelector('[data-before]').value =
            entry?.present ? String(entry.before || '') : '';

        dialog.querySelector('[data-after]').value =
            entry?.present ? String(entry.after || '') : '';

        updateRows();
        updatePreview();
        updateButtons();
        showStatus('');
    }

    function invoke(command, action) {
        if (pending || typeof tullTitleCommand !== 'function') {
            showStatus('DLL 칭호 명령 연결을 확인할 수 없습니다.');
            return;
        }

        pending = {
            ...action,
            reference:titleState
        };

        if (pendingTimer !== null) {
            window.clearTimeout(pendingTimer);
        }

        pendingTimer = window.setTimeout(() => {
            if (!pending) return;

            pending = null;
            pendingTimer = null;

            updateButtons();
            updateRows();

            showStatus(
                'DLL 응답 시간 초과 · 적용 여부가 확인되지 않았습니다. ' +
                '관리창을 다시 열어 현재 상태를 확인하세요.'
            );
        }, 8000);

        deleteConfirm = false;
        updateButtons();
        updateRows();
        showStatus('DLL 응답 대기 중…');

        try {
            tullTitleCommand(command);
        }
        catch (_) {
            if (pendingTimer !== null) {
                window.clearTimeout(pendingTimer);
                pendingTimer = null;
            }

            pending = null;
            updateButtons();
            updateRows();
            showStatus('명령 전달 실패 · 저장되지 않았습니다.');
        }
    }

    function save() {
        if (pending) return;

        if (composing) {
            showStatus('한글 입력 조합을 완료한 후 저장하세요.');
            return;
        }

        const before =
            dialog.querySelector('[data-before]').value;

        const after = selected < FIRST + 10 ?
            '' : dialog.querySelector('[data-after]').value;

        if (
            (selected < FIRST + 10 && !before.trim()) ||
            (selected >= FIRST + 10 &&
             !before.trim() && !after.trim())
        ) {
            showStatus('칭호 문구를 입력하세요.');
            return;
        }

        if (potTitleChars(before).length + potTitleChars(after).length > POT_TITLE_MAX_CHARS) {
            showStatus(dialog.querySelector('[data-title-limit]').textContent);
            return;
        }

        let beforeBytes, afterBytes;

        try {
            beforeBytes = new TextEncoder().encode(before).length;
            afterBytes = new TextEncoder().encode(after).length;
        }
        catch (_) {
            showStatus('UTF-8 길이 검사 실패 · 저장되지 않았습니다.');
            return;
        }

        if (beforeBytes > MAX_BYTES || afterBytes > MAX_BYTES) {
            showStatus(
                '각 문구는 UTF-8 1,024바이트 이하여야 합니다.');
            return;
        }

        let command;

        try {
            command =
                `title.custom.set.${selected}.` +
                `${encode(before)}.${encode(after)}`;
        }
        catch (_) {
            showStatus('문구 인코딩 실패 · 저장되지 않았습니다.');
            return;
        }

        invoke(command, {
            kind:'set',
            id:selected,
            before,
            after
        });
    }

    function remove() {
        if (pending || !record(selected)?.present) return;

        if (!deleteConfirm) {
            deleteConfirm = true;
            updateButtons();
            showStatus(
                `${code(selected)}만 삭제합니다. ` +
                '재확인하려면 삭제 확정을 누르세요.');
            return;
        }

        invoke(
            `title.custom.delete.${selected}`,
            {kind:'delete',id:selected});
    }

    function toggleFavorite() {
        if (pending || !record(selected)?.present) return;

        const favorite = !!record(selected)?.favorite;

        invoke(
            `title.favorite.${selected}`,
            {
                kind:'favorite',
                id:selected,
                expected:!favorite
            });
    }

    function onState() {
        if (!isOpen()) return;

        if (pending && titleState !== pending.reference) {
            if (pendingTimer !== null) {
                window.clearTimeout(pendingTimer);
                pendingTimer = null;
            }

            const action = pending;
            const entry = record(action.id);

            const ok =
                action.kind === 'delete' ?
                    !entry?.present :
                action.kind === 'set' ?
                    entry?.present &&
                    entry.before === action.before &&
                    entry.after === action.after :
                action.kind === 'favorite' ?
                    entry?.present &&
                    entry.favorite === action.expected :
                false;

            pending = null;

            if (ok) {
                if (action.id === selected) select(selected);

                showStatus(
                    `${code(action.id)} · 상태 스냅샷에서 변경 확인`);
            }
            else {
                showStatus(
                    `${code(action.id)} · DLL이 변경을 적용하지 않았습니다.`);
            }
        }

        updateRows();
        updateButtons();
        updatePreview();
    }

    // S18 R1-01: lock only background panels while the custom manager is open.
    // Keep the dialog's own scrolling and restore each panel's original state.
    let customModalScrollLock = null;

    function lockCustomModalBackgroundScroll() {
        if (customModalScrollLock) return;
        customModalScrollLock = ['leftPanel', 'centerPanel', 'rightPanel']
            .map(id => document.getElementById(id))
            .filter(Boolean)
            .map(element => {
                const saved = {
                    element,
                    overflowY: element.style.overflowY,
                    scrollTop: element.scrollTop
                };
                element.style.overflowY = 'hidden';
                return saved;
            });
    }

    function unlockCustomModalBackgroundScroll() {
        if (!customModalScrollLock) return;
        for (const saved of customModalScrollLock) {
            saved.element.style.overflowY = saved.overflowY;
            saved.element.scrollTop = saved.scrollTop;
        }
        customModalScrollLock = null;
    }

    function open() {
        if (isOpen()) return;

        if (
            !Array.isArray(titleState?.customTitles) ||
            titleState.customTitles.length !== COUNT
        ) {
            return;
        }

        lastFocus = document.activeElement;
        ensureDialog();
        pending = null;
        composing = false;

        dialog.showModal();
        lockCustomModalBackgroundScroll();
        select(selected);

        dialog.querySelector(
            '[data-slots] [aria-selected="true"]'
        )?.focus();
    }

    function close() {
        if (!dialog?.open) return;

        dialog.close();
        unlockCustomModalBackgroundScroll();

        if (pendingTimer !== null) {
            window.clearTimeout(pendingTimer);
            pendingTimer = null;
        }

        pending = null;
        deleteConfirm = false;
        composing = false;
        showStatus('');

        if (lastFocus?.isConnected) {
            lastFocus.focus();
        }

        lastFocus = null;
    }

    function entryPoints() {
        const heading = document.querySelector(
            '#titleSection .s18-title-heading');

        if (
            heading &&
            !heading.querySelector('[data-custom-manager-open]')
        ) {
            const button = document.createElement('button');

            button.className = 'tull-custom-title-entry';
            button.type = 'button';
            button.dataset.customManagerOpen = '';
            button.textContent = '커스텀 칭호 관리';
            button.addEventListener('click', open);

            heading.appendChild(button);
        }

    }

    const oldRenderTitle = renderTitleSection;

    renderTitleSection = function(...args) {
        const result = oldRenderTitle.apply(this, args);
        entryPoints();
        return result;
    };

    const oldRenderSettings = renderSettings;

    renderSettings = function(...args) {
        const result = oldRenderSettings.apply(this, args);
        entryPoints();
        return result;
    };

    const oldReceive = window.tullReceive;

    window.tullReceive = function(...args) {
        const result = oldReceive.apply(this, args);

        let message = args[0];

        if (typeof message === 'string') {
            try {
                message = JSON.parse(message);
            }
            catch (_) {
                message = null;
            }
        }

        if (message?.kind === 'title-state') {
            onState();
        }
        else if (
            message?.kind === 'native-main-reopen' &&
            isOpen()
        ) {
            close();
        }

        return result;
    };

    const oldEscape = handleNativeEscape;

    handleNativeEscape = function(...args) {
        if (window.TULLImeControls?.blocksEscape()) return;
        if (settingsState?.hotkeyCaptureActive) {
            return oldEscape.apply(this, args);
        }

        if (isOpen()) {
            close();
            return;
        }

        return oldEscape.apply(this, args);
    };

    window.TULLCustomTitleManagerR1 =
        Object.freeze({open, close, isOpen});

    entryPoints();
})();
