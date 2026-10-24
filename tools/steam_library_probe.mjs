// Default: read-only discovery. --verify creates/removes its own temporary collection;
// existing favorites/hidden flags are tested only with their unchanged current values.
import {readFile} from 'node:fs/promises';
import {randomUUID} from 'node:crypto';
const endpoint = 'http://127.0.0.1:8080';
const targets = await (await fetch(`${endpoint}/json/list`)).json();
const target = targets.find(t => t.title === 'SharedJSContext' && new URL(t.url).origin === 'https://steamloopback.host');
if (!target) throw new Error('Steam SharedJSContext is unavailable');
const socket = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
let expression = `(() => {
  const c = window.collectionStore, app = window.App, cloud = window.cloudStorageInternalState;
  const f = c?.GetCollection('favorite');
  const methods = {};
  for (const [owner, names] of [[c, ['SaveCollection','SetAppsAsHidden','SetAppsAsFavorite','GetCollection']],
                               [f, ['Save','AddApps','RemoveApps','ToStorageFormat']]])
    for (const name of names) methods[name] = typeof owner?.[name] === 'function' ? String(owner[name]) : null;
  return { servicesReady: app?.GetServicesInitialized?.(), loggedIn: app?.BHasCurrentUser?.(),
    accountAvailable: /^[1-9]\\d{16}$/.test(app?.GetCurrentUser?.()?.strSteamID || ''),
    appStoreReady: window.appStore?.m_bIsInitialized, collections: c?.collectionsFromStorage?.size,
    favoriteReady: !!f, hiddenReady: !!c?.GetCollection('hidden'),
    mutableSets: f?.internalAddedList instanceof Set && f?.internalRemovedList instanceof Set,
    cloudDirtyObservable: cloud?.m_mapDirtyKeys instanceof Map, methods };
})()`;
if (process.argv.includes('--creation-shape')) {
  expression = `(() => {
    const c = window.collectionStore.NewUnsavedCollection('Launchme unsaved shape probe', undefined, []);
    const fields = Object.getOwnPropertyNames(c).filter(k => /id/i.test(k)).map(k=>({key:k,type:typeof c[k],matchesID:c[k]===c.id}));
    const getters = [];
    for (let p=Object.getPrototypeOf(c);p;p=Object.getPrototypeOf(p)) {
      const d=Object.getOwnPropertyDescriptor(p,'id'); if(d?.get)getters.push(String(d.get));
    }
    return {fields,getters,stored:window.collectionStore.collectionsFromStorage.has(c.id)};
  })()`;
}
if (process.argv.includes('--test-collections')) {
  expression = `([...window.collectionStore.collectionsFromStorage.values()].filter(c=>c.displayName.startsWith('Launchme sync check ')).map(c=>({id:c.id,name:c.displayName})))`;
}
if (process.argv.includes('--verify')) {
  const header = await readFile(new URL('../src/platform/steam_library_bridge.hpp', import.meta.url), 'utf8');
  const source = header.split('R"STEAM_BRIDGE(')[1].split(')STEAM_BRIDGE"')[0];
  const name = 'Launchme sync check ' + randomUUID();
  const nativeID = 'uc-launchme-' + randomUUID().replaceAll('-', '');
  expression = `(async () => {
    const bridge = (${source});
    const account = window.App.GetCurrentUser().strSteamID;
    const baseline = await bridge({account,changes:[]});
    if (!baseline.ok) return {stage:'read',error:baseline.error};
    const store = window.collectionStore;
    const appid = baseline.collections.flatMap(c=>c.members).find(id => {
      const o = window.appStore.GetAppOverviewByAppID(id); return id < 0x80000000 && o && !o.BIsModOrShortcut();
    });
    if (!appid) return {stage:'fixture-app-unavailable'};
    const nativeID = ${JSON.stringify(nativeID)};
    let stage = 'create', result, cleanup = false;
    try {
      const id = 'user-collections.' + nativeID;
      result = await bridge({account,changes:[{kind:'create',collection:id,name:${JSON.stringify(name)}}]});
      if (!result.ok || result.ack.length !== 1) return {stage,error:result.error || 'unconfirmed'};
      const count = store.collectionsFromStorage.size;
      result = await bridge({account,changes:[{kind:'create',collection:id,name:${JSON.stringify(name)}}]});
      if (!result.ok || result.ack.length !== 1 || store.collectionsFromStorage.size !== count) return {stage:'retry-created-duplicate'};
      for (const change of [
        {kind:'member',collection:id,app:appid,value:true},
        {kind:'name',collection:id,name:${JSON.stringify(name + ' — test "nazwy"')}},
        {kind:'member',collection:id,app:appid,value:false},
        {kind:'member',collection:'user-collections.favorite',app:appid,value:store.BIsFavorite(appid)},
        {kind:'hidden',app:appid,value:store.BIsHidden(appid)}
      ]) {
        stage = change.kind;
        result = await bridge({account,changes:[change]});
        if (!result.ok || result.ack.length !== 1) return {stage,error:result.error || 'unconfirmed'};
      }
      stage = 'verified';
    } finally {
      if (window.App.GetCurrentUser().strSteamID === account) {
        if (store.collectionsFromStorage.has(nativeID)) await store.DeleteCollection(nativeID);
        for (let attempt=0; attempt<30; ++attempt) {
          const entries = new Map(await (await fetch('/cloudstorage/cloud-storage-namespace-1.json',{cache:'no-store'})).json());
          const entry = entries.get('user-collections.' + nativeID);
          if (!store.collectionsFromStorage.has(nativeID) && (!entry || entry.is_deleted)) { cleanup = true; break; }
          await new Promise(resolve=>setTimeout(resolve,100));
        }
      }
      if (!cleanup) throw new Error('Temporary collection cleanup unconfirmed: ' + nativeID);
    }
    return {stage,cleanup,existingFavoriteUnchanged:store.BIsFavorite(appid) === baseline.collections.find(c=>c.id==='user-collections.favorite').members.includes(appid),
      existingHiddenUnchanged:store.BIsHidden(appid) === baseline.hidden.includes(appid)};
  })()`;
}
const timer = setTimeout(() => { socket.close(); throw new Error('Steam probe timeout'); }, 45000);
try {
  const response = await new Promise((resolve, reject) => {
    socket.onmessage = event => { const r = JSON.parse(event.data); if (r.id === 1) resolve(r); };
    socket.onerror = reject;
    socket.send(JSON.stringify({id: 1, method: 'Runtime.evaluate', params: {expression, returnByValue: true, awaitPromise: true}}));
  });
  if (response.error || response.result?.exceptionDetails) {
    console.error(response.error?.message || response.result.exceptionDetails.exception?.description || response.result.exceptionDetails.text);
    throw new Error('Steam interface inspection failed');
  }
  console.log(JSON.stringify(response.result.result.value, null, 2));
} finally { clearTimeout(timer); socket.close(); }
