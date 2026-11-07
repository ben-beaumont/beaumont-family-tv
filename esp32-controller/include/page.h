#pragma once

// Stand-in dashboard served by the ESP32 until the real family app exists.
// Dark and dim for OLED, and it drifts every minute so nothing burns in.
static const char PAGE_HTML[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Family Hub</title>
<style>
html,body{margin:0;height:100%;background:#000;color:#7d828c;
  font-family:Helvetica,Arial,sans-serif;overflow:hidden;cursor:none}
#w{position:absolute;left:50%;top:50%;text-align:center;
  transform:translate(-50%,-50%);transition:transform 3s ease}
#t{font-size:16vh;font-weight:200;color:#a9adb5;letter-spacing:.02em}
#d{font-size:4vh;margin-top:1vh}
#s{font-size:2.4vh;margin-top:6vh;color:#4a4f58}
</style></head>
<body><div id="w"><div id="t"></div><div id="d"></div>
<div id="s">Family dashboard coming soon</div></div>
<script>
var w=document.getElementById('w');
function pad(n){return (n<10?'0':'')+n;}
var days=['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];
var months=['January','February','March','April','May','June','July','August',
  'September','October','November','December'];
function tick(){
  var n=new Date();
  document.getElementById('t').textContent=pad(n.getHours())+':'+pad(n.getMinutes());
  document.getElementById('d').textContent=days[n.getDay()]+' '+n.getDate()+' '+months[n.getMonth()];
}
function drift(){
  var x=Math.random()*16-8, y=Math.random()*16-8;
  w.style.transform='translate(calc(-50% + '+x+'vw),calc(-50% + '+y+'vh))';
}
tick(); setInterval(tick,5000); setInterval(drift,60000);
</script></body></html>
)HTML";
