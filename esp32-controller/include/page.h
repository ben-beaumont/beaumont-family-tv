#pragma once

// Stand-in dashboard served by the ESP32 until the real family app exists.
// Dark and dim for OLED, and it drifts every minute so nothing burns in.
// Rotates between a calendar page (Today timeline + Upcoming list) and a
// locations page (everyone's zone/ETA, from Supabase via /locations.json).
static const char PAGE_HTML[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Family Hub</title>
<style>
html,body{margin:0;height:100%;background:#000;color:#7d828c;
  font-family:Helvetica,Arial,sans-serif;overflow:hidden;cursor:none}
#w{position:absolute;left:50%;top:50%;max-width:82vw;text-align:center;
  transform:translate(-50%,-50%);transition:transform 3s ease}
#dashboard{display:none;gap:4vw}
#locations{display:none;width:64vw;text-align:left;font-size:2.6vh;line-height:1.6}
#locations .heading{color:#a9adb5;font-size:4vh;font-weight:200;margin-bottom:2vh}
#locations .empty{color:#4a4f58}
#map{height:76vh;border-radius:4px;overflow:hidden;background:#111}
.leaflet-control-attribution{background:rgba(0,0,0,.55)!important;color:#5a5f68!important;
  font-size:1.3vh!important}
.leaflet-control-attribution a{color:#7d828c!important}
.fh-marker{display:flex;align-items:center;gap:.4em;white-space:nowrap;pointer-events:none}
.fh-marker .arrow{width:2vh;height:2vh;color:#e4e6ea;flex:0 0 auto;
  filter:drop-shadow(0 0 2px #000)}
.fh-marker .dot{width:1vh;height:1vh;border-radius:50%;background:#e4e6ea;
  flex:0 0 auto;box-shadow:0 0 0 2px rgba(0,0,0,.6)}
.fh-marker .label{background:rgba(0,0,0,.65);border-radius:3px;padding:.1em .5em}
.fh-marker .name{color:#e4e6ea;font-size:1.7vh}
.fh-marker .meta{color:#9ea3ad;font-size:1.3vh}
.fh-marker.stale{opacity:.5}
#today,#upcoming{text-align:left;font-size:2.6vh;line-height:1.6}
#today{width:38vw}
#upcoming{width:24vw}
#today .heading,#upcoming .heading{color:#a9adb5;font-size:4vh;font-weight:200;margin-bottom:2vh}
#today .day,#upcoming .day{color:#a9adb5;font-size:3vh;margin-top:2.4vh;font-weight:200}
#today .day:first-child,#upcoming .day:first-child{margin-top:0}
#today .ev,#upcoming .ev{display:flex;gap:1.2em;white-space:nowrap;overflow:hidden}
#today .ev .time,#upcoming .ev .time{color:#4a4f58;flex:0 0 auto;width:5.5em}
#today .ev .summary,#upcoming .ev .summary{overflow:hidden;text-overflow:ellipsis}
#today .empty,#upcoming .empty{color:#4a4f58}
#today .allday{margin-bottom:1.6vh}
#timeline{position:relative;height:68vh;margin-top:.5vh}
#timeline .hour{position:absolute;left:0;right:0;border-top:1px solid #23262c;height:0}
#timeline .hour span{position:absolute;top:-1em;left:0;font-size:1.7vh;color:#4a4f58;
  background:#000;padding-right:.6em}
#timeline .block{position:absolute;left:4em;right:.3em;box-sizing:border-box;
  display:flex;align-items:center;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;
  background:#1c1f24;border-left:3px solid #7d828c;border-radius:2px;
  padding:0 .7em;font-size:2.1vh;color:#c4c8d0}
#timeline .block .t{color:#7d828c;margin-right:.5em}
#timeline .now{position:absolute;left:0;right:0;border-top:1px solid #d9694a;height:0}
#timeline .now::before{content:'';position:absolute;left:3.7em;top:-.3em;
  width:.6em;height:.6em;border-radius:50%;background:#d9694a}
#timeline .now .label{position:absolute;top:-1em;left:0;font-size:1.7vh;color:#d9694a;
  background:#000;padding-right:.6em}
</style>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
</head>
<body>
<div id="w">
  <div id="dashboard">
    <div id="today"><div class="heading">Today</div><div class="body"></div></div>
    <div id="upcoming"><div class="heading">Upcoming</div><div class="body"></div></div>
  </div>
  <div id="locations"><div id="map"></div></div>
</div>
<script>
var w = document.getElementById('w');
var dashboardPage = document.getElementById('dashboard');
var todayView = document.getElementById('today');
var upcomingView = document.getElementById('upcoming');
var locationsPage = document.getElementById('locations');

function pad(n) { return (n < 10 ? '0' : '') + n; }
var DAYS = ['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];
var MONTHS = ['January','February','March','April','May','June','July','August',
  'September','October','November','December'];

// Any remote keypress dismisses the dashboard back to the TV's home screen,
// like a screensaver would. The physical power button never reaches a web
// page as a DOM event, so there's nothing to special-case for it.
var tvIndex = new URLSearchParams(location.search).get('tv');
if (tvIndex !== null) {
  document.addEventListener('keydown', function () {
    fetch('/home?tv=' + tvIndex).catch(function () {});
  });

  // Lets the ESP32 tell this page is genuinely still loaded even when the
  // TV's own "what app is in front" API doesn't notice it's gone (seen
  // after exiting the system browser via the remote's Back button).
  var sendHeartbeat = function () { fetch('/heartbeat?tv=' + tvIndex).catch(function () {}); };
  sendHeartbeat();
  setInterval(sendHeartbeat, 20000);
}

function drift() {
  var x = Math.random() * 16 - 8, y = Math.random() * 16 - 8;
  w.style.transform = 'translate(calc(-50% + ' + x + 'vw),calc(-50% + ' + y + 'vh))';
}
setInterval(drift, 60000);

// ---------------------------------------------------------------- calendar
//
// Minimal iCalendar (RFC 5545) reader: enough to show an upcoming-events
// agenda from a single Google Calendar feed. Known limitations: BYDAY is
// only honoured for weekly recurrence (no "2nd Tuesday" style rules), and
// timezone offsets other than UTC ("Z") are read as plain local wall-clock
// time rather than converted.

function unfoldIcs(text) {
  return text.replace(/\r\n/g, '\n').split('\n').reduce(function (lines, line) {
    if ((line[0] === ' ' || line[0] === '\t') && lines.length) {
      lines[lines.length - 1] += line.slice(1);
    } else {
      lines.push(line);
    }
    return lines;
  }, []);
}

function parseIcsDate(value, isUntil) {
  var m = /^(\d{4})(\d{2})(\d{2})(T(\d{2})(\d{2})(\d{2})(Z)?)?$/.exec(value);
  if (!m) return null;
  var y = +m[1], mo = +m[2], d = +m[3];
  if (!m[4]) return { date: new Date(y, mo - 1, d), allDay: true };
  var hh = +m[5], mm = +m[6], ss = +m[7];
  var date = m[8] ? new Date(Date.UTC(y, mo - 1, d, hh, mm, ss)) : new Date(y, mo - 1, d, hh, mm, ss);
  return { date: date, allDay: false };
}

function parseIcsLine(line) {
  var colon = line.indexOf(':');
  if (colon < 0) return null;
  var left = line.slice(0, colon), value = line.slice(colon + 1);
  var semi = left.indexOf(';');
  var key = semi < 0 ? left : left.slice(0, semi);
  var params = semi < 0 ? '' : left.slice(semi + 1);
  return { key: key, params: params, value: value };
}

function parseIcs(text) {
  var lines = unfoldIcs(text);
  var events = [];
  var cur = null;
  for (var i = 0; i < lines.length; i++) {
    var line = lines[i];
    if (line === 'BEGIN:VEVENT') { cur = {}; continue; }
    if (line === 'END:VEVENT') { if (cur) events.push(cur); cur = null; continue; }
    if (!cur) continue;
    var p = parseIcsLine(line);
    if (!p) continue;
    if (p.key === 'DTSTART' || p.key === 'DTEND') {
      var parsed = parseIcsDate(p.value);
      if (parsed) cur[p.key] = parsed;
    } else if (p.key === 'SUMMARY') {
      cur.summary = p.value.replace(/\\,/g, ',').replace(/\\n/g, ' ');
    } else if (p.key === 'RRULE' || p.key === 'UID' || p.key === 'STATUS') {
      cur[p.key] = p.value;
    } else if (p.key === 'RECURRENCE-ID') {
      var rid = parseIcsDate(p.value);
      if (rid) cur.recurrenceId = rid.date.getTime();
    }
  }
  return events;
}

function parseRrule(s) {
  var out = {};
  s.split(';').forEach(function (part) {
    var eq = part.indexOf('=');
    if (eq < 0) return;
    out[part.slice(0, eq)] = part.slice(eq + 1);
  });
  return out;
}

var WEEKDAY_CODES = ['SU','MO','TU','WE','TH','FR','SA'];

// Occurrence starts for one VEVENT within [windowStart, windowEnd]. An
// occurrence counts as "in" the window if it overlaps it at all - e.g. an
// event that started before windowStart but hasn't ended yet (a class
// that's on right now) still belongs on today's list.
function occurrencesInWindow(ev, windowStart, windowEnd) {
  if (!ev.DTSTART) return [];
  var start = ev.DTSTART.date;
  // An all-day event with no DTEND spans its whole start day (RFC 5545
  // default duration), not just the midnight instant DTSTART parses to.
  var duration = ev.DTEND ? (ev.DTEND.date - start) :
    (ev.DTSTART.allDay ? 86400000 : 0);
  function overlapsWindow(occStart) {
    var occEnd = new Date(occStart.getTime() + duration);
    return occStart <= windowEnd && occEnd >= windowStart;
  }
  if (!ev.RRULE) {
    return overlapsWindow(start) ? [{ start: start, end: new Date(start.getTime() + duration) }] : [];
  }
  var rule = parseRrule(ev.RRULE);
  var freq = rule.FREQ;
  var interval = parseInt(rule.INTERVAL || '1', 10) || 1;
  var count = rule.COUNT ? parseInt(rule.COUNT, 10) : null;
  var until = rule.UNTIL ? parseIcsDate(rule.UNTIL).date : null;
  var byday = (freq === 'WEEKLY' && rule.BYDAY) ? rule.BYDAY.split(',').map(function (d) { return d.slice(-2); }) : null;

  var result = [];
  var n = 0;
  var cur = new Date(start.getTime());
  var guard = 0;
  while (guard++ < 2000) {
    if (until && cur > until) break;
    if (count && n >= count) break;

    var isOccurrence = true;
    if (byday) {
      var weeksSinceStart = Math.floor((cur - start) / (7 * 86400000));
      isOccurrence = byday.indexOf(WEEKDAY_CODES[cur.getDay()]) !== -1 && (Math.floor(weeksSinceStart / 1) % interval === 0);
    }
    if (isOccurrence) {
      n++;
      if (overlapsWindow(cur)) {
        result.push({ start: new Date(cur.getTime()), end: new Date(cur.getTime() + duration) });
      }
      if (cur > windowEnd) break;
    }

    if (byday) {
      cur = new Date(cur.getTime() + 86400000);
    } else if (freq === 'DAILY') {
      cur = new Date(cur.getTime() + interval * 86400000);
    } else if (freq === 'WEEKLY') {
      cur = new Date(cur.getTime() + interval * 7 * 86400000);
    } else if (freq === 'MONTHLY') {
      cur = new Date(cur.getFullYear(), cur.getMonth() + interval, cur.getDate(), cur.getHours(), cur.getMinutes());
    } else if (freq === 'YEARLY') {
      cur = new Date(cur.getFullYear() + interval, cur.getMonth(), cur.getDate(), cur.getHours(), cur.getMinutes());
    } else {
      break;  // unknown FREQ
    }
    if (cur > windowEnd && (!byday)) break;
  }
  return result;
}

function buildAgenda(icsText, windowStart, windowEnd) {
  var events = parseIcs(icsText);

  // Occurrences explicitly moved/cancelled (RECURRENCE-ID overrides), so the
  // generated recurrence underneath doesn't also show up.
  var overridden = {};
  events.forEach(function (ev) {
    if (ev.UID && ev.recurrenceId) overridden[ev.UID + ':' + ev.recurrenceId] = true;
  });

  var occ = [];
  events.forEach(function (ev) {
    if (ev.STATUS === 'CANCELLED') return;
    occurrencesInWindow(ev, windowStart, windowEnd).forEach(function (o) {
      if (ev.UID && overridden[ev.UID + ':' + o.start.getTime()]) return;
      occ.push({ start: o.start, end: o.end, allDay: ev.DTSTART.allDay, summary: ev.summary || '(untitled)' });
    });
  });
  occ.sort(function (a, b) { return a.start - b.start; });
  return occ;
}

function dayDiff(date, today) {
  return Math.round((new Date(date.getFullYear(), date.getMonth(), date.getDate()) -
    new Date(today.getFullYear(), today.getMonth(), today.getDate())) / 86400000);
}

function dayLabel(date, today) {
  var diffDays = dayDiff(date, today);
  if (diffDays === 0) return 'Today';
  if (diffDays === 1) return 'Tomorrow';
  return DAYS[date.getDay()] + ' ' + date.getDate() + ' ' + MONTHS[date.getMonth()];
}

function appendEventRow(container, e) {
  var row = document.createElement('div');
  row.className = 'ev';
  var time = document.createElement('div');
  time.className = 'time';
  time.textContent = e.allDay ? 'All day' : pad(e.start.getHours()) + ':' + pad(e.start.getMinutes());
  var summary = document.createElement('div');
  summary.className = 'summary';
  summary.textContent = e.summary;
  row.appendChild(time);
  row.appendChild(summary);
  container.appendChild(row);
}

// Minutes since midnight, clamping anything outside today to today's edges
// (for events that started yesterday or run past midnight).
function minutesOfDay(date, startOfToday, startOfTomorrow) {
  if (date <= startOfToday) return 0;
  if (date >= startOfTomorrow) return 24 * 60;
  return date.getHours() * 60 + date.getMinutes();
}

function renderToday(occ) {
  var today = new Date();
  var startOfToday = new Date(today.getFullYear(), today.getMonth(), today.getDate());
  var startOfTomorrow = new Date(startOfToday.getTime() + 86400000);
  var body = todayView.querySelector('.body');
  body.innerHTML = '';
  // Overlap, not same start day - a multi-day event that started earlier
  // still belongs on today's list for as long as it's still running.
  var mine = occ.filter(function (e) { return e.start < startOfTomorrow && e.end > startOfToday; });
  if (!mine.length) {
    body.innerHTML = '<div class="empty">Nothing on today</div>';
    return;
  }

  var allDay = mine.filter(function (e) { return e.allDay; });
  var timed = mine.filter(function (e) { return !e.allDay; });

  if (allDay.length) {
    var adWrap = document.createElement('div');
    adWrap.className = 'allday';
    allDay.forEach(function (e) { appendEventRow(adWrap, e); });
    body.appendChild(adWrap);
  }
  if (!timed.length) return;

  // Hour range covers 06:00-23:00 by default, widened to fit any event
  // that falls outside it.
  var hourStart = 6, hourEnd = 23;
  timed.forEach(function (e) {
    hourStart = Math.min(hourStart, Math.floor(minutesOfDay(e.start, startOfToday, startOfTomorrow) / 60));
    hourEnd = Math.max(hourEnd, Math.ceil(minutesOfDay(e.end, startOfToday, startOfTomorrow) / 60));
  });
  hourEnd = Math.min(hourEnd, 24);
  var rangeStart = hourStart * 60, rangeLen = (hourEnd - hourStart) * 60;

  // #timeline's CSS height (68vh); block sizes are computed in vh rather
  // than % of that so a short event still gets a readable minimum height.
  var TIMELINE_VH = 68;
  var MIN_BLOCK_VH = 4;

  var timeline = document.createElement('div');
  timeline.id = 'timeline';
  for (var h = hourStart; h <= hourEnd; h++) {
    var row = document.createElement('div');
    row.className = 'hour';
    row.style.top = ((h - hourStart) * 60 / rangeLen * 100) + '%';
    var label = document.createElement('span');
    label.textContent = (h % 24) + ':00';
    row.appendChild(label);
    timeline.appendChild(row);
  }

  timed.forEach(function (e) {
    var startMin = Math.max(rangeStart, minutesOfDay(e.start, startOfToday, startOfTomorrow));
    var endMin = Math.min(rangeStart + rangeLen, minutesOfDay(e.end, startOfToday, startOfTomorrow));
    var top = (startMin - rangeStart) / rangeLen * TIMELINE_VH;
    var height = Math.max((endMin - startMin) / rangeLen * TIMELINE_VH, MIN_BLOCK_VH);
    var block = document.createElement('div');
    block.className = 'block';
    block.style.top = top + 'vh';
    block.style.height = height + 'vh';
    var t = document.createElement('span');
    t.className = 't';
    t.textContent = pad(e.start.getHours()) + ':' + pad(e.start.getMinutes()) + ' -';
    block.appendChild(t);
    block.appendChild(document.createTextNode(e.summary));
    timeline.appendChild(block);
  });

  if (today >= startOfToday && today < startOfTomorrow) {
    var nowMin = today.getHours() * 60 + today.getMinutes();
    if (nowMin >= rangeStart && nowMin <= rangeStart + rangeLen) {
      var nowLine = document.createElement('div');
      nowLine.className = 'now';
      nowLine.style.top = ((nowMin - rangeStart) / rangeLen * 100) + '%';
      var nowLabel = document.createElement('div');
      nowLabel.className = 'label';
      nowLabel.textContent = pad(today.getHours()) + ':' + pad(today.getMinutes());
      nowLine.appendChild(nowLabel);
      timeline.appendChild(nowLine);
    }
  }

  body.appendChild(timeline);
}

function renderUpcoming(occ) {
  var today = new Date();
  var body = upcomingView.querySelector('.body');
  body.innerHTML = '';
  // Only events that start on a later day - today's (possibly multi-day)
  // events are already covered by the Today view.
  var rest = occ.filter(function (e) { return dayDiff(e.start, today) >= 1; });
  if (!rest.length) {
    body.innerHTML = '<div class="empty">Nothing coming up this week</div>';
    return;
  }
  var lastDay = null;
  rest.slice(0, 12).forEach(function (e) {
    var label = dayLabel(e.start, today);
    if (label !== lastDay) {
      var h = document.createElement('div');
      h.className = 'day';
      h.textContent = label;
      body.appendChild(h);
      lastDay = label;
    }
    appendEventRow(body, e);
  });
}

var lastAgenda = [];
function renderAll() {
  renderToday(lastAgenda);
  renderUpcoming(lastAgenda);
}
function refreshCalendar() {
  fetch('/calendar.ics').then(function (r) {
    if (!r.ok) throw new Error('no calendar yet');
    return r.text();
  }).then(function (text) {
    var now = new Date();
    var startOfToday = new Date(now.getFullYear(), now.getMonth(), now.getDate());
    var weekAhead = new Date(startOfToday.getTime() + 7 * 86400000);
    lastAgenda = buildAgenda(text, startOfToday, weekAhead);
  }).catch(function () { lastAgenda = []; })
    .then(renderAll);
}
refreshCalendar();
setInterval(refreshCalendar, 5 * 60000);

// Re-render Today on its own, more frequent timer so the "now" line keeps
// moving without waiting on the next full calendar re-fetch.
setInterval(function () { renderToday(lastAgenda); }, 60000);

// --------------------------------------------------------------- locations
//
// Everyone's last-known zone/position from Supabase (via /locations.json -
// the ESP32 fetches it with a secret key that never reaches this page).
// Shown as a dot when stationary, or an arrow rotated to the compass
// heading when moving, so motion is visible at a glance.

var STALE_MS = 30 * 60000;  // older than this: show "last seen", not a live reading

function formatAgo(ms) {
  var mins = Math.round(ms / 60000);
  if (mins < 1) return 'just now';
  if (mins < 60) return mins + ' min ago';
  return Math.round(mins / 60) + 'h ago';
}

// Dark basemap, no API key needed - the real webOS app will use MapKit JS
// per the project plan; this is the stand-in page's pragmatic equivalent.
var familyMap = L.map('map', {
  zoomControl: false, attributionControl: true,
  dragging: false, scrollWheelZoom: false, doubleClickZoom: false,
  touchZoom: false, boxZoom: false, keyboard: false
}).setView([52.2053, 0.1218], 12);
L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/Canvas/World_Dark_Gray_Base/MapServer/tile/{z}/{y}/{x}', {
  attribution: 'Esri, HERE, Garmin, &copy; OpenStreetMap contributors',
  maxZoom: 16
}).addTo(familyMap);
var markerLayer = L.layerGroup().addTo(familyMap);

function markerHtml(p, stale, moving) {
  var icon = moving
    ? '<svg class="arrow" viewBox="0 0 24 24" style="transform:rotate(' + p.heading + 'deg)">' +
      '<path d="M12 2 L19 21 L12 17 L5 21 Z" fill="currentColor"/></svg>'
    : '<span class="dot"></span>';
  var metaText;
  if (stale) {
    metaText = 'Last seen ' + formatAgo(Date.now() - new Date(p.updated_at).getTime());
  } else {
    var bits = [p.zone || 'On the way'];
    if (moving) bits.push(Math.round(p.speed_mps * 2.23694) + ' mph');
    if (p.eta_home_minutes != null && p.eta_home_minutes > 0) bits.push(p.eta_home_minutes + ' min home');
    metaText = bits.join(' · ');
  }
  var div = document.createElement('div');
  div.className = 'fh-marker' + (stale ? ' stale' : '');
  div.innerHTML = icon +
    '<span class="label"><span class="name"></span><br><span class="meta"></span></span>';
  div.querySelector('.name').textContent = p.person;
  div.querySelector('.meta').textContent = metaText;
  return div;
}

function renderLocations(rows) {
  markerLayer.clearLayers();
  if (!rows || !rows.length) return;

  var bounds = [];
  rows.forEach(function (p) {
    if (p.lat == null || p.lng == null) return;
    var ageMs = Date.now() - new Date(p.updated_at).getTime();
    var stale = ageMs > STALE_MS;
    var moving = !stale && p.heading !== null && p.heading !== undefined &&
      p.speed_mps && p.speed_mps > 0.5;
    var icon = L.divIcon({
      className: 'fh-marker-wrap', html: markerHtml(p, stale, moving),
      iconSize: [220, 32], iconAnchor: [8, 16]
    });
    L.marker([p.lat, p.lng], { icon: icon }).addTo(markerLayer);
    bounds.push([p.lat, p.lng]);
  });
  if (bounds.length) familyMap.fitBounds(bounds, { padding: [40, 40], maxZoom: 14 });
}

var lastLocations = [];
function refreshLocations() {
  fetch('/locations.json').then(function (r) {
    if (!r.ok) throw new Error('no locations yet');
    return r.json();
  }).then(function (rows) {
    lastLocations = rows;
  }).catch(function () { lastLocations = []; })
    .then(function () { renderLocations(lastLocations); });
}
refreshLocations();
setInterval(refreshLocations, 20000);

// ---------------------------------------------------------- page rotation
//
// Alternates the calendar page and the locations page every 20s. Each
// page's own data refreshes on its own timer above regardless of which is
// currently visible.

var PAGES = [
  { name: 'calendar', el: dashboardPage, display: 'flex' },
  { name: 'map', el: locationsPage, display: 'block' }
];
var pageIndex = 0;

function showPage(i) {
  pageIndex = i;
  PAGES.forEach(function (p, idx) { p.el.style.display = idx === i ? p.display : 'none'; });
  // Leaflet measures its container on init; while #locations was hidden
  // that came out as 0x0, so force a re-measure now it's actually visible.
  if (PAGES[i].el === locationsPage) setTimeout(function () { familyMap.invalidateSize(); }, 0);
}

// ?page=calendar or ?page=map jumps straight there and stops rotating, so
// it can be checked from a laptop browser without waiting on the TV.
var forcedPageName = new URLSearchParams(location.search).get('page');
var forcedPageIndex = -1;
PAGES.forEach(function (p, idx) { if (p.name === forcedPageName) forcedPageIndex = idx; });

showPage(forcedPageIndex >= 0 ? forcedPageIndex : 0);
if (forcedPageIndex < 0) {
  setInterval(function () { showPage((pageIndex + 1) % PAGES.length); }, 20000);
}
</script>
</body></html>
)HTML";
