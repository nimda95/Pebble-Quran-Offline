/* Settings page shown in the Pebble app (gear icon next to Quran).
 * Built as a data: URL so nothing needs to be hosted. The byte layout matches
 * the Settings struct in src/c/state.h; the UI language travels beside it.
 * Texts come from lang/*.json (via the generated i18n_data.js). ES5 only. */
var I18N = require('./i18n_data');

var VERSION = 4;
// Option labels starting with '@' are page strings; others are shown as they are.
var FIELDS = [
  // [byte index (-1: not in the Settings bytes), id, label, options]
  [-1, 'lang', '@f_lang', ['@lang_auto'].concat(I18N.NAMES)],
  [1, 'zoom', '@f_zoom', ['15 px', '20 px', '26 px', '28 px', '32 px']],
  [7, 'font', '@f_font', ['@o_font_quran', '@o_font_system']],
  [8, 'sysfont', '@f_sysfont', ['@o_medium', '@o_large']],
  [3, 'scroll', '@f_scroll', ['@o_by_line', '@o_by_page']],
  [2, 'header', '@f_header', ['@o_off', '@o_surah_ayah', '@o_surah_ayah_juz', '@o_clock']],
  [4, 'theme', '@f_theme', ['@t_light', '@t_dark', '@t_night_red', '@t_night_amber', '@t_sepia',
    '@t_midnight', '@t_custom']],
  [5, 'light', '@f_light', ['@l_default', '@l_warm', '@l_amber', '@l_red', '@l_green', '@l_blue']],
  [9, 'trans', '@f_trans', ['@o_none', 'English: Sahih International', 'English: Mustafa Khattab',
    'English: Abdel Haleem', 'English: Yusuf Ali', 'French: Hamidullah', 'German: Bubenheim & Elyas',
    'Spanish: Isa Garcia', 'Turkish: Diyanet', 'Indonesian: Kemenag', 'Malay: Basmeih', 'Dutch: Siregar',
    'Italian: Piccardo', 'Portuguese: El Hayek', 'Bosnian: Korkut']],
  [6, 'render', '@f_render', ['@o_render_auto', '@o_render_watch']],
  [10, 'glance', '@f_glance', ['@o_off', '@o_on']]
];
var GROUPS = [
  ['@g_language', ['lang']],
  ['@g_reading', ['zoom', 'scroll', 'header']],
  ['@g_arabic', ['font', 'sysfont']],
  ['@g_appearance', ['theme', 'light']],
  ['@g_translation', ['trans']],
  ['@g_advanced', ['render', 'glance']]
];

function defaults() { return [VERSION, 3, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0xFE, 0xC4]; }

function esc(s) {
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function field(id) {
  for (var i = 0; i < FIELDS.length; i++) if (FIELDS[i][1] === id) return FIELDS[i];
  return null;
}

// Language code for a language setting (0 = automatic: the given locale, e.g. "fr-FR").
function langCode(setting, locale) {
  if (setting > 0 && setting <= I18N.CODES.length) return I18N.CODES[setting - 1];
  var n = String(locale || 'en').toLowerCase().slice(0, 2);
  if (n === 'in') n = 'id';
  return I18N.PAGE[n] ? n : 'en';
}

// Text of a label, and the attribute that lets the page switch language live.
function label(T, s) {
  if (s.charAt(0) !== '@') return { text: s, attr: '' };
  return { text: T[s.slice(1)], attr: ' data-t="' + s.slice(1) + '"' };
}

// Pebble's 64 colours: GColor8 argb = 0b11rrggbb
function hex(argb) {
  var v = [0, 0x55, 0xAA, 0xFF], s = '#';
  var ch = [(argb >> 4) & 3, (argb >> 2) & 3, argb & 3];
  for (var i = 0; i < 3; i++) s += (v[ch[i]] < 16 ? '0' : '') + v[ch[i]].toString(16);
  return s;
}

function swatches(id, cur) {
  var h = '<div class="sw" id="' + id + '">';
  for (var c = 0; c < 64; c++) {
    var a = 0xC0 | c;
    h += '<i data-c="' + a + '" style="background:' + hex(a) + '"' + (a === cur ? ' class="on"' : '') + '></i>';
  }
  return h + '</div>';
}

function span(T, key) { return '<span data-t="' + key + '">' + esc(T[key]) + '</span>'; }

// bytes: Settings bytes; lang: UI language setting (0 = automatic)
function url(bytes, lang) {
  lang = lang | 0;
  var locale = (typeof navigator !== 'undefined' && navigator.language) || 'en';
  var code = langCode(lang, locale), T = I18N.PAGE[code];
  var body = '';
  for (var g = 0; g < GROUPS.length; g++) {
    var gl = label(T, GROUPS[g][0]);
    body += '<h2' + gl.attr + '>' + esc(gl.text) + '</h2><div class="card">';
    var ids = GROUPS[g][1];
    for (var k = 0; k < ids.length; k++) {
      var f = field(ids[k]), cur = f[0] < 0 ? lang : bytes[f[0]] | 0, fl = label(T, f[2]);
      body += '<label><span' + fl.attr + '>' + esc(fl.text) + '</span><select id="' + f[1] + '">';
      for (var o = 0; o < f[3].length; o++) {
        var ol = label(T, f[3][o]);
        body += '<option value="' + o + '"' + ol.attr + (o === cur ? ' selected' : '') + '>' + esc(ol.text) +
                '</option>';
      }
      body += '</select></label>';
      if (f[1] === 'lang') body += '<p class="note" id="arnote" data-t="lang_ar_note">' + esc(T.lang_ar_note) + '</p>';
      if (f[1] === 'theme') {
        body += '<div id="custom"><label>' + span(T, 'c_paper') + swatches('paper', bytes[11] | 0xC0) + '</label>' +
                '<label>' + span(T, 'c_ink') + swatches('ink', bytes[12] | 0xC0) + '</label>' +
                '<label>' + span(T, 'c_preview') + '<div id="pv" dir="rtl">بِسْمِ ' +
                'اللَّهِ الرَّحْمَ' +
                'ٰنِ</div></label></div>';
      }
    }
    body += '</div>';
  }
  var ids = [];
  for (var i = 0; i < FIELDS.length; i++) ids.push(FIELDS[i][1]);
  var html =
    '<!doctype html><html lang="' + code + '" dir="' + (code === 'ar' ? 'rtl' : 'ltr') + '"><head><meta charset="utf-8">' +
    '<meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<title>' + esc(T.page_title) + '</title><style>' +
    ':root{--bg:#f4f4f1;--card:#fff;--fg:#1d1d1b;--muted:#6b6b66;--accent:#0b6b3a;--line:#e2e2dc}' +
    '@media (prefers-color-scheme:dark){:root{--bg:#111;--card:#1c1c1c;--fg:#eee;--muted:#9a9a94;--accent:#3fb472;--line:#2c2c2c}}' +
    'body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.4 -apple-system,system-ui,Roboto,sans-serif;padding:16px 16px 96px}' +
    'h1{font-size:22px;margin:4px 0 2px}p.sub{margin:0 0 8px;color:var(--muted);font-size:14px}' +
    'h2{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);margin:22px 4px 6px}' +
    '.card{background:var(--card);border-radius:12px;border:1px solid var(--line);overflow:hidden}' +
    'label{display:block;padding:12px 14px;border-top:1px solid var(--line)}label:first-child{border-top:0}' +
    'label span{display:block;font-size:14px;color:var(--muted);margin-bottom:6px}' +
    'p.note{margin:0;padding:0 14px 12px;font-size:13px;color:var(--muted)}' +
    'select{width:100%;font:inherit;color:var(--fg);background:var(--bg);border:1px solid var(--line);border-radius:8px;padding:9px 10px}' +
    '.bar{position:fixed;left:0;right:0;bottom:0;padding:12px 16px calc(12px + env(safe-area-inset-bottom));background:var(--bg);border-top:1px solid var(--line)}' +
    '.sw{display:grid;grid-template-columns:repeat(8,1fr);gap:4px}' +
    '.sw i{display:block;aspect-ratio:1;border-radius:6px;border:1px solid var(--line)}' +
    '.sw i.on{outline:3px solid var(--accent);outline-offset:1px}' +
    '#pv{font-size:30px;padding:10px 12px;border-radius:8px;text-align:center}' +
    'button{width:100%;font:600 17px/1 inherit;font-family:inherit;color:#fff;background:var(--accent);border:0;border-radius:10px;padding:15px}' +
    '</style></head><body>' +
    '<h1 data-t="heading">' + esc(T.heading) + '</h1><p class="sub" data-t="intro">' + esc(T.intro) + '</p>' + body +
    '<div class="bar"><button id="save" data-t="save">' + esc(T.save) + '</button></div>' +
    '<script>var $=function(i){return document.getElementById(i)};' +
    'var col={paper:' + (bytes[11] | 0xC0) + ',ink:' + (bytes[12] | 0xC0) + '};' +
    // page strings of every language, so the page follows the language select at once
    'var L=' + JSON.stringify(I18N.PAGE).replace(/</g, '\\u003c') + ',C=' + JSON.stringify(I18N.CODES) + ';' +
    'function code(v){if(v>0)return C[v-1];var n=(navigator.language||"en").toLowerCase().slice(0,2);' +
    'if(n=="in")n="id";return L[n]?n:"en"}' +
    'function tr(){var c=code(+$("lang").value),t=L[c],e=document.querySelectorAll("[data-t]");' +
    'document.documentElement.lang=c;document.documentElement.dir=c=="ar"?"rtl":"ltr";document.title=t.page_title;' +
    'for(var i=0;i<e.length;i++){var k=e[i].getAttribute("data-t");e[i].textContent=t[k]||L.en[k]}' +
    '$("arnote").style.display=c=="ar"?"":"none"}' +
    'function hex(a){var v=["00","55","aa","ff"];return "#"+v[(a>>4)&3]+v[(a>>2)&3]+v[a&3]}' +
    'function upd(){$("custom").style.display=$("theme").value=="6"?"":"none";' +
    '$("pv").style.background=hex(col.paper);$("pv").style.color=hex(col.ink)}' +
    '["paper","ink"].forEach(function(k){$(k).onclick=function(e){var c=e.target.getAttribute("data-c");if(!c)return;' +
    'col[k]=+c;var l=$(k).children;for(var i=0;i<l.length;i++)l[i].className=l[i]===e.target?"on":"";upd()}});' +
    '$("theme").onchange=upd;upd();$("lang").onchange=tr;tr();' +
    '$("save").onclick=function(){var ids=' + JSON.stringify(ids) +
    ',r={paper:col.paper,ink:col.ink};for(var i=0;i<ids.length;i++)r[ids[i]]=+$(ids[i]).value;' +
    'location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify(r));};</script>' +
    '</body></html>';
  return 'data:text/html;charset=utf-8,' + encodeURIComponent(html);
}

// Returns { bytes: new Settings bytes, lang: UI language } from the page's
// response, or null if cancelled.
function parse(response, current, currentLang) {
  var r;
  try { r = JSON.parse(decodeURIComponent(response)); } catch (e) {
    try { r = JSON.parse(response); } catch (e2) { return null; }
  }
  if (!r || typeof r !== 'object') return null;
  var b = current.slice(0), lang = currentLang | 0;
  b[0] = VERSION;
  for (var i = 0; i < FIELDS.length; i++) {
    var f = FIELDS[i], v = r[f[1]];
    if (typeof v !== 'number' || v < 0 || v >= f[3].length || v !== Math.floor(v)) continue;
    if (f[0] < 0) lang = v;
    else b[f[0]] = v;
  }
  if (typeof r.paper === 'number') b[11] = (r.paper & 0x3F) | 0xC0;
  if (typeof r.ink === 'number') b[12] = (r.ink & 0x3F) | 0xC0;
  while (b.length < 13) b.push(0);
  return { bytes: b, lang: lang };
}

module.exports = { url: url, parse: parse, defaults: defaults, langCode: langCode };
