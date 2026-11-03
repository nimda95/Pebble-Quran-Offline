/* Settings page shown in the Pebble app (gear icon next to Quran).
 * Built as a data: URL so nothing needs to be hosted. The byte layout matches
 * the Settings struct in src/c/state.h. ES5 only. */

var VERSION = 4;
var FIELDS = [
  // [byte index, id, label, options]
  [1, 'zoom', 'Text size', ['15 px', '20 px', '26 px', '28 px', '32 px']],
  [7, 'font', 'Arabic text', ['Quran font (built in)', 'Pebble system font']],
  [8, 'sysfont', 'System font size', ['Medium (24)', 'Large (28)']],
  [3, 'scroll', 'Scrolling', ['By line', 'By page']],
  [2, 'header', 'Header', ['Off', 'Surah & ayah', 'Surah, ayah & juz\'', 'Clock']],
  [4, 'theme', 'Theme', ['Light', 'Dark', 'Night (red on black)', 'Night (amber on black)', 'Sepia (black on cream)',
    'Midnight blue', 'Custom']],
  [5, 'light', 'Backlight colour (hold Select while reading)', ['Default', 'Warm white', 'Amber', 'Red', 'Green', 'Blue']],
  [9, 'trans', 'Translation', ['None', 'English: Sahih International', 'English: Mustafa Khattab',
    'English: Abdel Haleem', 'English: Yusuf Ali', 'French: Hamidullah', 'German: Bubenheim & Elyas',
    'Spanish: Isa Garcia', 'Turkish: Diyanet', 'Indonesian: Kemenag', 'Malay: Basmeih', 'Dutch: Siregar',
    'Italian: Piccardo', 'Portuguese: El Hayek', 'Bosnian: Korkut']],
  [6, 'render', 'Rendering', ['Phone when connected, watch otherwise', 'Always on the watch']],
  [10, 'glance', 'Reading position in the app list', ['Off', 'On']]
];
var GROUPS = [
  ['Reading', ['zoom', 'scroll', 'header']],
  ['Arabic text', ['font', 'sysfont']],
  ['Appearance', ['theme', 'light']],
  ['Translation', ['trans']],
  ['Advanced', ['render', 'glance']]
];

function defaults() { return [VERSION, 3, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0xFE, 0xC4]; }

function esc(s) {
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function field(id) {
  for (var i = 0; i < FIELDS.length; i++) if (FIELDS[i][1] === id) return FIELDS[i];
  return null;
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

function url(bytes) {
  var body = '';
  for (var g = 0; g < GROUPS.length; g++) {
    body += '<h2>' + esc(GROUPS[g][0]) + '</h2><div class="card">';
    var ids = GROUPS[g][1];
    for (var k = 0; k < ids.length; k++) {
      var f = field(ids[k]), cur = bytes[f[0]] | 0;
      body += '<label><span>' + esc(f[2]) + '</span><select id="' + f[1] + '">';
      for (var o = 0; o < f[3].length; o++)
        body += '<option value="' + o + '"' + (o === cur ? ' selected' : '') + '>' + esc(f[3][o]) + '</option>';
      body += '</select></label>';
      if (f[1] === 'theme') {
        body += '<div id="custom"><label><span>Paper</span>' + swatches('paper', bytes[11] | 0xC0) + '</label>' +
                '<label><span>Ink</span>' + swatches('ink', bytes[12] | 0xC0) + '</label>' +
                '<label><span>Preview</span><div id="pv" dir="rtl">\u0628\u0650\u0633\u0652\u0645\u0650 ' +
                '\u0627\u0644\u0644\u0651\u064e\u0647\u0650 \u0627\u0644\u0631\u0651\u064e\u062d\u0652\u0645\u064e' +
                '\u0670\u0646\u0650</div></label></div>';
      }
    }
    body += '</div>';
  }
  var ids = [];
  for (var i = 0; i < FIELDS.length; i++) ids.push(FIELDS[i][1]);
  var html =
    '<!doctype html><html><head><meta charset="utf-8">' +
    '<meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<title>Quran settings</title><style>' +
    ':root{--bg:#f4f4f1;--card:#fff;--fg:#1d1d1b;--muted:#6b6b66;--accent:#0b6b3a;--line:#e2e2dc}' +
    '@media (prefers-color-scheme:dark){:root{--bg:#111;--card:#1c1c1c;--fg:#eee;--muted:#9a9a94;--accent:#3fb472;--line:#2c2c2c}}' +
    'body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.4 -apple-system,system-ui,Roboto,sans-serif;padding:16px 16px 96px}' +
    'h1{font-size:22px;margin:4px 0 2px}p.sub{margin:0 0 8px;color:var(--muted);font-size:14px}' +
    'h2{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);margin:22px 4px 6px}' +
    '.card{background:var(--card);border-radius:12px;border:1px solid var(--line);overflow:hidden}' +
    'label{display:block;padding:12px 14px;border-top:1px solid var(--line)}label:first-child{border-top:0}' +
    'label span{display:block;font-size:14px;color:var(--muted);margin-bottom:6px}' +
    'select{width:100%;font:inherit;color:var(--fg);background:var(--bg);border:1px solid var(--line);border-radius:8px;padding:9px 10px}' +
    '.bar{position:fixed;left:0;right:0;bottom:0;padding:12px 16px calc(12px + env(safe-area-inset-bottom));background:var(--bg);border-top:1px solid var(--line)}' +
    '.sw{display:grid;grid-template-columns:repeat(8,1fr);gap:4px}' +
    '.sw i{display:block;aspect-ratio:1;border-radius:6px;border:1px solid var(--line)}' +
    '.sw i.on{outline:3px solid var(--accent);outline-offset:1px}' +
    '#pv{font-size:30px;padding:10px 12px;border-radius:8px;text-align:center}' +
    'button{width:100%;font:600 17px/1 inherit;font-family:inherit;color:#fff;background:var(--accent);border:0;border-radius:10px;padding:15px}' +
    '</style></head><body>' +
    '<h1>Quran</h1><p class="sub">Changes apply to the watch when you save.</p>' + body +
    '<div class="bar"><button id="save">Save</button></div>' +
    '<script>var $=function(i){return document.getElementById(i)};' +
    'var col={paper:' + (bytes[11] | 0xC0) + ',ink:' + (bytes[12] | 0xC0) + '};' +
    'function hex(a){var v=["00","55","aa","ff"];return "#"+v[(a>>4)&3]+v[(a>>2)&3]+v[a&3]}' +
    'function upd(){$("custom").style.display=$("theme").value=="6"?"":"none";' +
    '$("pv").style.background=hex(col.paper);$("pv").style.color=hex(col.ink)}' +
    '["paper","ink"].forEach(function(k){$(k).onclick=function(e){var c=e.target.getAttribute("data-c");if(!c)return;' +
    'col[k]=+c;var l=$(k).children;for(var i=0;i<l.length;i++)l[i].className=l[i]===e.target?"on":"";upd()}});' +
    '$("theme").onchange=upd;upd();' +
    '$("save").onclick=function(){var ids=' + JSON.stringify(ids) +
    ',r={paper:col.paper,ink:col.ink};for(var i=0;i<ids.length;i++)r[ids[i]]=+$(ids[i]).value;' +
    'location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify(r));};</script>' +
    '</body></html>';
  return 'data:text/html;charset=utf-8,' + encodeURIComponent(html);
}

// Returns new settings bytes from the page's response, or null if cancelled.
function parse(response, current) {
  var r;
  try { r = JSON.parse(decodeURIComponent(response)); } catch (e) {
    try { r = JSON.parse(response); } catch (e2) { return null; }
  }
  if (!r || typeof r !== 'object') return null;
  var b = current.slice(0);
  b[0] = VERSION;
  for (var i = 0; i < FIELDS.length; i++) {
    var f = FIELDS[i], v = r[f[1]];
    if (typeof v === 'number' && v >= 0 && v < f[3].length) b[f[0]] = v;
  }
  if (typeof r.paper === 'number') b[11] = (r.paper & 0x3F) | 0xC0;
  if (typeof r.ink === 'number') b[12] = (r.ink & 0x3F) | 0xC0;
  while (b.length < 13) b.push(0);
  return b;
}

module.exports = { url: url, parse: parse, defaults: defaults };
