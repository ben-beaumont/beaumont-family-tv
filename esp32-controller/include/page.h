#pragma once

// Stand-in dashboard served by the ESP32 until the real family app exists.
// Dark and dim for OLED, and it drifts every minute so nothing burns in.
// Rotates between a clock view and an upcoming-events view built from the
// calendar feed at /calendar.ics (see main.cpp - fetchCalendar()).
static const char PAGE_HTML[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Family Hub</title>
<style>
html,body{margin:0;height:100%;background:#000;color:#7d828c;
  font-family:Helvetica,Arial,sans-serif;overflow:hidden;cursor:none}
#w{position:absolute;left:50%;top:50%;width:90vw;max-width:90vw;text-align:center;
  transform:translate(-50%,-50%);transition:transform 3s ease}
#clock{font-size:16vh;font-weight:200;color:#a9adb5;letter-spacing:.02em}
#date{font-size:4vh;margin-top:1vh}
#sub{font-size:2.4vh;margin-top:6vh;color:#4a4f58}
#today,#upcoming{display:none;text-align:left;font-size:2.6vh;line-height:1.6}
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
  background:#1c1f24;border-left:3px solid #7d828c;border-radius:2px;
  padding:.15em .7em;overflow:hidden;font-size:2.1vh;color:#c4c8d0}
#timeline .block .t{font-size:1.5vh;color:#7d828c}
#timeline .now{position:absolute;left:4em;right:0;border-top:2px solid #b5482f;height:0}
#timeline .now .label{position:absolute;top:-1em;left:-4em;width:3.6em;text-align:right;
  font-size:1.7vh;color:#b5482f}
</style></head>
<body>
<div id="w">
  <div id="clockView"><div id="clock"></div><div id="date"></div>
    <div id="sub">Family dashboard coming soon</div></div>
  <div id="today"><div class="heading">Today</div><div class="body"></div></div>
  <div id="upcoming"><div class="heading">Upcoming</div><div class="body"></div></div>
</div>
<script>
var w = document.getElementById('w');
var clockView = document.getElementById('clockView');
var todayView = document.getElementById('today');
var upcomingView = document.getElementById('upcoming');

function pad(n) { return (n < 10 ? '0' : '') + n; }
var DAYS = ['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];
var MONTHS = ['January','February','March','April','May','June','July','August',
  'September','October','November','December'];

function tickClock() {
  var n = new Date();
  document.getElementById('clock').textContent = pad(n.getHours()) + ':' + pad(n.getMinutes());
  document.getElementById('date').textContent = DAYS[n.getDay()] + ' ' + n.getDate() + ' ' + MONTHS[n.getMonth()];
}
function drift() {
  var x = Math.random() * 16 - 8, y = Math.random() * 16 - 8;
  w.style.transform = 'translate(calc(-50% + ' + x + 'vw),calc(-50% + ' + y + 'vh))';
}
tickClock();
setInterval(tickClock, 5000);
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
    var top = (startMin - rangeStart) / rangeLen * 100;
    var height = Math.max((endMin - startMin) / rangeLen * 100, 2.5);
    var block = document.createElement('div');
    block.className = 'block';
    block.style.top = top + '%';
    block.style.height = height + '%';
    var t = document.createElement('div');
    t.className = 't';
    t.textContent = pad(e.start.getHours()) + ':' + pad(e.start.getMinutes());
    var s = document.createElement('div');
    s.textContent = e.summary;
    block.appendChild(t);
    block.appendChild(s);
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

// Rotate clock -> today's events -> upcoming events, every 20s. A page
// opened with ?view=today or ?view=upcoming shows just that view instead,
// so it can be checked from a laptop browser without waiting on the TV.
var VIEW_ORDER = [clockView, todayView, upcomingView];
var NAMED_VIEWS = { clock: clockView, today: todayView, upcoming: upcomingView };
var forcedView = NAMED_VIEWS[new URLSearchParams(location.search).get('view')] || null;
var viewIndex = 0;

function showView(view) {
  if (view === todayView) renderToday(lastAgenda);
  if (view === upcomingView) renderUpcoming(lastAgenda);
  VIEW_ORDER.forEach(function (v) { v.style.display = v === view ? 'block' : 'none'; });
}
function rotateView() {
  viewIndex = (viewIndex + 1) % VIEW_ORDER.length;
  showView(VIEW_ORDER[viewIndex]);
}

var lastAgenda = [];
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
    .then(function () { showView(forcedView || VIEW_ORDER[viewIndex]); });
}
refreshCalendar();
setInterval(refreshCalendar, 5 * 60000);
if (!forcedView) setInterval(rotateView, 20000);

// Re-render Today on its own, more frequent timer so the "now" line keeps
// moving without waiting on the next full calendar re-fetch.
setInterval(function () {
  if ((forcedView || VIEW_ORDER[viewIndex]) === todayView) renderToday(lastAgenda);
}, 60000);
</script>
</body></html>
)HTML";
