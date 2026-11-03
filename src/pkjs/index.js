/* Pebble Quran: phone side (PebbleKit JS inside the Pebble app).
 *  - renders text lines for the watch (same renderer and data as the watch)
 *  - fetches the Arabic text (system-font view) and translations
 *  - hosts the settings page (no web hosting needed: it is a data: URL), in the
 *    UI language chosen there (lang/*.json)
 * ES5 only. */
var R = require('./render');
var BLOB = require('./data_blob');
var config = require('./config');
var Ar = require('./arabic');

var CMD = { READY: 1, HELLO: 2, LINES_REQ: 3, LINES: 4, SETTINGS: 5, TEXT_REQ: 6, TEXT: 7 };
var MAX_DATA = 7400;            // strip bytes per message (watch inbox is 8200 bytes; see HELLO)
var SETTINGS_LEN = 13;

// Keep in the same order as TRANS_NAMES in src/c/state.c.
var EDITIONS = [null, 'eng-ummmuhammad', 'eng-mustafakhattaba', 'eng-abdelhaleem', 'eng-abdullahyusufal',
  'fra-muhammadhamidul', 'deu-asfbubenheimand', 'spa-muhammadisagarc', 'tur-diyanetisleri',
  'ind-indonesianislam', 'msa-abdullahmuhamma', 'nld-sofianssiregar', 'ita-hamzarobertopic',
  'por-samirelhayek', 'bos-besimkorkut'];

function log(s) { console.log('quran: ' + s); }

// ------------------------------------------------------------------ outbox
var queue = [], sending = false;

function send(msg, done) {
  queue.push({ msg: msg, done: done, tries: 0 });
  pump();
}

function pump() {
  if (sending || !queue.length) return;
  var item = queue.shift();
  sending = true;
  Pebble.sendAppMessage(item.msg, function () {
    sending = false;
    if (item.done) item.done(true);
    pump();
  }, function () {
    sending = false;
    if (++item.tries < 3) { queue.unshift(item); setTimeout(pump, 200); return; }
    if (item.done) item.done(false);
    pump();
  });
}

// ------------------------------------------------------------------ settings
function loadSettings() {
  try {
    var s = JSON.parse(localStorage.getItem('settings') || 'null');
    if (s && s.length === SETTINGS_LEN) return s;
  } catch (e) { /* ignore */ }
  return null;
}

function saveSettings(bytes) {
  try { localStorage.setItem('settings', JSON.stringify(bytes)); } catch (e) { /* ignore */ }
}

// UI language (0 = automatic), kept beside the Settings bytes
function loadLang() {
  try { return parseInt(localStorage.getItem('lang'), 10) || 0; } catch (e) { return 0; }
}

function saveLang(v) {
  try { localStorage.setItem('lang', String(v)); } catch (e) { /* ignore */ }
}

var settings = loadSettings();
var lang = loadLang();

// ------------------------------------------------------------------ lines
var job = null;                 // { req, zoom, g, l, left }

function nextLine(zoom, width, p) {
  if (p.l + 1 < R.blockLines(zoom, p.g, width)) { p.l++; return true; }
  if (p.g + 1 >= R.N_AYAH) return false;
  p.g++; p.l = 0;
  return true;
}

function sendBatch() {
  if (!job) return;
  var j = job, out = [], p = { g: j.g, l: j.l }, done = false;
  while (j.left > 0) {
    var at = out.length;
    R.renderStrip(j.zoom, p.g, p.l, out, j.width);
    if (out.length > MAX_DATA && at > 0) { out.length = at; break; }   // goes in the next message
    j.left--;
    if (!nextLine(j.zoom, j.width, p)) { j.left = 0; break; }
    j.g = p.g; j.l = p.l;
  }
  if (j.left <= 0) done = true;
  send({ CMD: CMD.LINES, REQ: j.req, ZOOM: j.zoom, DATA: out, FLAGS: done ? 1 : 0 }, function () {
    if (job === j && !done) sendBatch();
    else if (job === j) job = null;
  });
}

function onLinesRequest(d) {
  // a newer request replaces whatever is still queued for the old one
  queue = queue.filter(function (it) { return it.msg.CMD !== CMD.LINES; });
  var zoom = d.ZOOM | 0;
  if (zoom < 0 || zoom >= R.ZOOMS) return;
  // (a batch of the old request still in flight finishes, but is not continued)
  var width = d.WIDTH | 0;
  if (width < 32 || width > 400) width = 200;
  job = { req: d.REQ >>> 0, zoom: zoom, width: width, g: d.AYAH | 0, l: d.LINE | 0, left: Math.min(d.COUNT | 0, 64) };
  sendBatch();
}

// ------------------------------------------------------------------ text
var chapters = {};               // 'edition/chapter' -> [verse texts]
var chapterOrder = [];
var pending = {};                // 'edition/chapter' -> [callbacks]

function sources(ed, ch) {
  var p = '/editions/' + ed + '/' + ch;
  return ['https://cdn.jsdelivr.net/gh/fawazahmed0/quran-api@1' + p + '.min.json',
          'https://raw.githubusercontent.com/fawazahmed0/quran-api/1' + p + '.json',
          'https://cdn.jsdelivr.net/gh/fawazahmed0/quran-api@1' + p + '.json'];
}

function remember(key, verses) {
  chapters[key] = verses;
  chapterOrder.push(key);
  while (chapterOrder.length > 12) delete chapters[chapterOrder.shift()];
  try {
    localStorage.setItem('c:' + key, JSON.stringify(verses));
    var idx = JSON.parse(localStorage.getItem('c:index') || '[]');
    idx = idx.filter(function (k) { return k !== key; });
    idx.push(key);
    while (idx.length > 40) localStorage.removeItem('c:' + idx.shift());
    localStorage.setItem('c:index', JSON.stringify(idx));
  } catch (e) { /* storage full: memory cache only */ }
}

function getChapter(ed, ch, cb) {
  var key = ed + '/' + ch;
  if (chapters[key]) return cb(chapters[key]);
  try {
    var cached = localStorage.getItem('c:' + key);
    if (cached) { chapters[key] = JSON.parse(cached); return cb(chapters[key]); }
  } catch (e) { /* ignore */ }
  if (pending[key]) { pending[key].push(cb); return; }
  pending[key] = [cb];
  var urls = sources(ed, ch);
  var finish = function (verses) {
    var cbs = pending[key] || [];
    delete pending[key];
    if (verses) remember(key, verses);
    for (var i = 0; i < cbs.length; i++) {
      try { cbs[i](verses); } catch (e) { log('chapter callback: ' + e); }
    }
  };
  var attempt = function (i) {
    if (i >= urls.length) return finish(null);
    var xhr = new XMLHttpRequest();
    xhr.open('GET', urls[i], true);
    xhr.timeout = 15000;
    xhr.onload = function () {
      if (xhr.status !== 200) return attempt(i + 1);
      var verses = null;
      try {
        var list = JSON.parse(xhr.responseText).chapter;
        verses = [];
        for (var k = 0; k < list.length; k++) verses[list[k].verse - 1] = list[k].text;
      } catch (e) { verses = null; }
      if (!verses) return attempt(i + 1);
      finish(verses);
    };
    xhr.onerror = xhr.ontimeout = function () { attempt(i + 1); };
    xhr.send();
  };
  attempt(0);
}

var TF = { ARABIC: 1, TRANS: 2, DIGITS: 4 };
var SEP = '\u001e';
var MAX_TEXT_BYTES = 6800;
var latestTextReq = {};          // per kind (Arabic / translation): only the newest request is answered

function utf8len(s) { return unescape(encodeURIComponent(s)).length; }

function marker(n, arabicDigits) {
  var d = String(n);
  if (arabicDigits) {
    var o = '';
    for (var i = 0; i < d.length; i++) o += String.fromCharCode(0x0660 + d.charCodeAt(i) - 48);
    d = o;
  }
  return '\u001f(' + d + ')';      // joined to the last word: never wraps alone
}

function clip(s, maxBytes) {
  if (utf8len(s) <= maxBytes) return s;
  while (s.length && utf8len(s) > maxBytes - 3) s = s.slice(0, -16);
  return s + '…';
}

function onTextRequest(d) {
  var first = d.AYAH | 0, count = Math.max(1, Math.min(d.COUNT | 0 || 1, 40)), flags = d.FLAGS | 0;
  if (first < 0 || first >= R.N_AYAH) return;
  if (first + count > R.N_AYAH) count = R.N_AYAH - first;
  var kind = (flags & TF.ARABIC) ? 'ar' : 'tr';
  var req = d.REQ >>> 0;
  latestTextReq[kind] = req;
  var ed = EDITIONS[d.LINE | 0] || null;
  var si = R.surahOf(first), surah = si + 1;
  var surahEnd = R.meta().surahs[si].first + R.meta().surahs[si].count;
  if (flags & TF.TRANS) count = Math.min(count, surahEnd - first);   // one chapter per fetch

  var finish = function (trVerses, trFailed) {
    if (latestTextReq[kind] !== req) return;                         // superseded
    var ar = [], tr = [], bytes = 0, n = 0, errors = 0;
    for (var g = first; g < first + count; g++) {
      var a = '', t = '';
      if (flags & TF.ARABIC) a = Ar.ayah(g) + marker(R.ayahNo(g), flags & TF.DIGITS);
      if (flags & TF.TRANS) {
        t = trVerses ? (trVerses[R.ayahNo(g) - 1] || '') : '';
        t = clip(t.replace(/\s+/g, ' ').trim(), 3000);
      }
      var add = utf8len(a) + utf8len(t) + 2;
      if (n > 0 && bytes + add > MAX_TEXT_BYTES) break;
      ar.push(clip(a, 3000)); tr.push(t);
      bytes += add; n++;
    }
    if ((flags & TF.TRANS) && (trFailed || !ed)) errors |= 2;
    var msg = { CMD: CMD.TEXT, REQ: req, AYAH: first, COUNT: n, FLAGS: errors };
    if (flags & TF.ARABIC) msg.TEXT = ar.join(SEP);
    if (flags & TF.TRANS) msg.TEXT2 = tr.join(SEP);
    send(msg);
  };
  if (!(flags & TF.TRANS)) return finish(null, false);
  if (!ed) return finish(null, true);
  getChapter(ed, surah, function (verses) {
    try { finish(verses, !verses); } catch (e) { log('text reply: ' + e); }
  });
}

// ------------------------------------------------------------------ events
function sendReady() { send({ CMD: CMD.READY, VER: BLOB.VERSION }); }

Pebble.addEventListener('ready', function () {
  sendReady();
  // decode the data now so the first page request is quick
  setTimeout(function () { try { R.meta(); Ar.init(); } catch (e) { log(e); } }, 50);
});

Pebble.addEventListener('appmessage', function (e) {
  var d = e.payload;
  switch (d.CMD) {
    case CMD.HELLO:
      if ((d.COUNT | 0) > 1000) MAX_DATA = Math.min(7400, (d.COUNT | 0) - 800);
      sendReady();
      break;
    case CMD.LINES_REQ: onLinesRequest(d); break;
    case CMD.TEXT_REQ: onTextRequest(d); break;
    case CMD.SETTINGS:
      if (d.DATA && d.DATA.length >= SETTINGS_LEN) {
        settings = Array.prototype.slice.call(d.DATA, 0, SETTINGS_LEN);
        saveSettings(settings);
      }
      if (typeof d.LANG === 'number') { lang = d.LANG; saveLang(lang); }
      break;
  }
});

Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(config.url(settings || config.defaults(), lang));
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  var r = config.parse(e.response, settings || config.defaults(), lang);
  if (!r) return;
  settings = r.bytes;
  lang = r.lang;
  saveSettings(settings);
  saveLang(lang);
  send({ CMD: CMD.SETTINGS, DATA: settings, LANG: lang });
});
