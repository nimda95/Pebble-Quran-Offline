// node tools/host/test_lang.js
// Checks the UI translations (lang/*.json) and the files made from them by
// tools/gen_lang.py:
//  - every language has every watch and page string, and no unknown ones
//  - printf conversions and leading/trailing spaces match English
//  - watch strings only use characters the Pebble system fonts have
//  - Arabic watch strings stay under the firmware's right-to-left line limit
//    (~32 code points / ~62 bytes per line, see src/c/systext.c), with
//    formatted values filled in
//  - LANG.bin, i18n_ids.h and i18n_data.js are up to date
//  - every string id is used by the watch code, every page key exists
//  - the settings page builds in every language and its answer parses
var fs = require('fs'), path = require('path');
var ROOT = path.join(__dirname, '..', '..');
var RTL_MAX_CP = 30, RTL_MAX_BYTES = 58;      // same margins as systext.c

var errors = [];
function fail(msg) { errors.push(msg); }
function read(p) { return fs.readFileSync(path.join(ROOT, p), 'utf8'); }

var I18N = require(path.join(ROOT, 'src', 'pkjs', 'i18n_data.js'));
var CODES = I18N.CODES;
var L = {};
CODES.forEach(function (c) { L[c] = JSON.parse(read('lang/' + c + '.json')); });
var EN = L.en;
var WKEYS = Object.keys(EN.watch), PKEYS = Object.keys(EN.page);

function convs(s) { return (s.match(/%[-0-9.]*l?[sdu%]/g) || []).join(' '); }
function edges(s) { return [/^\s*/.exec(s)[0], /\s*$/.exec(s)[0]].join('|'); }
function cps(s) { return Array.from(s).length; }
function bytes(s) { return Buffer.byteLength(s, 'utf8'); }

// ---------------------------------------------------------------- completeness
CODES.forEach(function (c) {
  var d = L[c];
  if (!d.name) fail(c + ': no "name"');
  ['watch', 'page'].forEach(function (sec) {
    var keys = sec === 'watch' ? WKEYS : PKEYS, have = d[sec] || {};
    keys.forEach(function (k) {
      var v = have[k];
      if (typeof v !== 'string' || !v.length) { fail(c + ': ' + sec + '.' + k + ' missing'); return; }
      var e = EN[sec][k];
      if (convs(v) !== convs(e)) fail(c + ': ' + sec + '.' + k + ' conversions "' + convs(v) + '" != "' + convs(e) + '"');
      if (edges(v) !== edges(e)) fail(c + ': ' + sec + '.' + k + ' leading/trailing spaces differ from English');
      if (sec === 'watch' && (v.match(/\n/g) || []).length > 8) fail(c + ': watch.' + k + ' has too many lines');
    });
    Object.keys(have).forEach(function (k) {
      if (keys.indexOf(k) < 0) fail(c + ': unknown ' + sec + '.' + k);
    });
  });
});

// ---------------------------------------------------------------- characters (watch)
function fontHas(cp, arabic) {
  if (cp === 10 || (cp >= 32 && cp < 127)) return true;
  if (cp >= 0xA0 && cp <= 0x17F) return true;                     // Latin-1, Latin Extended-A
  if ([0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026].indexOf(cp) >= 0) return true;
  return arabic && cp >= 0x600 && cp <= 0x6FF;                      // needs the Arabic glyph pack
}
CODES.forEach(function (c) {
  WKEYS.forEach(function (k) {
    var v = L[c].watch[k] || '';
    Array.from(v).forEach(function (ch) {
      if (!fontHas(ch.codePointAt(0), c === 'ar'))
        fail(c + ': watch.' + k + ' uses U+' + ch.codePointAt(0).toString(16).toUpperCase() + ' (not in the system font)');
    });
  });
});

// ---------------------------------------------------------------- Arabic line limit
var AR = L.ar.watch;
var SURAH_AR = [];
read('src/c/names_ar.c').split('\n').forEach(function (line) {
  var m = /\/\/ (\d+) (.+)$/.exec(line);
  if (m) SURAH_AR[+m[1] - 1] = m[2];
});
if (SURAH_AR.length !== 114) fail('names_ar.c: expected 114 Arabic surah names, found ' + SURAH_AR.length);
function longest(list) { return list.reduce(function (a, b) { return cps(b) > cps(a) ? b : a; }, ''); }
function pick(prefix) { return WKEYS.filter(function (k) { return k.indexOf(prefix) === 0; }).map(function (k) { return AR[k]; }); }
var LONG_SURAH = longest(SURAH_AR);
// Values a %s can take, per string (the default covers short values)
var S_VALUES = {
  HOLD_SELECT: longest(pick('LIGHT_')),
  RENDERS_ON: longest(pick('SRC_'))
};
function fill(k, s) {
  return s.replace(/%(l?u|d)/g, '00000').replace(/%s/g, S_VALUES[k] || longest(pick('THEME_')));
}
function checkRtl(label, s) {
  s.split('\n').forEach(function (line, i) {
    if (cps(line) > RTL_MAX_CP || bytes(line) > RTL_MAX_BYTES)
      fail('ar: ' + label + ' line ' + (i + 1) + ' is ' + cps(line) + ' code points / ' + bytes(line) +
           ' bytes (max ' + RTL_MAX_CP + ' / ' + RTL_MAX_BYTES + '): ' + line);
  });
}
WKEYS.forEach(function (k) { checkRtl('watch.' + k, fill(k, AR[k])); });
// strings the watch code puts together (see reader.c, menus.c, picker.c, textview.c, main.c)
var juz = AR.JUZ_N.replace('%d', '30');
checkRtl('reader header', LONG_SURAH + ' 114:286  ' + juz + ' •');
checkRtl('position', LONG_SURAH + ' 114:286');
checkRtl('bookmark', '114:286  ' + juz);
checkRtl('picker title', '114. ' + LONG_SURAH);
checkRtl('theme in action menu', longest(pick('THEME_')) + AR.NOW);
checkRtl('about', 'v2.0  ' + fill('KB_FREE', AR.KB_FREE));
checkRtl('language name', I18N.NAMES[CODES.indexOf('ar')]);

// ---------------------------------------------------------------- watch buffers (bytes incl. the NUL)
var BUFS = [
  ['dialog title (ui.c)', 64, function (W) { return [W.RESET_TITLE, W.DATA_MISSING]; }],
  ['dialog body (ui.c)', 300, function (W) { return [W.RESET_BODY, W.DATA_MISSING_BODY]; }],
  ['theme in action menu (reader.c)', 48, function (W) { return pick2(W, 'THEME_').map(function (t) { return t + W.NOW; }); }],
  ['text size in action menu (reader.c)', 24, function (W) { return ['32 px' + W.NOW]; }],
  ['juz / hizb (menus.c, reader.c)', 24, function (W) { return [W.JUZ_N, W.HIZB_N].map(function (s) { return s.replace('%d', '60'); }); }],
  ['reader header (reader.c)', 64, function (W, c) { return [surah(c) + ' 114:286  ' + W.JUZ_N.replace('%d', '30') + ' •']; }],
  ['picker hint (picker.c)', 128, function (W) { return [W.AYAH_RANGE.replace('%d', '286') + '\n' + W.PICKER_HELP]; }],
  ['settings value (menus.c)', 64, function (W) {
    return pick2(W, 'LIGHT_').map(function (l) { return W.HOLD_SELECT.replace('%s', l); })
      .concat(pick2(W, 'SRC_').map(function (s) { return W.RENDERS_ON.replace('%s', s); }));
  }],
  ['status title (menus.c)', 48, function (W) { return [W.STATUS.replace(/%lu/g, '4294967295')]; }]
];
function pick2(W, prefix) { return WKEYS.filter(function (k) { return k.indexOf(prefix) === 0; }).map(function (k) { return W[k]; }); }
function surah(c) { return c === 'ar' ? LONG_SURAH : 'Al-Mumtahanah'; }
CODES.forEach(function (c) {
  BUFS.forEach(function (b) {
    b[2](L[c].watch, c).forEach(function (s) {
      if (bytes(s) + 1 > b[1]) fail(c + ': ' + b[0] + ' needs ' + (bytes(s) + 1) + ' bytes, buffer has ' + b[1] + ': ' + s);
    });
  });
});

// ---------------------------------------------------------------- generated files
var bin = fs.readFileSync(path.join(ROOT, 'resources', 'data', 'LANG.bin'));
if (bin.toString('latin1', 0, 4) !== 'QLG1') fail('LANG.bin: bad magic');
var nl = bin[4], ns = bin.readUInt16LE(6);
if (nl !== CODES.length || ns !== WKEYS.length) fail('LANG.bin: ' + nl + ' languages / ' + ns + ' strings, expected ' +
                                                     CODES.length + ' / ' + WKEYS.length + ' (run tools/gen_lang.py)');
var maxBlock = 0;
for (var li = 0; li < Math.min(nl, CODES.length); li++) {
  var off = bin.readUInt32LE(8 + 8 * li), size = bin.readUInt32LE(12 + 8 * li);
  maxBlock = Math.max(maxBlock, size);
  for (var si = 0; si < Math.min(ns, WKEYS.length); si++) {
    var so = off + bin.readUInt16LE(off + 2 * si), end = bin.indexOf(0, so);
    var got = bin.toString('utf8', so, end), want = L[CODES[li]].watch[WKEYS[si]];
    if (got !== want) { fail('LANG.bin is out of date (' + CODES[li] + '.' + WKEYS[si] + '); run tools/gen_lang.py'); li = nl; break; }
  }
}
var ids = read('src/c/i18n_ids.h');
var hkeys = (ids.match(/^ {2}S_([A-Z0-9_]+),$/mg) || []).map(function (l) { return l.trim().slice(2, -1); });
if (hkeys.join() !== WKEYS.join()) fail('i18n_ids.h is out of date; run tools/gen_lang.py');
var bm = /I18N_BLOCK_MAX (\d+)/.exec(ids);
if (!bm || +bm[1] !== maxBlock) fail('I18N_BLOCK_MAX does not match LANG.bin');
CODES.forEach(function (c) {
  PKEYS.forEach(function (k) {
    if (I18N.PAGE[c][k] !== L[c].page[k]) fail('i18n_data.js is out of date (' + c + '.' + k + '); run tools/gen_lang.py');
  });
});

// ---------------------------------------------------------------- usage
var csrc = fs.readdirSync(path.join(ROOT, 'src', 'c')).filter(function (f) { return /\.c$/.test(f); })
  .map(function (f) { return read('src/c/' + f); }).join('\n');
WKEYS.forEach(function (k) {
  if (!new RegExp('\\bS_' + k + '\\b').test(csrc)) fail('watch string ' + k + ' is not used in src/c');
});
(csrc.match(/\bS_[A-Z0-9_]+\b/g) || []).forEach(function (id) {
  if (id !== 'S_N' && WKEYS.indexOf(id.slice(2)) < 0) fail(id + ' is used in src/c but not defined in lang/en.json');
});
var cfg = read('src/pkjs/config.js');
(cfg.match(/'@[a-z_]+'|data-t="[a-z_]+"|span\(T, '[a-z_]+'\)|T\.[a-z_]+/g) || []).forEach(function (m) {
  var k = /[a-z_]+(?=['"]?\)?$)/.exec(m.replace(/^T\./, ''))[0];
  if (PKEYS.indexOf(k) < 0) fail('config.js uses page string "' + k + '" which lang/en.json lacks');
});

// ---------------------------------------------------------------- settings page
var config = require(path.join(ROOT, 'src', 'pkjs', 'config.js'));
var defaults = config.defaults();
for (var lang = 0; lang <= CODES.length; lang++) {
  var html;
  try { html = decodeURIComponent(config.url(defaults, lang).replace(/^data:text\/html;charset=utf-8,/, '')); }
  catch (e) { fail('settings page (language ' + lang + '): ' + e); continue; }
  var code = config.langCode(lang, 'en-US'), T = I18N.PAGE[code];
  if (html.indexOf('>' + T.save + '<') < 0) fail('settings page (' + code + '): Save button not translated');
  if (code === 'ar' && html.indexOf('dir="rtl"') < 0) fail('settings page (ar): not right-to-left');
  var script = /<script>([\s\S]*)<\/script>/.exec(html);
  try { new Function(script ? script[1] : ''); } catch (e) { fail('settings page (' + code + '): script error ' + e); }
  var sel = new RegExp('<select id="lang">(?:(?!</select>).)*<option value="' + lang + '"[^>]* selected');
  if (!sel.test(html)) fail('settings page: language ' + lang + ' not selected');
}
if (config.langCode(0, 'fr-CA') !== 'fr' || config.langCode(0, 'in-ID') !== 'id' || config.langCode(0, 'ja') !== 'en')
  fail('langCode: automatic language from the locale is wrong');
var answer = { lang: 3, zoom: 2, font: 1, sysfont: 0, scroll: 0, header: 3, theme: 6, light: 2, trans: 5, render: 1,
               glance: 0, paper: 0xC3, ink: 0xFF };
var r = config.parse(encodeURIComponent(JSON.stringify(answer)), defaults, 0);
if (!r || r.lang !== 3 || r.bytes.length !== 13 || r.bytes[1] !== 2 || r.bytes[9] !== 5 || r.bytes[0] !== defaults[0])
  fail('config.parse: wrong result ' + JSON.stringify(r));
r = config.parse(encodeURIComponent(JSON.stringify({ lang: 99 })), defaults, 4);
if (!r || r.lang !== 4) fail('config.parse: an invalid language must keep the current one');

// ---------------------------------------------------------------- report
var count = WKEYS.length + ' watch + ' + PKEYS.length + ' page strings';
if (errors.length) {
  errors.forEach(function (e) { console.log('FAIL ' + e); });
  console.log(errors.length + ' problem(s); ' + CODES.length + ' languages, ' + count);
  process.exit(1);
}
console.log('OK: ' + CODES.length + ' languages (' + CODES.join(', ') + '), ' + count + ' each; Arabic lines <= ' +
            RTL_MAX_CP + ' code points / ' + RTL_MAX_BYTES + ' bytes');
