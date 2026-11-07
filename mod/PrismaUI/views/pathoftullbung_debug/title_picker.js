(() => {
    'use strict';

    if (window.TULLTitlePickerProductionR1) {
        return;
    }

    const roleLabels = {
        PREFIX:'접두',
        NAME_TEMPLATE:'이름',
        SUFFIX:'접미',
        STANDALONE:'독립'
    };

    let popup = null;
    let slotNumber = 0;
    let anchorBlock = null;

    let page = 1;
    let query = '';

    let acquiredOnly = false;
    let favoriteOnly = false;

    let pending = false;
    let pendingStateRef = null;
    let pendingAction = null;

    let statusText = '';


    function catalogTitles()
    {
        return (
            titleCatalog &&
            Array.isArray(
                titleCatalog.titles
            )
        ) ?
            titleCatalog.titles :
            [];
    }


    function stateSnapshot()
    {
        return (
            titleState &&
            Array.isArray(
                titleState.slots
            )
        ) ?
            titleState :
            null;
    }


    /*
        R1-10: exactly five entries per page.
        Search/filter/page remain UI-only state.
    */
    function pageSize()
    {
        return 5;
    }


    // S18 B3-A: condition tooltip independent of browser title UI.
    let conditionBubble = null;

    function hideConditionBubble() {
        if (conditionBubble) conditionBubble.hidden = true;
    }

    function showConditionBubble(row, condition) {
        if (!popup?.classList.contains('open')) return;
        if (!conditionBubble) {
            conditionBubble = document.createElement('div');
            conditionBubble.id = 'tullTitleConditionBubble';
            conditionBubble.setAttribute('role', 'tooltip');
            document.body.appendChild(conditionBubble);
        }
        conditionBubble.textContent = '획득 조건\n' +
            (condition || '획득조건 정보가 없습니다.');
        conditionBubble.hidden = false;
        conditionBubble.style.visibility = 'hidden';

        const rect = row.getBoundingClientRect();
        const width = Math.max(
            100, Math.min(420, window.innerWidth - 16));
        conditionBubble.style.maxWidth = `${width}px`;

        const x = Math.max(8, Math.min(
            rect.left,
            window.innerWidth - conditionBubble.offsetWidth - 8
        ));
        const below = rect.bottom + 6;
        const y = below + conditionBubble.offsetHeight <=
            window.innerHeight - 8 ?
            below :
            Math.max(8, Math.min(
                rect.top - conditionBubble.offsetHeight - 6,
                window.innerHeight - conditionBubble.offsetHeight - 8
            ));

        conditionBubble.style.left = `${x}px`;
        conditionBubble.style.top = `${y}px`;
        conditionBubble.style.visibility = 'visible';
        row.setAttribute(
            'aria-describedby', conditionBubble.id);
    }

    function rows(
        slot,
        state)
    {
        const owned =
            new Set(
                Array.isArray(
                    state.ownedTitleIDs
                ) ?
                    state.ownedTitleIDs
                        .map(Number) :
                    []
            );

        const favorites =
            new Set(
                Array.isArray(
                    state.favoriteTitleIDs
                ) ?
                    state.favoriteTitleIDs
                        .map(Number) :
                    []
            );

        const q =
            String(
                query || ''
            )
            .trim()
            .toLocaleLowerCase(
                'ko-KR'
            );

        const result = [];


        if (
            slot.role ===
            'NAME_TEMPLATE'
        ) {
            result.push({
                special:true,
                specialLabel:'NAME_ONLY',
                id:0,
                segment:0,
                owned:true,
                favorite:false,
                text:
                    `이름만 · ${
                        state.effectiveName ||
                        state.characterName ||
                        ''
                    }`,
                conditionText:''
            });
        }
        else {
            result.push({
                special:true,
                specialLabel:'EMPTY',
                id:0,
                segment:0,
                owned:true,
                favorite:false,
                text:'칭호 없음',
                conditionText:''
            });
        }


        for (
            const title of
            catalogTitles()
        ) {
            const id =
                Number(
                    title?.id
                );

            if (
                !Number.isInteger(id) ||
                !Array.isArray(
                    title?.segments
                )
            ) {
                continue;
            }


            const segments =
                title.segments
                    .filter(
                        segment =>
                            segment &&
                            Number.isInteger(
                                Number(
                                    segment.index
                                )
                            ) &&
                            Array.isArray(
                                segment.roles
                            ) &&
                            segment.roles.includes(
                                slot.role
                            )
                    );


            if (
                !segments.length
            ) {
                continue;
            }


            const isOwned =
                owned.has(
                    id
                );

            const favorite =
                isOwned &&
                favorites.has(
                    id
                );


            if (
                acquiredOnly &&
                !isOwned
            ) {
                continue;
            }


            if (
                favoriteOnly &&
                !favorite
            ) {
                continue;
            }


            const numeric =
                String(
                    id
                );

            const padded =
                String(
                    id
                ).padStart(
                    4,
                    '0'
                );


            const idMatch =
                numeric.includes(
                    q
                ) ||
                padded.includes(
                    q
                );


            /*
                Unowned titles:
                public name is not searchable.
            */
            const nameMatch =
                isOwned &&
                [
                    title.name,
                    ...segments.map(
                        segment =>
                            segment.text
                    )
                ]
                .some(
                    value =>
                        String(
                            value || ''
                        )
                        .toLocaleLowerCase(
                            'ko-KR'
                        )
                        .includes(
                            q
                        )
                );


            if (
                q &&
                !idMatch &&
                !nameMatch
            ) {
                continue;
            }


            /*
                Locked title:
                one ??? row only.
                Do not expose segment text.
            */
            if (!isOwned) {
                result.push({
                    special:false,
                    id,
                    segment:
                        Number(
                            segments[0].index
                        ),
                    owned:false,
                    favorite:false,
                    text:'???',
                    conditionText:
                        String(
                            title.conditionText ||
                            ''
                        )
                });

                continue;
            }


            for (
                const segment of
                segments
            ) {
                result.push({
                    special:false,
                    id,
                    segment:
                        Number(
                            segment.index
                        ),
                    owned:true,
                    favorite,
                    text:
                        String(
                            segment.text ||
                            title.name ||
                            ''
                        ),
                    conditionText:
                        String(
                            title.conditionText ||
                            ''
                        )
                });
            }
        }


        // Custom titles always follow official titles,
        // including future official numeric IDs.
        const customRecords = Array.isArray(state.customTitles) ?
            state.customTitles : [];

        for (const record of customRecords) {
            const id = Number(record?.id);

            if (!Number.isInteger(id) ||
                id < 1000 || id > 1019 ||
                record.present !== true) {
                continue;
            }

            const standalone = id < 1010;
            const role = standalone ?
                'STANDALONE' : 'NAME_TEMPLATE';

            if (slot.role !== role) {
                continue;
            }

            const code =
                `C${Math.floor((id - 1000) / 10)}${(id - 1000) % 10}`;

            const before = String(record.before ?? '');
            const after = String(record.after ?? '');

            const text = standalone ?
                before : `${before}<이름>${after}`;

            const favorite = record.favorite === true;

            if (favoriteOnly && !favorite) {
                continue;
            }

            if (q && ![code, String(id), text].some(
                value => value.toLocaleLowerCase('ko-KR').includes(q)
            )) {
                continue;
            }

            result.push({
                special:false,
                custom:true,
                id,
                code,
                segment:0,
                owned:true,
                favorite,
                text,
                conditionText:''
            });
        }

        return result.sort(
            (left,right) =>
                Number(
                    right.special
                ) -
                Number(
                    left.special
                ) ||

                Number(
                    left.custom === true
                ) -
                Number(
                    right.custom === true
                ) ||

                Number(
                    right.favorite
                ) -
                Number(
                    left.favorite
                ) ||

                left.id -
                right.id ||

                left.segment -
                right.segment
        );
    }


    function currentSlot()
    {
        const state =
            stateSnapshot();

        if (
            !state ||
            !Number.isInteger(
                slotNumber
            ) ||
            slotNumber < 1 ||
            slotNumber > 6
        ) {
            return null;
        }


        const slot =
            state.slots[
                slotNumber -
                1
            ];


        return (
            slot &&
            slot.active === true
        ) ?
            slot :
            null;
    }


    /*
        Search/filter changes:
        keep currently selected item visible when possible.
    */
    function setPageForCurrent()
    {
        const state =
            stateSnapshot();

        const slot =
            currentSlot();


        if (
            !state ||
            !slot
        ) {
            page = 1;
            return;
        }


        const items =
            rows(
                slot,
                state
            );


        const index =
            items.findIndex(
                item =>
                    Number(
                        slot.titleID
                    ) ===
                        item.id &&

                    Number(
                        slot.segmentIndex
                    ) ===
                        item.segment
            );


        page =
            index >= 0 ?
                Math.floor(
                    index /
                    pageSize()
                ) +
                1 :
                1;
    }


    function ensureStyle()
    {
        if (
            document.getElementById(
                'S18_TITLE_PICKER_PRODUCTION_R1_STYLE'
            )
        ) {
            return;
        }


        const style =
            document.createElement(
                'style'
            );

        style.id =
            'S18_TITLE_PICKER_PRODUCTION_R1_STYLE';


        style.textContent = `
#titlePickerPopup{
    position:fixed;
    z-index:99998;
    display:none;
    flex-direction:column;
    box-sizing:border-box;
    width:350px;
    max-width:calc(100vw - 16px);
    max-height:calc(100vh - 16px);
    min-height:0;
    overflow:hidden;
    padding:10px;
    border:1px solid var(--card-line);
    border-radius:6px;
    background:var(--paper2);
    color:var(--ink);
    box-shadow:0 10px 32px rgba(0,0,0,.34);
    font:12px 'Malgun Gothic',sans-serif
}

#titlePickerPopup.open{
    display:flex
}

#titlePickerPopup > :not(.title-picker-list){
    flex-shrink:0
}

#tullTitleConditionBubble{
    position:fixed;
    z-index:99999;
    box-sizing:border-box;
    padding:9px 11px;
    border:1px solid var(--gold);
    border-radius:5px;
    background:var(--paper2);
    color:var(--ink);
    box-shadow:0 7px 20px rgba(0,0,0,.35);
    font:12px/1.55 'Malgun Gothic',sans-serif;
    white-space:pre-wrap;
    overflow-wrap:anywhere;
    max-height:min(55vh,360px);
    overflow:hidden;
    pointer-events:none
}
#tullTitleConditionBubble[hidden]{display:none}

.title-picker-head,
.title-picker-foot{
    display:flex;
    align-items:center;
    gap:8px
}

.title-picker-head{
    justify-content:space-between;
    margin-bottom:8px
}

.title-picker-head strong{
    font-size:14px
}

.title-picker-head button{
    min-width:28px
}

#titlePickerPopup input,
#titlePickerPopup button{
    box-sizing:border-box;
    border:1px solid var(--card-line);
    border-radius:4px;
    background:var(--card-bg);
    color:var(--ink);
    font:inherit
}

#titlePickerPopup input{
    width:100%;
    margin-bottom:8px;
    padding:7px 8px
}

#titlePickerPopup button:not(:disabled){
    cursor:pointer
}

.title-picker-list{
    display:flex;
    flex:0 1 auto;
    flex-direction:column;
    gap:3px;
    min-height:0;
    max-height:none;
    overflow-y:auto;
    overflow-x:hidden
}

.title-picker-list button{
    display:flex;
    justify-content:space-between;
    align-items:center;
    gap:10px;
    width:100%;
    padding:7px 8px;
    text-align:left;
    border-color:transparent;
    background:rgba(255,249,226,.34)
}

.title-picker-list button:hover:not(:disabled),
.title-picker-list button.current{
    border-color:var(--gold);
    background:var(--paper3)
}

.title-picker-list button:disabled{
    opacity:.56;
    cursor:not-allowed
}

.title-picker-list button[aria-disabled="true"]{
    opacity:.56;
    cursor:help
}
.title-picker-list button[aria-disabled="true"]:hover,
.title-picker-list button[aria-disabled="true"]:focus-visible{
    border-color:var(--gold)
}

.title-picker-filters{
    display:flex;
    flex-direction:row;
    flex-wrap:nowrap;
    align-items:center;
    margin:0 0 8px;
    font-size:11px
}

.title-picker-filters label{
    display:inline-flex;
    flex:0 0 auto;
    align-items:center;
    column-gap:5px;
    white-space:nowrap;
    line-height:14px;
    cursor:pointer
}

.title-picker-filters label + label{
    margin-left:18px
}

.title-picker-filters input[type="checkbox"]{
    flex:0 0 14px;
    width:14px;
    height:14px;
    margin:0;
    padding:0
}

.title-picker-row-right{
    display:flex;
    gap:7px;
    align-items:center;
    white-space:nowrap
}

.title-picker-star{
    font-size:16px;
    line-height:1;
    cursor:pointer;
    color:var(--muted)
}

.title-picker-star.on{
    color:var(--gold)
}

.title-picker-list small{
    white-space:nowrap;
    color:var(--muted)
}

.title-picker-status{
    min-height:16px;
    margin-top:6px;
    color:var(--muted);
    font-size:11px
}

.title-picker-foot{
    justify-content:center;
    margin-top:8px
}

.title-picker-pages{
    display:flex;
    align-items:center;
    justify-content:center;
    gap:3px
}

.title-picker-pages button{
    min-width:25px;
    padding:3px 5px
}

.title-picker-pages button.current{
    border:2px solid var(--gold);
    background:var(--gold);
    color:var(--ink);
    box-shadow:inset 0 0 0 1px var(--ink);
    font-weight:800
}

.title-picker-empty{
    padding:18px 8px;
    text-align:center;
    color:var(--muted)
}

#titleGridSurface .title-internal-block{
    cursor:pointer
}
`;

        document.head.append(
            style
        );
    }


    function ensurePopup()
    {
        if (popup) {
            return popup;
        }


        popup =
            document.createElement(
                'div'
            );

        popup.id =
            'titlePickerPopup';


        popup.innerHTML = `
<div class="title-picker-head">
    <strong data-title></strong>
    <button
        type="button"
        data-close
        aria-label="닫기">×</button>
</div>

<input
    type="search"
    data-search
    placeholder="칭호 ID / 획득 칭호명 검색">

<div class="title-picker-filters">
    <label>
        <input
            type="checkbox"
            data-acquired-only>
        획득만
    </label>

    <label>
        <input
            type="checkbox"
            data-favorite-only>
        즐겨찾기만
    </label>
</div>

<div
    class="title-picker-list"
    data-list>
</div>

<div
    class="title-picker-status"
    data-status>
</div>

<div class="title-picker-foot">
    <button
        type="button"
        data-prev>←</button>

    <span
        class="title-picker-pages"
        data-pages>
    </span>

    <button
        type="button"
        data-next>→</button>
</div>
`;


        document.body.append(
            popup
        );
        window.TULLImeControls?.attach(popup);

        popup.querySelector('[data-list]')
            .addEventListener('scroll', hideConditionBubble);
        window.addEventListener('resize', hideConditionBubble);

        // QA4-F: wheel paging belongs only to the picker list, never the popup footer.
        // No polling/timer; a bounded event-time throttle keeps touchpads from skipping pages.
        let wheelLastPageAt = 0;
        popup.querySelector('[data-list]').addEventListener('wheel', event => {
            if (!popup?.classList.contains('open') || event.ctrlKey || event.shiftKey)
                return;
            const slot = currentSlot();
            const state = stateSnapshot();
            if (!slot || !state || !catalogTitles().length) return;
            const pageCount = Math.max(1, Math.ceil(rows(slot, state).length / pageSize()));
            if (pageCount < 2 || !Number.isFinite(event.deltaY) || event.deltaY === 0)
                return;
            event.preventDefault();
            const now = Date.now();
            if (now - wheelLastPageAt < 170) return;
            const next = Math.max(1, Math.min(pageCount, page + (event.deltaY > 0 ? 1 : -1)));
            if (next === page) return;
            page = next;
            wheelLastPageAt = now;
            hideConditionBubble();
            render();
        }, {passive:false});


        popup.querySelector(
            '[data-close]'
        )
        .addEventListener(
            'click',
            close
        );


        popup.querySelector(
            '[data-search]'
        )
        .addEventListener(
            'input',
            event => {
                if (window.TULLImeControls?.isComposing(event.target)) return;

                query =
                    event.target.value;

                setPageForCurrent();

                render();
            }
        );


        popup.querySelector('[data-search]')
            .addEventListener('compositionend', event => {
                query = event.target.value;
                setPageForCurrent();
                render();
            });

        /*
            Registered once here.
            Never re-register inside render().
        */
        popup.querySelector(
            '[data-acquired-only]'
        )
        .addEventListener(
            'change',
            event => {

                acquiredOnly =
                    event.target.checked;

                setPageForCurrent();

                render();
            }
        );


        popup.querySelector(
            '[data-favorite-only]'
        )
        .addEventListener(
            'change',
            event => {

                favoriteOnly =
                    event.target.checked;

                setPageForCurrent();

                render();
            }
        );


        popup.querySelector(
            '[data-prev]'
        )
        .addEventListener(
            'click',
            () => {

                page =
                    Math.max(
                        1,
                        page - 1
                    );

                render();
            }
        );


        popup.querySelector(
            '[data-next]'
        )
        .addEventListener(
            'click',
            () => {

                page += 1;

                render();
            }
        );


        popup.querySelector(
            '[data-pages]'
        )
        .addEventListener(
            'click',
            event => {

                const button =
                    event.target.closest(
                        '[data-page-number]'
                    );


                if (!button) {
                    return;
                }


                page =
                    Number(
                        button.dataset
                            .pageNumber
                    );

                render();
            }
        );


        popup.querySelector(
            '[data-list]'
        )
        .addEventListener(
            'click',
            event => {

                const star =
                    event.target.closest(
                        '[data-favorite-title]'
                    );


                if (star) {
                    event.preventDefault();
                    event.stopPropagation();


                    sendTitle(
                        `title.favorite.${
                            Number(
                                star.dataset
                                    .favoriteTitle
                            )
                        }`,
                        {
                            kind:'favorite'
                        }
                    );

                    return;
                }


                const row =
                    event.target.closest(
                        '[data-row]'
                    );


                if (
                    !row ||
                    row.disabled ||
                    row.dataset.unowned === 'true'
                ) {
                    return;
                }


                sendTitle(
                    `title.select.${
                        slotNumber
                    }.${
                        Number(
                            row.dataset.id
                        )
                    }.${
                        Number(
                            row.dataset.segment
                        )
                    }`,
                    {
                        kind:'select',
                        slot:slotNumber,
                        id:
                            Number(
                                row.dataset.id
                            ),
                        segment:
                            Number(
                                row.dataset.segment
                            )
                    }
                );
            }
        );


        return popup;
    }


    /*
        No optimistic semantic mutation.

        UI
        -> tullTitleCommand
        -> DLL TitleRuntime
        -> title-state
        -> UI
    */
    function sendTitle(
        command,
        action)
    {
        if (
            pending ||
            typeof tullTitleCommand !==
                'function'
        ) {
            return false;
        }


        pending =
            true;

        pendingStateRef =
            titleState;

        pendingAction =
            action;

        statusText =
            'DLL 응답 대기 중…';


        render();


        try {
            tullTitleCommand(
                command
            );

            return true;
        }
        catch {
            pending =
                false;

            pendingStateRef =
                null;

            pendingAction =
                null;

            statusText =
                '칭호 명령을 전달하지 못했습니다.';

            render();

            return false;
        }
    }


    function reconcilePending()
    {
        if (
            !pending ||
            !titleState ||
            titleState ===
                pendingStateRef
        ) {
            return;
        }


        const action =
            pendingAction;


        pending =
            false;

        pendingStateRef =
            null;

        pendingAction =
            null;

        statusText =
            '';


        if (
            action?.kind ===
            'select'
        ) {
            const slot =
                titleState.slots?.[
                    action.slot -
                    1
                ];


            if (
                slot &&
                Number(
                    slot.titleID
                ) ===
                    action.id &&

                Number(
                    slot.segmentIndex
                ) ===
                    action.segment
            ) {
                close();

                return;
            }


            statusText =
                'DLL이 해당 칭호 선택을 적용하지 않았습니다.';
        }
    }


    function close()
    {
        if (!popup) {
            return;
        }


        hideConditionBubble();

        popup.classList.remove(
            'open'
        );

        slotNumber =
            0;

        anchorBlock =
            null;

        page =
            1;

        query =
            '';

        statusText =
            '';

        pending =
            false;

        pendingStateRef =
            null;

        pendingAction =
            null;


        const input =
            popup.querySelector(
                '[data-search]'
            );


        if (input) {
            input.value =
                '';
        }
    }


    function positionPopup()
    {
        if (
            !popup?.classList.contains(
                'open'
            ) ||
            !anchorBlock?.isConnected
        ) {
            return;
        }


        const rect =
            anchorBlock
                .getBoundingClientRect();


        const width =
            Math.min(
                350,
                window.innerWidth -
                16
            );


        popup.style.width =
            `${width}px`;


        popup.style.left =
            `${Math.max(
                8,
                Math.min(
                    rect.left,
                    window.innerWidth -
                    width -
                    8
                )
            )}px`;


        const popupHeight = popup.offsetHeight;
        const below = rect.bottom + 6;
        const above = rect.top - popupHeight - 6;
        const fitsBelow =
            below + popupHeight <= window.innerHeight - 8;
        const fitsAbove = above >= 8;

        const preferredTop = fitsBelow ?
            below :
            (fitsAbove ?
                above :
                window.innerHeight - popupHeight - 8);

        popup.style.top =
            `${Math.max(
                8,
                Math.min(
                    preferredTop,
                    window.innerHeight - popupHeight - 8
                )
            )}px`;
    }


    function render()
    {
        if (
            !popup?.classList.contains(
                'open'
            )
        ) {
            return;
        }


        reconcilePending();


        if (
            !popup?.classList.contains(
                'open'
            )
        ) {
            return;
        }


        const state =
            stateSnapshot();

        const slot =
            currentSlot();


        if (
            !state ||
            !slot ||
            !catalogTitles().length
        ) {
            close();

            return;
        }


        const items =
            rows(
                slot,
                state
            );


        const size =
            pageSize();


        const pages =
            Math.max(
                1,
                Math.ceil(
                    items.length /
                    size
                )
            );


        page =
            Math.min(
                Math.max(
                    1,
                    page
                ),
                pages
            );


        popup.querySelector(
            '[data-title]'
        )
        .textContent =
            `${
                roleLabels[
                    slot.role
                ] ||
                slot.role
            } 칭호 선택`;


        popup.querySelector(
            '[data-acquired-only]'
        )
        .checked =
            acquiredOnly;


        popup.querySelector(
            '[data-favorite-only]'
        )
        .checked =
            favoriteOnly;


        popup.querySelector(
            '[data-status]'
        )
        .textContent =
            statusText;


        const list =
            popup.querySelector(
                '[data-list]'
            );


        hideConditionBubble();
        list.replaceChildren();


        for (
            const item of
            items.slice(
                (
                    page -
                    1
                ) *
                size,

                page *
                size
            )
        ) {
            const button =
                document.createElement(
                    'button'
                );


            button.type =
                'button';

            button.dataset.row =
                '';

            button.dataset.id =
                String(
                    item.id
                );

            button.dataset.segment =
                String(
                    item.segment
                );

            // Locked titles: inspectable but never selectable.
            button.disabled = pending;
            if (!item.special && !item.owned) {
                button.dataset.unowned = 'true';
                button.setAttribute('aria-disabled', 'true');
            }

            if (!item.special && !item.custom) {
                const condition = String(
                    item.conditionText || '').trim();

                button.addEventListener('mouseenter',
                    () => showConditionBubble(button, condition));
                button.addEventListener(
                    'mouseleave', hideConditionBubble);
                button.addEventListener('focus',
                    () => showConditionBubble(button, condition));
                button.addEventListener(
                    'blur', hideConditionBubble);
            }


            if (
                Number(
                    slot.titleID
                ) ===
                    item.id &&

                Number(
                    slot.segmentIndex
                ) ===
                    item.segment
            ) {
                button.classList.add(
                    'current'
                );
            }


            const label =
                document.createElement(
                    'span'
                );


            label.textContent =
                item.text;


            const meta =
                document.createElement(
                    'small'
                );


            meta.textContent =
                item.custom ?
                    `${item.favorite ? '★ ' : ''}${item.code}` :
                item.special ?
                    item.specialLabel :

                item.owned ?
                    `${
                        item.favorite ?
                            '★ ' :
                            ''
                    }#${
                        String(
                            item.id
                        ).padStart(
                            4,
                            '0'
                        )
                    }` :

                    `#${
                        String(
                            item.id
                        ).padStart(
                            4,
                            '0'
                        )
                    } · 조건 미달`;


            const right =
                document.createElement(
                    'span'
                );


            right.className =
                'title-picker-row-right';


            if (
                item.owned &&
                !item.special
            ) {
                const star =
                    document.createElement(
                        'span'
                    );


                star.className =
                    'title-picker-star' +
                    (
                        item.favorite ?
                            ' on' :
                            ''
                    );


                star.dataset
                    .favoriteTitle =
                    String(
                        item.id
                    );


                star.setAttribute(
                    'role',
                    'button'
                );


                star.textContent =
                    item.favorite ?
                        '★' :
                        '☆';


                star.title =
                    item.favorite ?
                        '즐겨찾기 해제' :
                        '즐겨찾기';


                if (pending) {
                    star.style
                        .pointerEvents =
                        'none';
                }


                right.append(
                    star
                );
            }


            right.append(
                meta
            );


            button.append(
                label,
                right
            );


            list.append(
                button
            );
        }


        if (!items.length) {
            const empty =
                document.createElement(
                    'div'
                );


            empty.className =
                'title-picker-empty';


            empty.textContent =
                '표시할 칭호가 없습니다.';


            list.append(
                empty
            );
        }


        const pageHost =
            popup.querySelector(
                '[data-pages]'
            );


        pageHost.replaceChildren();


        const first =
            Math.max(
                1,
                Math.min(
                    page -
                    2,
                    Math.max(
                        1,
                        pages -
                        4
                    )
                )
            );


        const last =
            Math.min(
                pages,
                first +
                4
            );


        for (
            let number = first;
            number <= last;
            number += 1
        ) {
            const button =
                document.createElement(
                    'button'
                );


            button.type =
                'button';

            button.dataset
                .pageNumber =
                String(
                    number
                );

            button.textContent =
                String(
                    number
                );

            button.disabled =
                pending;


            if (
                number ===
                page
            ) {
                button.classList.add(
                    'current'
                );
            }


            if (number === page) button.setAttribute('aria-current','page');
            pageHost.append(
                button
            );
        }


        popup.querySelector(
            '[data-prev]'
        )
        .disabled =
            pending ||
            page <= 1;


        popup.querySelector(
            '[data-next]'
        )
        .disabled =
            pending ||
            page >= pages;


        positionPopup();
    }


    function open(
        slot,
        block)
    {
        const state =
            stateSnapshot();


        const target =
            state?.slots?.[
                slot -
                1
            ];


        if (
            !state ||
            !catalogTitles().length ||
            typeof tullTitleCommand !==
                'function' ||
            pending ||
            target?.active !== true
        ) {
            return;
        }


        ensurePopup();


        slotNumber =
            slot;

        anchorBlock =
            block;

        query =
            '';

        statusText =
            '';


        popup.querySelector(
            '[data-search]'
        )
        .value =
            '';


        popup.classList.add(
            'open'
        );


        setPageForCurrent();

        render();


        popup.querySelector(
            '[data-search]'
        )
        .focus();
    }


    function handleTitleRender()
    {
        reconcilePending();


        if (
            popup?.classList.contains(
                'open'
            )
        ) {
            if (
                !anchorBlock?.isConnected
            ) {
                anchorBlock =
                    document.querySelector(
                        `#titleGridSurface .title-internal-block[data-slot="${slotNumber}"]`
                    );
            }


            render();
        }
    }


    ensureStyle();


    /*
        Event delegation:
        title blocks are recreated by renderTitleSection(),
        so listeners are not attached per block.
    */
    document.addEventListener(
        'click',
        event => {

            const block =
                event.target.closest(
                    '#titleGridSurface .title-internal-block'
                );


            if (
                !block ||
                document.querySelector(
                    '#titleInternalModal[open]'
                )
            ) {
                return;
            }


            const slot =
                Number(
                    block.dataset.slot
                );


            if (
                !Number.isInteger(
                    slot
                ) ||
                slot < 1 ||
                slot > 6
            ) {
                return;
            }


            event.preventDefault();

            event.stopPropagation();


            open(
                slot,
                block
            );
        },
        true
    );


    document.addEventListener(
        'pointerdown',
        event => {

            if (
                popup?.classList.contains(
                    'open'
                ) &&

                !popup.contains(
                    event.target
                ) &&

                !event.target.closest(
                    '#titleGridSurface .title-internal-block'
                )
            ) {
                close();
            }
        },
        true
    );


    /*
        Capture Escape before main UI close.
    */
    window.addEventListener(
        'keydown',
        event => {

            if (
                event.key ===
                    'Escape' &&

                popup?.classList.contains(
                    'open'
                )
            ) {
                event.preventDefault();

                event.stopImmediatePropagation();

                if (window.TULLImeControls?.blocksEscape()) return;
                close();
            }
        },
        true
    );


    window.addEventListener(
        'resize',
        () => {

            if (
                popup?.classList.contains(
                    'open'
                )
            ) {
                setPageForCurrent();

                render();
            }
        }
    );


    /*
        Keep existing render chain.
        A2-2 Title Authoring remains the inner wrapper.
    */
    const priorRenderTitleSection =
        renderTitleSection;


    renderTitleSection =
        function()
        {
            priorRenderTitleSection();

            handleTitleRender();
        };


    window.TULLTitlePickerProductionR1 =
        Object.freeze({
            close,

            refresh:
                render,

            isOpen:
                () =>
                    popup?.classList.contains(
                        'open'
                    ) === true
        });
})();