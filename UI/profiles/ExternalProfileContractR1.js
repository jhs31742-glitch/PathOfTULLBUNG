(() => {
  'use strict';
  if (window.TULLUIExternalProfileContractR1) return;
  const FORMAT = 'TULLBUNG_UI_ONLY_PROFILE';
  const MAX_BYTES = 1000000;
  const MAX_KEYS = 16;
  const NATIVE = Object.freeze([
    'themeId', 'uiBackgroundOpacity', 'hudMaster',
    ...['Void','Composure','Quiet'].flatMap(k =>
      ['Visible','X','Y','Scale','Opacity'].map(f => `hud${k}${f}`))
  ]);
  const isObj = v => v !== null && typeof v === 'object' && !Array.isArray(v);
  const exact = (v, keys) => isObj(v) &&
    Object.keys(v).length === keys.length &&
    keys.every(k => Object.prototype.hasOwnProperty.call(v, k));
  const finiteRange = (v, low, high) =>
    typeof v === 'number' && Number.isFinite(v) && v >= low && v <= high;
  function utf8Length(s) {
    let bytes = 0;
    for (let i = 0; i < s.length; i++) {
      const c = s.charCodeAt(i);
      if (c <= 0x7F) bytes += 1;
      else if (c <= 0x7FF) bytes += 2;
      else if (c >= 0xD800 && c <= 0xDBFF) {
        if (++i >= s.length) return -1;
        const d = s.charCodeAt(i);
        if (d < 0xDC00 || d > 0xDFFF) return -1;
        bytes += 4;
      } else if (c >= 0xDC00 && c <= 0xDFFF) return -1;
      else bytes += 3;
    }
    return bytes;
  }
  function checkNative(v) {
    if (!exact(v, NATIVE)) return false;
    if (!['default','dark-gold'].includes(v.themeId) ||
        !finiteRange(v.uiBackgroundOpacity,0,1) ||
        typeof v.hudMaster !== 'boolean') return false;
    for (const k of ['Void','Composure','Quiet']) {
      if (typeof v[`hud${k}Visible`] !== 'boolean' ||
          !finiteRange(v[`hud${k}X`],0,1) ||
          !finiteRange(v[`hud${k}Y`],0,1) ||
          !finiteRange(v[`hud${k}Scale`],0.5,2) ||
          !finiteRange(v[`hud${k}Opacity`],0,1)) return false;
    }
    return true;
  }
  function checkTitles(t) {
    if (!Array.isArray(t) || t.length !== 20) return false;
    return t.every((r,i) => {
      if (!exact(r,['code','present','before','after']) ||
          r.code !== `C${String(i).padStart(2,'0')}` ||
          typeof r.present !== 'boolean' ||
          typeof r.before !== 'string' || typeof r.after !== 'string' ||
          utf8Length(r.before) > 1024 || utf8Length(r.after) > 1024 ||
          utf8Length(r.before) < 0 || utf8Length(r.after) < 0) return false;
      if (!r.present) return r.before === '' && r.after === '';
      const visible = s => /\S/.test(s);
      return i < 10 ? visible(r.before) && r.after === '' :
        visible(r.before) || visible(r.after);
    });
  }
  function check(bundle) {
    const core = window.TULLUIBrowserBackupR2;
    if (!core || !exact(bundle,['format','schema','browser','native','customTitles']) ||
        bundle.format !== FORMAT || bundle.schema !== 1 ||
        !exact(bundle.browser,['schema','values']) || bundle.browser.schema !== 2 ||
        !isObj(bundle.browser.values) || !checkNative(bundle.native) ||
        !checkTitles(bundle.customTitles)) return false;
    const keys = core.keys();
    if (keys.length !== MAX_KEYS || !exact(bundle.browser.values,keys)) return false;
    return keys.every(k => core.valid(k,bundle.browser.values[k]));
  }
  function exportBundle(browserValues, settingsSnapshot, titleSnapshot) {
    const core = window.TULLUIBrowserBackupR2;
    if (!core || !isObj(browserValues) || !isObj(settingsSnapshot) ||
        !isObj(titleSnapshot) || !Array.isArray(titleSnapshot.customTitles))
      return {ok:false,reason:'source-missing'};
    const values = Object.create(null);
    for (const k of core.keys()) values[k] = browserValues[k];
    const native = Object.create(null);
    for (const k of NATIVE) native[k] = settingsSnapshot[k];
    const customTitles = titleSnapshot.customTitles.map((t,i) => ({
      code:`C${String(i).padStart(2,'0')}`,
      present:t?.present,
      before:t?.before,
      after:t?.after
    }));
    const bundle = {format:FORMAT,schema:1,browser:{schema:2,values},native,customTitles};
    if (!check(bundle)) return {ok:false,reason:'invalid-source'};
    const raw = JSON.stringify(bundle,null,2) + '\n';
    if (utf8Length(raw) > MAX_BYTES) return {ok:false,reason:'too-large'};
    return {ok:true,raw};
  }
  function parseBundle(raw) {
    if (typeof raw !== 'string' || utf8Length(raw) < 0 ||
        utf8Length(raw) > MAX_BYTES) return {ok:false,reason:'size-or-encoding'};
    let bundle;
    try { bundle = JSON.parse(raw); }
    catch (_) { return {ok:false,reason:'json-parse'}; }
    if (!check(bundle)) return {ok:false,reason:'schema-or-content'};
    return {ok:true,bundle};
  }
  window.TULLUIExternalProfileContractR1 = Object.freeze({
    nativeKeys:()=>[...NATIVE], exportBundle, parseBundle
  });
})();
