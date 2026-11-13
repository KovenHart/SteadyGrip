// SteadyGrip - interface. Reçoit l'état de l'application (messages JSON) et lui envoie les commandes.
'use strict';

const host = window.chrome && window.chrome.webview;
let state = null;      // dernier état reçu (bibliothèque, statut, partie en cours)
let game = null;       // réglages du jeu affiché
let view = 'library';  // library | game | advanced | settings
let addTab = 'steam';
let steamGames = null;
let steamPick = null;
let exeForm = { path: '', name: '', image: '' };

const LEVELS = [
  { name: 'Désactivée', hint: 'Pimax seul' },
  { name: 'Réactive', hint: 'Léger lissage' },
  { name: 'Équilibrée', hint: 'Recommandé' },
  { name: 'Très stable', hint: 'Visée au sniper' }
];

function send(cmd, extra) {
  const msg = Object.assign({ cmd }, extra || {});
  if (host) host.postMessage(JSON.stringify(msg));
  else mock(msg);
}

function esc(s) {
  return String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

const $ = (sel) => document.querySelector(sel);

// --- Barre du haut ----------------------------------------------------------------------------------
function renderStatus() {
  if (!state) return;
  document.documentElement.dataset.theme = state.theme === 'evo' ? 'evo' : 'classic';
  const ready = $('#ready');
  ready.className = 'ready ' + (state.ready ? 'ok' : 'warn');
  const icon = state.ready
    ? '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M5 12l5 5L20 7"/></svg>'
    : '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" aria-hidden="true"><path d="M12 8v5M12 16.5v.5"/><circle cx="12" cy="12" r="9"/></svg>';
  const install = state.layer !== 1 ? ' <button type="button" class="btn primary" data-action="install">Installer</button>' : '';
  ready.innerHTML = icon + '<span>' + esc(state.readyText) + '</span>' + install;
  $('#checks').innerHTML = state.checks.map((c) =>
    '<div class="check"><span class="dot' + (c.ok ? '' : ' off') + '"></span>' + esc(c.text) + '</div>').join('');
}

// --- Bibliothèque -----------------------------------------------------------------------------------
function coverHtml(g, extraClass) {
  const img = g.image ? '<img src="' + esc(g.image) + '" alt="" onerror="this.remove()">' : '';
  const badge = g.running ? '<span class="badge">EN COURS</span>' : '';
  return '<div class="cover' + (extraClass || '') + '">' + badge + img + '<span>' + esc(g.name) + '</span></div>';
}

function renderLibrary() {
  const games = state ? state.games : [];
  const cards = games.map((g) =>
    '<button type="button" class="card' + (g.running ? ' running' : '') + '" data-action="open" data-id="' + esc(g.id) + '">' +
    coverHtml(g) + '<span class="name">' + esc(g.name) + '</span><span class="sub">' + esc(g.summary) + '</span></button>').join('');
  const defaults =
    '<button type="button" class="card" data-action="open" data-id="default">' +
    '<div class="cover defaults"><svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="3" y="7" width="18" height="11" rx="4"/><path d="M7 12h3M8.5 10.5v3M15 11.5h.01M17 13h.01"/></svg><span>Tous les autres jeux</span></div>' +
    '<span class="name">Réglages par défaut</span><span class="sub">' + esc(state ? state.defaultSummary : '') + '</span></button>';
  const add =
    '<button type="button" class="card" data-action="add">' +
    '<div class="cover add"><svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-hidden="true"><path d="M12 5v14M5 12h14"/></svg><span class="label">Ajouter un jeu</span></div>' +
    '<span class="sub" style="margin-top:0">Depuis Steam ou un fichier .exe</span></button>';
  $('#view').innerHTML =
    '<div class="lib-head"><h1>Mes jeux</h1><span class="muted">Clique sur un jeu pour régler sa stabilisation. Un jeu lancé s\'ajoute tout seul.</span></div>' +
    '<div class="grid">' + cards + defaults + add + '</div>';
}

// --- Page d'un jeu ----------------------------------------------------------------------------------
function liveHtml() {
  const running = state && state.running && game && state.running.id === game.id;
  if (!running) {
    return '<aside class="live panel"><h2>Pendant la partie</h2><p class="note">Lance ce jeu : l\'état de chaque main et le résultat de la stabilisation s\'afficheront ici.</p>' +
      '<div class="notice"><strong>Conseil :</strong> tiens l\'arme un peu en avant du visage. Juste sous le casque, les caméras ne voient presque plus la manette.</div></aside>';
  }
  const names = ['Main gauche', 'Main droite'];
  const hands = state.hands.map((h, i) =>
    '<div class="hand ' + esc(h.state) + '"><span class="b"></span><div><div class="n">' + names[i] + '</div><div class="s">' + esc(h.text) + '</div></div></div>').join('');
  const tremor = state.tremorPct > 0 ? '−' + state.tremorPct + ' %' : '…';
  return '<aside class="live panel"><h2>Pendant la partie</h2>' + hands +
    '<div><div class="big good">' + tremor + '</div><div class="note">de tremblement en visée</div></div>' +
    '<div><div class="big">' + state.caught + '</div><div class="note">pertes de suivi rattrapées en douceur</div></div>' +
    '<div class="notice"><strong>Conseil :</strong> tiens l\'arme un peu en avant du visage. Juste sous le casque, les caméras ne voient presque plus la manette.</div></aside>';
}

function renderGame() {
  if (!game) { $('#view').innerHTML = ''; return; }
  const s = game.settings;
  const isDefault = game.id === 'default';
  const running = state && state.running && state.running.id === game.id;
  const cover = isDefault ? '' : coverHtml({ name: game.name, image: game.image, running });
  const levels = LEVELS.map((l, i) =>
    '<button type="button" class="level" aria-pressed="' + (s.level === i) + '" data-action="level" data-value="' + i + '"><b>' + l.name + '</b><small>' + l.hint + '</small></button>').join('');
  const toggle = (key, title, text) =>
    '<div class="toggle-row"><div><div class="t">' + title + '</div><div class="d">' + text + '</div></div>' +
    '<button type="button" class="switch" role="switch" aria-checked="' + !!s[key] + '" aria-label="' + esc(title) + '" data-action="toggle" data-key="' + key + '"></button></div>';
  const origin = isDefault
    ? '<p class="note">Ces réglages servent à tous les jeux qui n\'ont pas les leurs.</p>'
    : game.own
      ? '<button type="button" class="link" data-action="reset">Revenir aux réglages par défaut</button>'
      : '<p class="note">Ce jeu utilise les réglages par défaut. Change un réglage pour lui donner les siens.</p>';
  const custom = s.level < 0 ? '<p class="note">Réglage personnalisé (réglages avancés).</p>' : '';
  const manage = isDefault ? '' :
    '<div class="row" style="flex-wrap:wrap"><button type="button" class="btn ghost" data-action="setImage">Changer l\'image</button>' +
    '<button type="button" class="btn ghost danger" data-action="remove">Retirer de mes jeux</button></div>';
  $('#view').innerHTML =
    '<button type="button" class="back" data-action="home"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M15 18l-6-6 6-6"/></svg>Mes jeux</button>' +
    '<div class="game" style="margin-top:12px">' +
      '<div class="game-side">' + cover + '<h1>' + esc(game.name) + '</h1>' +
        (running ? '<div class="check" style="color:var(--accent-line)"><span class="dot" style="background:var(--accent-line)"></span>En cours</div>' : '') +
        origin + manage + '</div>' +
      '<div class="game-main">' +
        '<section class="panel"><h2>Stabilité de la visée</h2><p>Plus c\'est stable, moins l\'arme tremble quand tu vises. En mouvement, elle reste toujours réactive.</p><div class="levels">' + levels + '</div>' + custom + '</section>' +
        '<section class="panel" style="padding-top:6px;padding-bottom:6px">' +
          toggle('lossGuard', 'Garder l\'arme en main hors du champ', 'Sans ça, l\'arme peut partir au sol ou se téléporter quand les caméras perdent la manette.') +
          toggle('twoHands', 'Armes à deux mains', 'Quand tes deux grips sont serrés, la main perdue suit l\'autre main.') +
          toggle('gripFix', 'Grip qui lâche tout seul', 'Pour les jeux où la main s\'ouvre et se referme toute seule et où l\'arme tombe : SteadyGrip ignore les relâchements très courts du grip.') +
        '</section>' +
        '<button type="button" class="link" style="align-self:flex-start" data-action="advanced">Réglages avancés' + (isDefault ? '' : ' pour ce jeu') + ' ›</button>' +
      '</div>' + liveHtml() +
    '</div>';
}

// --- Réglages avancés -------------------------------------------------------------------------------
const ADV = [
  { title: 'Tremblements', text: 'Les caméras corrigent la position de la manette environ 25 fois par seconde. Ces réglages étalent ces petites corrections.', items: [
    { key: 'stillTauMs', label: 'Arme immobile', unit: ' ms', min: 10, max: 200, left: 'Réactive', right: 'Très lisse', help: 'Ce qu\'on voit en visant. Plus haut : visée plus calme.' },
    { key: 'movingTauMs', label: 'Arme en mouvement', unit: ' ms', min: 0, max: 30, left: 'Réactive', right: 'Fluide', help: 'Supprime les saccades quand tu déplaces l\'arme.' } ] },
  { title: 'Visée', text: 'Calme l\'orientation de l\'arme quand tu vises, sans retard quand tu la tournes vite.', items: [
    { key: 'orientSmoothing', label: 'Stabilisation de la visée', unit: '', min: 0, max: 100, left: 'Aucune', right: 'Maximale', help: '75 donne le meilleur résultat mesuré dans CrossFire: Sierra Squad.' },
    { key: 'teleportBlendMs', label: 'Retour dans le champ', unit: ' ms', min: 0, max: 300, left: 'Immédiat', right: 'En douceur', help: 'Temps pour rejoindre la vraie position quand les caméras retrouvent la manette.' } ] },
  { title: 'Manette hors du champ', text: 'Quand les caméras ne voient plus la manette, SteadyGrip la garde dans des limites réalistes.', items: [
    { key: 'maxLostDriftCm', label: 'Déplacement maximal', unit: ' cm', min: 5, max: 60, left: '5 cm', right: '60 cm', help: 'Distance que l\'arme peut parcourir pendant une perte.' },
    { key: 'maxReachCm', label: 'Longueur du bras', unit: ' cm', min: 50, max: 120, left: '50 cm', right: '120 cm', help: 'L\'arme ne s\'éloigne jamais plus de ton corps.' } ] },
  { title: 'Bouton grip', text: 'Avec « Grip qui lâche tout seul », un relâchement du grip plus court que ce délai est ignoré. Quand tu lâches vraiment, la main s\'ouvre avec ce délai de retard.', items: [
    { key: 'gripHoldMs', label: 'Relâchements ignorés jusqu\'à', unit: ' ms', min: 30, max: 400, left: 'Lâcher immédiat', right: 'Grip très sûr', help: '60 ms suffit en général (les relâchements parasites mesurés durent moins de 30 ms). Si l\'arme tombe encore, monte un peu.' } ] }
];

function renderAdvanced() {
  if (!game) return;
  const s = game.settings;
  const groups = ADV.map((g) =>
    '<section class="panel"><div><h2>' + g.title + '</h2><p class="note" style="margin:4px 0 0;line-height:1.45">' + g.text + '</p></div>' +
    g.items.map((it) =>
      '<div class="slider"><div class="top"><label for="adv-' + it.key + '">' + it.label + '</label><output id="out-' + it.key + '">' + Math.round(s[it.key]) + it.unit + '</output></div>' +
      '<input id="adv-' + it.key + '" type="range" min="' + it.min + '" max="' + it.max + '" value="' + Math.round(s[it.key]) + '" data-key="' + it.key + '" data-unit="' + it.unit + '">' +
      '<div class="ends"><span>' + it.left + '</span><span>' + it.right + '</span></div><p>' + it.help + '</p></div>').join('') +
    (g.title === 'Manette hors du champ'
      ? '<label class="check-row"><input type="checkbox" data-action="armModel"' + (s.armModel ? ' checked' : '') + '> Modèle de bras pendant les réinitialisations de Pimax (sinon : arme figée)</label>' : '') +
    '</section>').join('');
  $('#view').innerHTML =
    '<div class="row" style="justify-content:space-between;flex-wrap:wrap;margin-bottom:16px">' +
      '<div class="row"><button type="button" class="back" data-action="back-game"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M15 18l-6-6 6-6"/></svg>' + esc(game.name) + '</button><h1>Réglages avancés</h1></div>' +
      '<button type="button" class="btn ghost" data-action="recommended">Revenir aux réglages recommandés</button>' +
    '</div><div class="adv">' + groups + '</div>';
}

// --- Paramètres -------------------------------------------------------------------------------------
function shortcutRow(where, title, text, on) {
  return '<div class="toggle-row"><div><div class="t">' + title + '</div><div class="d">' + text + '</div></div>' +
    '<button type="button" class="switch" role="switch" aria-checked="' + !!on + '" aria-label="' + esc(title) + '" data-action="shortcut" data-where="' + where + '"></button></div>';
}

function renderSettings() {
  const theme = state && state.theme === 'evo' ? 'evo' : 'classic';
  const layer = state ? state.layer : 0;
  const layerText = layer === 1 ? 'Installée : active dans tous les jeux OpenXR.' : layer === 2 ? 'Installée depuis un autre dossier. Clique sur Installer pour utiliser celui-ci.' : 'Non installée.';
  $('#view').innerHTML =
    '<button type="button" class="back" data-action="home"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M15 18l-6-6 6-6"/></svg>Mes jeux</button>' +
    '<div class="settings" style="margin-top:12px"><h1>Paramètres</h1>' +
      '<section class="panel"><h2>Apparence</h2><div class="seg" role="group" aria-label="Thème">' +
        '<button type="button" aria-pressed="' + (theme === 'classic') + '" data-action="theme" data-value="classic">Pimax Classic</button>' +
        '<button type="button" aria-pressed="' + (theme === 'evo') + '" data-action="theme" data-value="evo">Pimax EVO</button></div></section>' +
      '<section class="panel" style="padding-top:10px;padding-bottom:6px"><h2 style="margin-top:8px">Raccourcis</h2>' +
        shortcutRow('desktop', 'Raccourci sur le bureau', 'Une icône SteadyGrip sur le bureau.', state && state.desktop) +
        shortcutRow('start', 'Dans le menu Démarrer', 'SteadyGrip dans la liste des applications de Windows.', state && state.startMenu) +
      '</section>' +
      '<section class="panel"><h2>Stabilisation dans les jeux</h2><p class="note" style="margin:0">' + layerText + '</p>' +
        '<div class="row"><button type="button" class="btn primary" data-action="install">Installer</button><button type="button" class="btn ghost danger" data-action="uninstall">Désinstaller</button></div></section>' +
      '<section class="panel"><h2>Pimax</h2><p class="note" style="margin:0">' + esc(state && state.pimaxVersion ? state.pimaxVersion : 'Service Pimax non détecté.') + '</p>' +
        '<p class="note" style="margin:0">SteadyGrip lit l\'état de suivi des manettes dans le service Pimax, sans rien y modifier. C\'est pour cela qu\'il demande les droits d\'administrateur.</p></section>' +
      '<p class="note">SteadyGrip ' + esc(state ? state.version : '') + ' · réglages et journal dans %LOCALAPPDATA%\\SteadyGrip</p>' +
    '</div>';
}

// --- Ajouter un jeu ---------------------------------------------------------------------------------
function renderAdd() {
  const modal = $('#modal');
  let body;
  if (addTab === 'steam') {
    let list;
    if (!steamGames) list = '<p class="empty">Recherche des jeux Steam…</p>';
    else if (!steamGames.length) list = '<p class="empty">Aucun jeu Steam trouvé sur ce PC.</p>';
    else {
      const q = ($('#steam-search') ? $('#steam-search').value : '').toLowerCase();
      list = '<div class="steam-list">' + steamGames.filter((g) => !q || g.name.toLowerCase().includes(q)).map((g) =>
        '<button type="button" class="steam-item" data-action="steamPick" data-app="' + g.appId + '" aria-pressed="' + (steamPick === g.appId) + '"' + (g.added ? ' disabled' : '') + '>' +
        '<span>' + esc(g.name) + '</span><small>' + (g.added ? 'Déjà ajouté' : '') + '</small></button>').join('') + '</div>';
    }
    body = '<label for="steam-search" class="note label">Jeux installés avec Steam</label>' +
      '<input id="steam-search" class="field" type="search" placeholder="Rechercher un jeu" autocomplete="off">' +
      '<div id="steam-results">' + list + '</div>' +
      '<p class="note" style="margin:0">La jaquette du jeu est récupérée sur Steam et gardée sur ton PC.</p>';
  } else {
    body = '<p style="margin:0;line-height:1.5">Pour un jeu hors de Steam : choisis le programme qui lance le jeu. SteadyGrip le reconnaîtra ensuite tout seul à chaque lancement.</p>' +
      '<div class="row"><label for="exe-path" class="sr">Programme du jeu</label><input id="exe-path" class="field" type="text" readonly placeholder="Aucun programme choisi" value="' + esc(exeForm.path) + '"><button type="button" class="btn ghost" data-action="pickExe">Parcourir…</button></div>' +
      '<div class="row"><label for="exe-name" class="sr">Nom du jeu</label><input id="exe-name" class="field" type="text" placeholder="Nom du jeu" value="' + esc(exeForm.name) + '"></div>' +
      '<div class="row"><button type="button" class="btn ghost" data-action="pickImage">Choisir une image (facultatif)</button><span class="note">' + esc(exeForm.image ? exeForm.image.split(/[\\/]/).pop() : '') + '</span></div>';
  }
  const canAdd = addTab === 'steam' ? steamPick != null : !!exeForm.path;
  modal.innerHTML =
    '<div class="modal" role="dialog" aria-modal="true" aria-labelledby="add-title">' +
      '<div class="modal-head"><h1 id="add-title">Ajouter un jeu</h1><button type="button" class="icon-btn" data-action="closeAdd" aria-label="Fermer"><svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-hidden="true"><path d="M6 6l12 12M18 6L6 18"/></svg></button></div>' +
      '<div class="tabs" role="tablist"><button type="button" class="tab" role="tab" aria-selected="' + (addTab === 'steam') + '" data-action="tab" data-value="steam">Depuis Steam</button>' +
        '<button type="button" class="tab" role="tab" aria-selected="' + (addTab === 'exe') + '" data-action="tab" data-value="exe">Autre jeu (.exe)</button></div>' +
      body +
      '<div class="actions"><button type="button" class="btn ghost" data-action="closeAdd">Annuler</button><button type="button" class="btn primary" data-action="confirmAdd"' + (canAdd ? '' : ' disabled') + '>Ajouter</button></div>' +
    '</div>';
  modal.hidden = false;
}

function closeAdd() { $('#modal').hidden = true; $('#modal').innerHTML = ''; }

// --- Rendu et navigation ----------------------------------------------------------------------------
function render() {
  renderStatus();
  if (view === 'library') renderLibrary();
  else if (view === 'game') renderGame();
  else if (view === 'advanced') renderAdvanced();
  else if (view === 'settings') renderSettings();
}


function go(v) { view = v; render(); $('#view').scrollTop = 0; }

document.addEventListener('click', (e) => {
  const el = e.target.closest('[data-action]');
  if (!el || el.disabled) return;
  const a = el.dataset.action;
  if (a === 'home') { game = null; go('library'); }
  else if (a === 'settings') go('settings');
  else if (a === 'open') { send('getGame', { id: el.dataset.id }); view = 'game'; }
  else if (a === 'level') send('set', { id: game.id, key: 'level', value: Number(el.dataset.value) });
  else if (a === 'toggle') send('set', { id: game.id, key: el.dataset.key, value: el.getAttribute('aria-checked') !== 'true' });
  else if (a === 'advanced') go('advanced');
  else if (a === 'back-game') go('game');
  else if (a === 'reset') send('reset', { id: game.id });
  else if (a === 'recommended') { send('set', { id: game.id, key: 'level', value: 2 }); }
  else if (a === 'setImage') send('setImage', { id: game.id });
  else if (a === 'remove') { if (confirm('Retirer « ' + game.name + ' » de tes jeux ? Ses réglages seront supprimés.')) { send('remove', { id: game.id }); game = null; go('library'); } }
  else if (a === 'install') send('install');
  else if (a === 'shortcut') send('shortcut', { where: el.dataset.where, value: el.getAttribute('aria-checked') !== 'true' });
  else if (a === 'uninstall') send('uninstall');
  else if (a === 'theme') send('theme', { value: el.dataset.value });
  else if (a === 'add') { addTab = 'steam'; steamPick = null; steamGames = null; exeForm = { path: '', name: '', image: '' }; renderAdd(); send('listSteam'); }
  else if (a === 'closeAdd') closeAdd();
  else if (a === 'tab') { addTab = el.dataset.value; renderAdd(); }
  else if (a === 'steamPick') { steamPick = Number(el.dataset.app); renderAdd(); }
  else if (a === 'pickExe') send('pickExe');
  else if (a === 'pickImage') send('pickImage');
  else if (a === 'confirmAdd') {
    if (addTab === 'steam') {
      const g = steamGames.find((x) => x.appId === steamPick);
      if (g) send('addSteam', { appId: g.appId, name: g.name });
    } else {
      exeForm.name = $('#exe-name').value.trim();
      send('addExe', { path: exeForm.path, name: exeForm.name, image: exeForm.image });
    }
    closeAdd();
  }
});

document.addEventListener('input', (e) => {
  if (e.target.id === 'steam-search') {
    const pos = e.target.selectionStart;
    renderAdd();
    const f = $('#steam-search');
    f.value = e.target.value;
    f.focus();
    f.setSelectionRange(pos, pos);
  } else if (e.target.id === 'exe-name') {
    exeForm.name = e.target.value;
  } else if (e.target.dataset.key) {
    const out = document.getElementById('out-' + e.target.dataset.key);
    if (out) out.textContent = e.target.value + e.target.dataset.unit;
  }
});

document.addEventListener('change', (e) => {
  if (!game) return;
  if (e.target.dataset.key) send('set', { id: game.id, key: e.target.dataset.key, value: Number(e.target.value) });
  else if (e.target.dataset.action === 'armModel') send('set', { id: game.id, key: 'armModel', value: e.target.checked });
});

document.addEventListener('keydown', (e) => { if (e.key === 'Escape' && !$('#modal').hidden) closeAdd(); });

function onMessage(msg) {
  if (msg.type === 'state') {
    const before = state;
    state = msg;
    // On ne redessine la page en cours que si ce qu'elle montre a changé (pas pendant qu'on déplace un curseur).
    if (view === 'advanced' && before) renderStatus();
    else render();
  } else if (msg.type === 'game') {
    game = msg;
    if (view !== 'advanced') view = 'game';
    render();
  } else if (msg.type === 'steam') {
    steamGames = msg.games;
    if (!$('#modal').hidden) renderAdd();
  } else if (msg.type === 'exePicked') {
    exeForm.path = msg.path;
    if (!exeForm.name) exeForm.name = msg.name;
    renderAdd();
  } else if (msg.type === 'imagePicked') {
    exeForm.image = msg.path;
    renderAdd();
  }
}

if (host) host.addEventListener('message', (e) => onMessage(e.data));

// --- Aperçu dans un navigateur (sans l'application) : données d'exemple -----------------------------
function mock(msg) {
  const s = mock.s || (mock.s = {
    type: 'state', version: '0.6', theme: 'classic', desktop: true, startMenu: true, layer: 1, ready: true, readyText: 'Tout est prêt : lance ton jeu',
    checks: [{ ok: true, text: 'Stabilisation installée' }, { ok: true, text: 'Casque Pimax détecté' }, { ok: true, text: 'Détection des pertes de suivi active' }],
    pimaxVersion: 'pi_server (exemple)', running: { id: 'steam:1', appName: 'Exemple' },
    hands: [{ state: 'ok', text: 'Vue par les caméras' }, { state: 'lost', text: 'Hors champ : arme gardée en main' }],
    tremorPct: 73, caught: 6, defaultSummary: 'Équilibrée',
    games: [{ id: 'steam:1', name: 'Jeu d\'exemple', image: '', own: true, summary: 'Très stable · armes à deux mains', running: true }]
  });
  const settings = mock.set || (mock.set = { level: 3, lossGuard: true, twoHands: true, armModel: true, stillTauMs: 100, movingTauMs: 30, teleportBlendMs: 140, orientSmoothing: 75, maxLostDriftCm: 30, maxReachCm: 85, gripFix: false, gripHoldMs: 120 });
  setTimeout(() => {
    if (msg.cmd === 'ready') onMessage(s);
    else if (msg.cmd === 'theme') { s.theme = msg.value; onMessage(s); }
    else if (msg.cmd === 'shortcut') { s[msg.where === 'desktop' ? 'desktop' : 'startMenu'] = msg.value; onMessage(s); }
    else if (msg.cmd === 'getGame' || msg.cmd === 'set') {
      if (msg.cmd === 'set') settings[msg.key] = msg.value;
      const g = s.games.find((x) => x.id === msg.id);
      onMessage({ type: 'game', id: msg.id, name: g ? g.name : 'Réglages par défaut', image: '', own: true, settings });
    } else if (msg.cmd === 'listSteam') onMessage({ type: 'steam', games: [{ appId: 2, name: 'Autre jeu d\'exemple', added: false }] });
  }, 30);
}

send('ready');
