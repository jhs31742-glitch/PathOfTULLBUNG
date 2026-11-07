(() => {
  'use strict';
  if (window.TULLUIExternalImportPlanR1) return;
  const profile = window.TULLUIExternalProfileContractR1;
  const browser = window.TULLUIBrowserBackupR2;
  const obj = v => v !== null && typeof v === 'object' && !Array.isArray(v);
  const has = (o, k) => Object.prototype.hasOwnProperty.call(o, k);
  const same = (a, b) => a === b;
  const fail = reason => ({ok:false,reason});
  function plan(raw, current) {
    if (!profile || !browser || !obj(current) || !obj(current.browser) ||
        !obj(current.native) || !obj(current.titleState)) return fail('snapshot-missing');
    const parsed = profile.parseBundle(raw);
    if (!parsed.ok) return fail('profile-' + parsed.reason);
    const incoming = parsed.bundle;
    const keys = browser.keys();
    const nativeKeys = profile.nativeKeys();
    if (keys.length !== 16 || nativeKeys.length !== 18 ||
        Object.keys(current.browser).length !== keys.length ||
        !keys.every(k => has(current.browser,k) && browser.valid(k,current.browser[k])))
      return fail('current-browser-invalid');
    const ts = current.titleState;
    if (!Array.isArray(ts.customTitles) || ts.customTitles.length !== 20 ||
        !Array.isArray(ts.slots) || ts.slots.length !== 6 ||
        !Array.isArray(ts.favoriteTitleIDs) ||
        !ts.favoriteTitleIDs.every(x => Number.isInteger(x) && x >= 0 && x <= 65535))
      return fail('current-titles-invalid');
    for (let i = 0; i < 20; i++) {
      const t = ts.customTitles[i];
      if (!obj(t) || t.id !== 1000+i || t.code !== `C${String(i).padStart(2,'0')}` ||
          typeof t.present !== 'boolean' || typeof t.favorite !== 'boolean' ||
          typeof t.before !== 'string' || typeof t.after !== 'string')
        return fail('current-titles-invalid');
    }
    const selected = new Set();
    for (const slot of ts.slots) {
      if (!obj(slot) || !Number.isInteger(slot.titleID) ||
          slot.titleID < 0 || slot.titleID > 65535) return fail('current-slots-invalid');
      if (slot.titleID >= 1000 && slot.titleID < 1020) selected.add(slot.titleID);
    }
    const favorite = new Set(ts.favoriteTitleIDs.filter(id => id >= 1000 && id < 1020));
    for (const t of ts.customTitles) if (t.favorite) favorite.add(t.id);
    const baseline = profile.exportBundle(current.browser,current.native,ts);
    if (!baseline.ok) return fail('current-contract-invalid');
    const browserWrites = [];
    for (const key of keys) if (!same(current.browser[key],incoming.browser.values[key]))
      browserWrites.push({key,before:current.browser[key],after:incoming.browser.values[key]});
    const nativeWrites = [];
    for (const key of nativeKeys) if (!same(current.native[key],incoming.native[key]))
      nativeWrites.push({key,before:current.native[key],after:incoming.native[key]});
    const titleWrites = [];
    for (let i=0;i<20;i++) {
      const prior=ts.customTitles[i], next=incoming.customTitles[i];
      if (prior.present && !next.present && (selected.has(prior.id) || favorite.has(prior.id)))
        return fail('protected-title-deletion:' + next.code);
      if (prior.present === next.present && prior.before === next.before && prior.after === next.after)
        continue;
      titleWrites.push({code:next.code,id:1000+i,
        operation:next.present?(prior.present?'update':'create'):'delete',
        before:{present:prior.present,before:prior.before,after:prior.after},
        after:{present:next.present,before:next.before,after:next.after}});
    }
    return {ok:true,applied:false,
      counts:{browser:browserWrites.length,native:nativeWrites.length,titles:titleWrites.length},
      browserWrites,nativeWrites,titleWrites,
      preserved:{hotkeys:true,developerMode:true,progress:true,titleFavorites:true,titleSelections:true}};
  }
  window.TULLUIExternalImportPlanR1=Object.freeze({plan});
})();
