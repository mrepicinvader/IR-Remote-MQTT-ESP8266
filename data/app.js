'use strict';
const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
let remotes = [], currentSignal = null, learning = false, learnTimer, toastTimer, config = {};
let batch = null, batchTimer, commandTimer, firmwareLimit = 0, firmwareEnabled = true, firmwareUploading = false;
let firmwareTarget='IRBRIDGE:ESP8266:4M1M:v1';
const icons = {power:'⏻',up:'↑',down:'↓',left:'←',right:'→',ok:'●',volume_up:'＋',volume_down:'−',mute:'🔇',play:'▶',pause:'Ⅱ',stop:'■',tv:'▣',fan:'✺',light:'☀',source:'↪'};
const commandLabels = {queued:'In Warteschlange',sent:'Gesendet',failed:'Fehlgeschlagen'};
const path = location.pathname;
const page = path === '/monitor' ? 'monitor' : path === '/diagnostics' ? 'diagnostics' : path === '/settings' ? 'settings' : 'home';
$(`#${page}`).hidden = false;
$(`nav [data-page="${page}"]`).classList.add('active');

function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}
function toast(text, failed = false) {
  const box = $('#toast'); box.textContent = text; box.hidden = false; box.classList.toggle('failed', failed);
  clearTimeout(toastTimer); toastTimer = setTimeout(() => box.hidden = true, failed ? 7000 : 3000);
}
async function api(url, method = 'GET', payload) {
  const options = {method, cache: 'no-store'};
  if (payload !== undefined) { options.headers = {'Content-Type':'application/json'}; options.body = JSON.stringify(payload); }
  const response = await fetch(url, options);
  if (response.status === 401) throw new Error('Anmeldung erforderlich. Seite neu laden und anmelden.');
  const result = await response.json();
  if (!response.ok || result.success === false) throw new Error(result.error || `HTTP ${response.status}`);
  return result;
}
function action(label, fn, className = 'secondary') {
  const button = element('button', className, label); button.type = 'button';
  button.addEventListener('click', async () => { button.disabled = true; try { await fn(); } catch (e) { toast(e.message, true); } finally { button.disabled = false; } });
  return button;
}
function details(target, values) {
  target.replaceChildren();
  for (const [name, value] of values) {
    const row = element('div'); row.append(element('dt', '', name),element('dd', '', value ?? '—')); target.append(row);
  }
}
function uptime(ms) { const seconds = Math.floor(ms / 1000); return `${Math.floor(seconds/86400)}d ${Math.floor(seconds%86400/3600)}h ${Math.floor(seconds%3600/60)}m`; }
function ago(timestamp, now) { return timestamp ? `bei ${uptime(timestamp)} Uptime · vor ${Math.max(0,Math.floor((now-timestamp)/1000))} s` : 'Noch nicht'; }
async function status() {
  if (firmwareUploading) return;
  try {
    const s = await api('/api/status');
    $('#connection').textContent = s.ap_active ? 'Setup AP' : s.wifi_connected ? 'Online' : 'WLAN getrennt';
    $('#connection').classList.toggle('online', s.wifi_connected);
    const stats = [['WiFi',s.wifi_connected ? 'Verbunden':'Setup / getrennt'],['MQTT',s.mqtt_connected?'Verbunden':'Getrennt'],['IP',s.ip],['RSSI',s.wifi_connected?`${s.rssi} dBm`:'—'],['Uptime',uptime(s.uptime_ms)],['Heap',`${Math.round(s.free_heap/1024)} KB`]];
    $('#stats').replaceChildren(...stats.map(([label,value]) => { const n=element('div','stat'); n.append(element('small','',label),element('strong','',value)); return n; }));
    $('#footer-version').textContent = `Firmware ${s.firmware_version}`;
    $('#firmware-info').textContent = `Version ${s.firmware_version} · OTA: ${s.ota_status}`;
    firmwareLimit=s.firmware_max_bytes || 0; firmwareEnabled=s.web_update_enabled !== false;
    firmwareTarget=s.firmware_target || firmwareTarget;
    $('#firmware-form button[type="submit"]').disabled=!firmwareEnabled;
    const warning = !s.storage_ready ? s.storage_error : s.ap_active ? `Konfiguration: ${s.ap_ssid} · 192.168.4.1. WLAN und MQTT unter Einstellungen eintragen.` : '';
    $('#notice').hidden = !warning; $('#notice').textContent = warning;
    details($('#diagnostic-details'),[
      ['Firmware',s.firmware_version],['Build',s.build_date],['Device ID (MAC)',s.device_id],['Hostname',s.hostname],
      ['Freier Heap',`${s.free_heap} Byte`],['Größter freier Block',`${s.max_free_block} Byte`],['LittleFS gesamt',`${s.littlefs_total ?? 0} Byte`],['LittleFS belegt',`${s.littlefs_used ?? 0} Byte`],
      ['Flash erkannt',`${s.flash_actual_bytes ?? 0} Byte`],['Flash konfiguriert',`${s.flash_configured_bytes ?? 0} Byte`],
      ['WiFi RSSI',`${s.rssi} dBm`],['WiFi Reconnects',s.wifi_reconnect_count],['MQTT Reconnects',s.mqtt_reconnect_count],['Uptime',uptime(s.uptime_ms)],
      ['Letztes IR RX',ago(s.last_ir_received_ms,s.uptime_ms)],['Letztes IR TX',ago(s.last_ir_transmitted_ms,s.uptime_ms)],['Letzter Sendemodus',s.last_send_mode || '—'],
      ['Remotes',`${s.remote_count} / ${s.max_remotes}`],['Tasten',`${s.button_count} / ${s.max_buttons}`],['RAW Empfangspuffer',s.rx_buffer_size],['RAW Speicherlimit',s.max_raw_samples],['OTA',s.ota_status],['Discovery',s.discovery_status || '—'],['Wartende Aufträge',s.queued_commands || 0],['Recovery-WLAN',s.ap_protected?'Passwortgeschützt':'—'],['Automatischer Fallback',s.ap_fallback_enabled?'Aktiv':'Deaktiviert']
    ]);
    $('#backup-panel').hidden = !s.backup_supported;
  } catch (e) { $('#connection').textContent = 'Bridge nicht erreichbar'; $('#connection').classList.remove('online'); }
}
function openRemote(remote = null) {
  const form = $('#remote-form'); form.reset(); form.elements.remote.value = remote?.id || '';
  form.elements.name.value = remote?.name || ''; form.elements.optimistic_switch.checked = remote?.optimistic_switch || false;
  form.elements.layout.value=remote?.layout || 'grid';
  $('#remote-title').textContent = remote ? 'Remote bearbeiten' : 'Remote hinzufügen';
  $('#optimistic-label').hidden = !remote; $('#remote-hint').hidden = !remote;
  $('#remote-dialog').showModal();
}
function remoteOptions(select, id = '') {
  select.replaceChildren(...remotes.map(r => { const option=element('option','',r.name); option.value=r.id; return option; }));
  if (id) select.value = id;
}
function openLearn(remoteId = '', button = null) {
  if (!remotes.length) { toast('Zuerst eine Remote anlegen.'); openRemote(); return; }
  const form=$('#learn-form'); form.reset(); remoteOptions(form.elements.remote,remoteId);
  form.elements.name.value=button?.name || ''; form.elements.replace_id.value=button?.id || '';
  if (button) { form.elements.entity_type.value=button.entity_type; form.elements.send_mode.value=button.send_mode; form.elements.repeat_count.value=button.repeat_count; }
  $('#learn-title').textContent=button?'Taste neu lernen':'Neue Taste lernen';
  $('#learn-progress').hidden=true; $('#learn-submit').disabled=false;
  $('#learn-dialog').showModal();
}
function requestId() { return globalThis.crypto?.randomUUID?.() || `web-${Date.now().toString(36)}-${Math.random().toString(36).slice(2,10)}`; }
async function waitCommand(id) {
  for (let attempt=0;attempt<900;attempt++) {
    const result=await api(`/api/commands?request_id=${encodeURIComponent(id)}`);
    if (result.status==='failed') throw new Error(result.error || 'IR-Ausgabe fehlgeschlagen');
    if (result.status==='sent') return result;
    await new Promise(resolve => setTimeout(resolve,500));
  }
  throw new Error('Auftrag läuft länger als erwartet. Unter Diagnose prüfen.');
}
async function submitCommand(payload,label) {
  const result=await api('/api/send','POST',{...payload,request_id:requestId()});
  toast(`${label} · In Warteschlange`);
  await waitCommand(result.request_id); toast(`${label} · Gesendet`);
}
async function send(remote,button) { await submitCommand({remote:remote.id,button:button.id},`${remote.name} · ${button.name}`); }
function menu(items) {
  const box=element('details','menu'); const summary=element('summary','','⋮'); summary.setAttribute('aria-label','Aktionen');
  const panel=element('div','menu-panel');
  for (const [label,fn] of items) panel.append(action(label,async () => { box.open=false; await fn(); },''));
  box.append(summary,panel); return box;
}
async function loadRemotes() {
  const data=await api('/api/remotes'); remotes=data.remotes;
  const grid=$('#remotes'); grid.replaceChildren();
  if (!remotes.length) {
    const empty=element('div','empty'); empty.append(element('h2','','Deine erste Fernbedienung'),element('p','','Lege zum Beispiel „Ventilator“ an und lerne die Tasten deiner Originalfernbedienung.'),action('+ Remote anlegen',() => openRemote(),'')); grid.append(empty);
  }
  for (const remote of remotes) {
    const card=element('article','card'); const header=element('div','remote-header'); const heading=element('div');
    heading.append(element('h2','',remote.name),element('p','',`${remote.buttons.length} gelernte Tasten${remote.optimistic_switch?' · Optimistic – real device state unknown':''}`));
    header.append(heading,menu([['Remote bearbeiten',() => openRemote(remote)],['Remote löschen',async () => { if (confirm(`Remote „${remote.name}“ mit allen Tasten löschen?`)) { await api(`/api/remotes/${remote.id}`,'DELETE'); await loadRemotes(); } }]]));
    const keys=element('div',`key-grid ${remote.layout || 'grid'}`), extras=element('div','key-grid extra-keys');
    for (const button of remote.buttons) {
      const key=makeKey(remote,button);
      const positioned=remote.layout==='directional' && button.slot || remote.layout==='media' && ['power','source','play','pause','stop','volume_up','volume_down','mute'].includes(button.slot);
      (remote.layout!=='grid' && !positioned ? extras : keys).append(key);
    }
    if (!remote.buttons.length) keys.append(element('p','muted','Noch keine Tasten gelernt.'));
    card.append(header,keys); if (extras.childElementCount) card.append(extras);
    const add=element('div','actions'); add.append(action('+ Taste lernen',() => openLearn(remote.id),'remote-add'),action('Tastenfolge',() => openBatch(remote.id),'remote-add'));
    card.append(add); grid.append(card);
  }
  const favorites=$('#favorites'); favorites.replaceChildren();
  for (const remote of remotes) for (const button of remote.buttons) if (button.favorite) favorites.append(action(`${icons[button.icon] || '★'} ${remote.name} · ${button.name}`,() => send(remote,button),'secondary'));
  $('#favorites-panel').hidden=!favorites.childElementCount;
  renderScenes();
}
function makeKey(remote,button) {
  const key=element('div',`key${button.slot?` slot-${button.slot}`:''}`);
  const label=`${icons[button.icon] ? icons[button.icon]+' ' : ''}${button.name}`;
  key.append(action(label,() => send(remote,button),'key-send'),menu([
    ['Senden',() => send(remote,button)],['Bearbeiten',() => openDetails(remote,button)],['Neu lernen',() => openLearn(remote.id,button)],
    [button.favorite?'Favorit entfernen':'Als Favorit',async () => { await api('/api/button','PATCH',{remote:remote.id,button:button.id,favorite:!button.favorite}); await loadRemotes(); }],
    ['Nach vorne',() => moveButton(remote,button,-1)],['Nach hinten',() => moveButton(remote,button,1)],
    ['Löschen',async () => { if (confirm(`Taste „${button.name}“ löschen?`)) { await api('/api/button','DELETE',{remote:remote.id,button:button.id}); await loadRemotes(); } }]
  ])); return key;
}
async function moveButton(remote,button,direction) {
  const order=remote.buttons.map(b => b.id), index=order.indexOf(button.id), next=index+direction;
  if (next<0 || next>=order.length) return;
  [order[index],order[next]]=[order[next],order[index]];
  await api('/api/remotes/layout','PATCH',{remote:remote.id,button_order:order}); await loadRemotes();
}
async function openDetails(remote,button) {
  const data=await api(`/api/button?remote=${remote.id}&button=${button.id}`); const form=$('#details-form');
  form.elements.remote.value=remote.id; form.elements.button.value=button.id;
  for (const field of ['name','entity_type','send_mode','frequency','repeat_count','icon','slot']) form.elements[field].value=data[field] ?? '';
  form.elements.favorite.checked=Boolean(data.favorite);
  details($('#button-details'),[['Protocol',data.protocol],['Bits',data.bits],['Address',data.address],['Command',data.command],['Value',data.value],['Raw length',data.raw_length],['Carrier frequency',`${data.frequency} Hz (${data.frequency_source})`],['Last transmitted',data.last_transmitted_ms?`bei ${uptime(data.last_transmitted_ms)} Uptime`:'Noch nicht'],['State bytes',data.state?data.state.length/2:0],['Dekodiert sicher',data.decoded_reliable?'Ja':'RAW in AUTO']]);
  $('#raw-download').href=`/api/button?remote=${remote.id}&button=${button.id}&raw=1`; $('#details-dialog').showModal();
}
async function learnStatus() {
  try {
    const s=await api('/api/learn/status'); learning=['waiting','receiving'].includes(s.status);
    const box=$('#learn-progress'); box.hidden=false; box.textContent=`${s.status.toUpperCase()} · ${s.captured}/${s.required}\n${s.message}${learning?`\nNoch ${Math.ceil(s.remaining_ms/1000)} s`:''}`;
    $('#learn-submit').disabled=learning;
    if (learning) learnTimer=setTimeout(learnStatus,700);
    else if (s.status==='saved') { toast('Taste dauerhaft gespeichert'); await loadRemotes(); }
  } catch (e) { toast(e.message,true); learning=false; $('#learn-submit').disabled=false; }
}
async function cancelLearn() { clearTimeout(learnTimer); await api('/api/learn/cancel','POST',{}); learning=false; $('#learn-dialog').close(); }
async function monitor() {
  if (page !== 'monitor') return;
  try {
    currentSignal=await api('/api/received/latest');
    $('#monitor-empty').hidden=currentSignal.available; $('#monitor-signal').hidden=!currentSignal.available;
    if (currentSignal.available) {
      const s=currentSignal.signal; $('#monitor-protocol').textContent=s.protocol==='RAW'?'UNKNOWN / RAW':s.protocol; $('#monitor-sequence').textContent=`Signal #${currentSignal.sequence}`;
      details($('#monitor-details'),[['Protocol',s.protocol],['Address',s.address],['Command',s.command],['Bits',s.bits],['Raw samples',s.raw_length],['Carrier',`${s.frequency} Hz · ${s.frequency_source}`],['Repeat frame',s.repeat?'Ja':'Nein'],['Full-state bytes',s.state?s.state.length/2:0]]);
      $('#monitor-error').textContent=currentSignal.error || ''; $('#use-signal').disabled=!currentSignal.valid; $('#replay-signal').disabled=!currentSignal.valid;
    }
  } catch (e) { $('#monitor-error').textContent=e.message; }
  setTimeout(monitor,900);
}
function formData(form) { return Object.fromEntries(new FormData(form)); }
function bindForm(id,handler) {
  $(id).addEventListener('submit',async event => {
    event.preventDefault(); const button=$('button[type="submit"]',event.target); button.disabled=true;
    try { await handler(event.target); } catch (e) { toast(e.message,true); }
    finally { button.disabled=event.target.id==='learn-form' && learning; }
  });
}
bindForm('#remote-form',async form => { const data=formData(form); data.optimistic_switch=form.elements.optimistic_switch.checked; await api('/api/remotes',data.remote?'PATCH':'POST',data); $('#remote-dialog').close(); await loadRemotes(); });
bindForm('#learn-form',async form => { const data=formData(form); for (const field of ['confirmations','tolerance_percent','repeat_count']) data[field]=Number(data[field]); await api('/api/learn/start','POST',data); learning=true; clearTimeout(learnTimer); await learnStatus(); });
bindForm('#details-form',async form => { const data=formData(form); data.frequency=Number(data.frequency); data.repeat_count=Number(data.repeat_count); data.favorite=form.elements.favorite.checked; await api('/api/button','PATCH',data); $('#details-dialog').close(); await loadRemotes(); toast('Taste aktualisiert'); });
bindForm('#use-form',async form => { const data=formData(form); data.sequence=Number(data.sequence); await api('/api/received/use','POST',data); $('#use-dialog').close(); await loadRemotes(); toast('Signal gespeichert'); });
bindForm('#config-form',async form => {
  const data=formData(form); data.mqtt_port=Number(data.mqtt_port); data.ota_enabled=form.elements.ota_enabled.checked; data.web_auth=form.elements.web_auth.checked;
  data.ap_fallback_enabled=form.elements.ap_fallback_enabled.checked;
  for (const field of ['wifi_password','mqtt_password','ota_password','web_password','recovery_password']) {
    if (!data[field] && !$("[data-clear='"+field+"']",form)?.checked) delete data[field];
  }
  if (data.wifi_ssid !== config.wifi_ssid && !data.wifi_password) data.wifi_password='';
  await api('/api/config','POST',data); toast('Gespeichert. Bridge startet neu.');
  setTimeout(() => location.reload(),7000);
});
async function loadConfig() {
  config=await api('/api/config'); const form=$('#config-form');
  for (const [key,value] of Object.entries(config)) {
    const input=form.elements[key]; if (!input) continue;
    if (input.type==='checkbox') input.checked=value; else input.value=value;
  }
}
function renderScenes() {
  const target=$('#scenes'); target.replaceChildren();
  for (const remote of remotes) for (const scene of remote.macros || []) {
    const row=element('div','scene-row'), description=element('div');
    description.append(element('strong','',scene.name),element('small','muted',`${remote.name} · ${scene.steps.length} Schritte`));
    row.append(description,action('Ausführen',() => submitCommand({remote:remote.id,macro:scene.id},scene.name)),menu([
      ['Bearbeiten',() => openScene(remote.id,scene)],
      ['Löschen',async () => { if (confirm(`Szene „${scene.name}“ löschen?`)) { await api('/api/macros','DELETE',{remote:remote.id,macro:scene.id}); await loadRemotes(); } }]
    ])); target.append(row);
  }
  if (!target.childElementCount) target.append(element('p','muted','Noch keine Szenen. Mit „+ Szene“ mehrere Tasten verbinden.'));
}
function openScene(remoteId='',scene=null) {
  if (!remotes.some(r => r.buttons.length)) { toast('Zuerst mindestens eine Taste lernen.'); return; }
  const form=$('#scene-form'); form.reset(); remoteOptions(form.elements.remote,remoteId);
  form.elements.remote.disabled=Boolean(scene); form.elements.id.value=scene?.id || ''; form.elements.name.value=scene?.name || '';
  $('#scene-title').textContent=scene?'Szene bearbeiten':'Neue Szene'; $('#scene-steps').replaceChildren();
  for (const step of scene?.steps || [{}]) addSceneStep(step);
  $('#scene-dialog').showModal();
}
function addSceneStep(step={}) {
  const target=$('#scene-steps'); if (target.childElementCount>=16) { toast('Höchstens 16 Schritte pro Szene.'); return; }
  const row=element('div','scene-step'), label=element('label','','Taste'), select=element('select','scene-button'); select.required=true;
  for (const remote of remotes) {
    const group=element('optgroup'); group.label=remote.name;
    for (const button of remote.buttons) { const option=element('option','',button.name); option.value=`${remote.id}/${button.id}`; group.append(option); }
    if (group.childElementCount) select.append(group);
  }
  if (step.remote) select.value=`${step.remote}/${step.button}`;
  label.append(select);
  const delayLabel=element('label','','Pause davor (ms)'), delay=element('input','scene-delay');
  delay.type='number'; delay.min='0'; delay.max='60000'; delay.step='1'; delay.value=step.delay_ms || 0; delay.required=true; delayLabel.append(delay);
  const controls=element('div','actions');
  const up=action('↑',() => { if (row.previousElementSibling) target.insertBefore(row,row.previousElementSibling); }); up.setAttribute('aria-label','Schritt nach oben');
  const down=action('↓',() => { if (row.nextElementSibling) target.insertBefore(row.nextElementSibling,row); }); down.setAttribute('aria-label','Schritt nach unten');
  controls.append(up,down,action('Entfernen',() => row.remove())); row.append(label,delayLabel,controls); target.append(row);
}
bindForm('#scene-form',async form => {
  const data={id:form.elements.id.value,remote:form.elements.remote.value,name:form.elements.name.value,steps:[]};
  for (const row of $$('.scene-step')) {
    const [remote,button]=$('.scene-button',row).value.split('/'); data.steps.push({remote,button,delay_ms:Number($('.scene-delay',row).value)});
  }
  if (!data.steps.length || data.steps.reduce((sum,step) => sum+step.delay_ms,0)>300000) throw new Error('Mindestens ein Schritt, insgesamt höchstens fünf Minuten Pause.');
  await api('/api/macros','POST',data); $('#scene-dialog').close(); await loadRemotes(); toast('Szene gespeichert');
});
async function commandHistory() {
  if (page!=='diagnostics') return;
  try {
    const data=await api('/api/commands'), target=$('#command-history'); target.replaceChildren();
    for (const result of [...data.commands].reverse()) {
      const row=element('div',`command-row ${result.status}`), description=element('div');
      description.append(element('strong','',commandLabels[result.status] || result.status),element('small','muted',`${result.request_id} · ${result.source} · ${result.completed_steps}/${result.total_steps} Schritte`));
      if (result.error) description.append(element('p','error',result.error));
      row.append(description);
      if (result.status==='queued') row.append(action('Abbrechen',async () => { await api('/api/commands/cancel','POST',{request_id:result.request_id}); await commandHistory(); }));
      target.append(row);
    }
    if (!data.commands.length) target.append(element('p','muted','Noch keine Aufträge seit dem Neustart.'));
  } catch (e) { $('#command-history').textContent=e.message; }
  clearTimeout(commandTimer); commandTimer=setTimeout(commandHistory,1500);
}
function openBatch(remoteId='') {
  if (!remotes.length) { openRemote(); return; }
  if (batch) { $('#batch-dialog').showModal(); return; }
  const form=$('#batch-form'); form.reset(); remoteOptions(form.elements.remote,remoteId);
  form.hidden=false; $('#batch-active').hidden=true; $('#batch-dialog').showModal();
}
function batchButtons(saved=false,retry=false) {
  $('#batch-test').disabled=!saved; $('#batch-next').disabled=!saved; $('#batch-retry').disabled=!retry;
}
async function startBatchStep() {
  clearTimeout(batchTimer); if (!batch) return;
  if (batch.index>=batch.names.length) {
    learning=false; $('#batch-progress').textContent='Tastenfolge abgeschlossen. Alle gespeicherten Tasten sind in deiner Remote verfügbar.';
    batchButtons(); $('#batch-skip').disabled=true; batch=null; await loadRemotes(); return;
  }
  const active=batch; active.button=null; batchButtons(); $('#batch-skip').disabled=true;
  try {
    const replaceId=remotes.find(r => r.id===active.remote)?.buttons.find(b => b.name===active.names[active.index])?.id || '';
    learning=true;
    await api('/api/learn/start','POST',{remote:active.remote,name:active.names[active.index],replace_id:replaceId,mode:active.confirmations>1?'reliable':'fast',confirmations:active.confirmations});
    if (batch!==active) return;
    learning=true; $('#batch-skip').disabled=false; await pollBatch();
  } catch (e) { learning=false; $('#batch-progress').textContent=e.message; batchButtons(false,true); $('#batch-skip').disabled=false; }
}
async function pollBatch() {
  if (!batch) return; const active=batch, index=active.index;
  try {
    const s=await api('/api/learn/status'); if (batch!==active || active.index!==index) return;
    if (s.remote && (s.remote!==active.remote || s.name!==active.names[index])) throw new Error('Eine andere Lernsession ist aktiv.');
    learning=['waiting','receiving'].includes(s.status);
    $('#batch-progress').textContent=`Taste ${index+1}/${active.names.length}: ${active.names[index]}\n${s.status.toUpperCase()} · ${s.captured}/${s.required}\n${s.message || ''}${learning?`\nNoch ${Math.ceil(s.remaining_ms/1000)} s`:''}`;
    if (learning) batchTimer=setTimeout(pollBatch,700);
    else if (s.status==='saved') {
      await loadRemotes(); if (batch!==active || active.index!==index) return;
      active.button=remotes.find(r => r.id===active.remote)?.buttons.find(b => b.name===active.names[index]);
      batchButtons(Boolean(active.button),true); $('#batch-progress').textContent+='\nGespeichert. Taste testen, dann weitergehen.';
    } else batchButtons(false,true);
  } catch (e) { learning=false; $('#batch-progress').textContent=e.message; batchButtons(false,true); }
}
async function stopBatch() {
  clearTimeout(batchTimer); batch=null;
  if (learning) await api('/api/learn/cancel','POST',{});
  learning=false; $('#batch-dialog').close();
}
bindForm('#batch-form',async form => {
  const names=form.elements.names.value.split(/\r?\n/).map(name => name.trim()).filter(Boolean);
  const remote=remotes.find(r => r.id===form.elements.remote.value), total=remotes.reduce((count,r) => count+r.buttons.length,0);
  if (!names.length || names.length>64-total || new Set(names).size!==names.length || names.some(name => new TextEncoder().encode(name).length>48 || /[\u0000-\u001f\u007f]/.test(name))) throw new Error('Eindeutige Namen mit höchstens 48 UTF-8-Bytes verwenden und das Tastenlimit beachten.');
  if (names.some(name => remote.buttons.some(button => button.name===name))) throw new Error('Ein Name existiert bereits. Zum Ersetzen „Neu lernen“ an dieser Taste wählen.');
  batch={remote:remote.id,names,index:0,confirmations:Number(form.elements.confirmations.value),button:null};
  form.hidden=true; $('#batch-active').hidden=false; await startBatchStep();
});
$('#batch-test').addEventListener('click',async () => {
  if (!batch?.button) return; const active=batch; batchButtons(); $('#batch-skip').disabled=true;
  try { await send(remotes.find(r => r.id===active.remote),active.button); }
  catch (e) { toast(e.message,true); }
  finally { if (batch===active) { batchButtons(true,true); $('#batch-skip').disabled=false; } }
});
$('#batch-next').addEventListener('click',async () => { if (batch) { batch.index++; await startBatchStep(); } });
$('#batch-skip').addEventListener('click',async () => {
  if (!batch) return;
  $('#batch-skip').disabled=true;
  try { clearTimeout(batchTimer); if (learning) await api('/api/learn/cancel','POST',{}); learning=false; batch.index++; await startBatchStep(); }
  catch (e) { toast(e.message,true); }
});
$('#batch-retry').addEventListener('click',async () => {
  if (!batch) return;
  await startBatchStep();
});
$('#batch-cancel').addEventListener('click',() => stopBatch().catch(e => toast(e.message,true)));
bindForm('#firmware-form',async form => {
  if (!firmwareEnabled) throw new Error('OTA in den Einstellungen aktivieren.');
  const file=form.elements.firmware.files[0];
  if (!file || !file.name.endsWith('.bin') || file.size<64 || file.size>firmwareLimit) throw new Error('Eine passende .bin-Datei innerhalb des freien Updatebereichs wählen.');
  const bytes=new Uint8Array(await file.arrayBuffer());
  if (bytes[0]!==0xe9 || (bytes[3]>>4)!==4 || !new TextDecoder().decode(bytes).includes(firmwareTarget)) throw new Error('Diese Datei ist keine passende ESP-IR-Bridge-Firmware.');
  const digest=firmwareMd5(bytes), payload=new FormData(); payload.append('firmware',file);
  firmwareUploading=true; $('#firmware-progress').hidden=false; $('#firmware-progress').value=0;
  try {
    await new Promise((resolve,reject) => {
      const xhr=new XMLHttpRequest(); xhr.open('POST','/api/firmware'); xhr.timeout=180000;
      xhr.setRequestHeader('X-IRBridge-Firmware','1'); xhr.setRequestHeader('X-Firmware-Size',String(file.size)); xhr.setRequestHeader('X-Firmware-MD5',digest);
      if (form.elements.update_password.value) xhr.setRequestHeader('X-IRBridge-OTA-Password',form.elements.update_password.value);
      xhr.upload.onprogress=event => {
        if (event.lengthComputable) $('#firmware-progress').value=Math.round(event.loaded/event.total*100);
        $('#firmware-message').textContent='Firmware wird übertragen und geprüft …';
      };
      xhr.onload=() => { try { const result=JSON.parse(xhr.responseText); if (xhr.status<200 || xhr.status>=300 || !result.success) throw new Error(result.error || 'Update fehlgeschlagen'); resolve(); } catch (e) { reject(e); } };
      xhr.onerror=() => reject(new Error('Verbindung beim Update unterbrochen. Gerätestatus prüfen.'));
      xhr.ontimeout=() => reject(new Error('Update-Zeitüberschreitung. Gerätestatus prüfen.'));
      xhr.send(payload);
    });
    $('#firmware-progress').value=100; $('#firmware-message').textContent='Firmware geprüft. Bridge startet neu …';
    form.elements.update_password.value=''; setTimeout(() => location.reload(),8000);
  } catch (e) { $('#firmware-message').textContent=e.message; throw e; }
  finally { firmwareUploading=false; }
});
$('#add-scene').addEventListener('click',() => openScene());
$('#scene-add-step').addEventListener('click',() => addSceneStep());
$('#batch-learn').addEventListener('click',() => openBatch());
$('#add-remote').addEventListener('click',() => openRemote());
$('#add-button').addEventListener('click',() => openLearn());
$('#learn-cancel').addEventListener('click',() => cancelLearn().catch(e => toast(e.message,true)));
$('#refresh-status').addEventListener('click',status);
$('#replay-signal').addEventListener('click',async () => { try { await api('/api/send/latest','POST',{}); toast('RAW gesendet'); } catch(e) { toast(e.message,true); } });
$('#use-signal').addEventListener('click',() => {
  if (!remotes.length) { openRemote(); return; }
  const form=$('#use-form'); form.reset(); remoteOptions(form.elements.remote); form.elements.sequence.value=currentSignal.sequence; $('#use-dialog').showModal();
});
$('#import-file').addEventListener('change',async event => {
  const file=event.target.files[0]; if (!file) return;
  try {
    const parsed=JSON.parse(await file.text());
    if (parsed.version!==1 || !Array.isArray(parsed.remotes) || !parsed.configuration) throw new Error('Ungültiges Backup-Format');
    if (!confirm('Alle Fernbedienungen durch dieses Backup ersetzen? Bestehende Passwörter bleiben erhalten.')) return;
    const data=new FormData(); data.append('backup',file);
    const response=await fetch('/api/backup/import',{method:'POST',headers:{'X-IRBridge-Import':'1'},body:data}); const result=await response.json();
    if (!response.ok || !result.success) throw new Error(result.error || 'Import fehlgeschlagen');
    toast('Backup importiert. Bridge startet neu.'); setTimeout(() => location.reload(),7000);
  } catch(e) { toast(e.message,true); }
  finally { event.target.value=''; }
});
for (const dialog of $$('dialog')) {
  $('.close-dialog',dialog).addEventListener('click',() => { if (dialog.id==='batch-dialog') stopBatch().catch(e => toast(e.message,true)); else if (dialog.id==='learn-dialog' && learning) cancelLearn().catch(e => toast(e.message,true)); else dialog.close(); });
  dialog.addEventListener('cancel',event => { if (dialog.id==='batch-dialog') { event.preventDefault(); stopBatch().catch(e => toast(e.message,true)); } else if (dialog.id==='learn-dialog' && learning) { event.preventDefault(); cancelLearn().catch(e => toast(e.message,true)); } });
}
document.addEventListener('click',event => { for (const details of $$('details.menu[open]')) if (!details.contains(event.target)) details.open=false; });
async function init() {
  await status();
  try { await loadRemotes(); if (page==='settings') await loadConfig(); } catch (e) { toast(e.message,true); }
  monitor(); setInterval(status,6000);
  commandHistory();
}
init();
