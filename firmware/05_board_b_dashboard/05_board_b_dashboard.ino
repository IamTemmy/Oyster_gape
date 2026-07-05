/* =============================================================================
   05_board_b_dashboard.ino
   Oyster_gape project  |  MBRACE
   -----------------------------------------------------------------------------
   ⚠  SUPERSEDED — REFERENCE ONLY (12 mT stage, pre-tare).
      This sketch carries the OLD 12 mT calibration constants and has NO tare. The
      peg sensors are now burned at 25 mT, so flashing this would report WRONG
      millimetres (25 mT voltages read through 12 mT lines — no error, just
      silently wrong).
      >>> Flash firmware/06_board_b_tare for correct, tared gape. <<<
      Kept as the pre-tare dashboard reference from the 12 mT stage.
   -----------------------------------------------------------------------------
   PURPOSE
     Board B — full demo firmware. Reads the THREE linearized peg sensors and
     serves a live WEB DASHBOARD showing each as an oyster whose shell opens by
     the measured gape. Supersedes 04 for the demo; keeps 04's serial `read`
     for bench debugging. Conversion core + flag logic are identical to 04
     (per-sensor mV->mm line from data/analysis/deployment_calibration.md).

       gape_mm = (mV - b) / m
       flags: CLAMP-LO -> LO -> OK -> HI -> CLAMP-HI  (clamp tested first)

   NETWORK  (Phase 1: self-contained, no internet needed)
     Credentials + mode live in secrets.h (gitignored — copy secrets.example.h to
     secrets.h and fill it in). NET_MODE selects:
       NET_AP  (default) -> Board B makes its OWN network; join it, browse
                            http://192.168.4.1 . No router, nothing to embed.
       NET_STA           -> Board B joins an existing WiFi; browse to the IP
                            printed on the serial monitor at boot.
     AP is usually the more reliable demo path: many institutional networks
     isolate clients, which blocks your laptop from reaching Board B even when
     both are "connected." Fall back to AP if the STA page won't load.

   HARDWARE  (unchanged from 04 — Board B, no actuator)
     Three ÷1.5 dividers, all on ADC1 (ADC2 conflicts with the WiFi radio):
       S1 -> GPIO34   S2 -> GPIO35   S3 -> GPIO32
     analogReadMilliVolts, 12-bit, ADC_11db.  USB-powered for now (Phase 1).

   SERIAL (115200): prints the connection info at boot.  `read` = one-shot
     verbose read of all 3 sensors.  `help` = menu.
   =============================================================================
*/

#include <WiFi.h>
#include <WebServer.h>

// ---- network config: lives in secrets.h (gitignored) -----------------------
// Copy secrets.example.h -> secrets.h and fill in real values. secrets.h defines
// NET_AP / NET_STA / NET_MODE and the AP_/STA_ credentials, and is never committed.
#include "secrets.h"

WebServer server(80);

// ---- per-sensor calibration (from deployment_calibration.md) ----------------
struct Sensor {
  const char* name;
  uint8_t pin;
  float m;         // slope, mV/mm
  float b;         // intercept, mV (negative)
  float rampLo;    // valid ramp low, mm
  float rampHi;    // valid ramp high, mm
  float clampLo;   // low clamp / wire-break band, mV
  float clampHi;   // high clamp band, mV
};

Sensor SENSORS[] = {
  //  name  pin     m       b     rampLo rampHi clampLo clampHi
  {  "S1",  34,  199.8f, -1810.0f, 11.4f, 24.4f,  299.0f, 3150.0f },
  {  "S2",  35,  197.8f, -1799.0f, 11.5f, 24.6f,  317.0f, 3150.0f },
  {  "S3",  32,  200.6f, -1863.0f, 11.7f, 24.5f,  316.0f, 3150.0f },
};
const int N_SENSORS = sizeof(SENSORS) / sizeof(SENSORS[0]);

// ---- read routine (same method as the rig; light averaging for live speed) --
const int          AVG_SAMPLES     = 100;
const unsigned int SAMPLE_DELAY_US = 250;
const float        CLAMP_MARGIN_MV = 20.0;   // the 20 mV "still pinned" safety net

// =============================== the page ===================================
// Self-contained: no external fonts/CDNs (the AP has no internet). One file.
const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Oyster gape monitor</title>
<style>
  :root{
    --bg:#0c1a1d; --bg2:#0f2327; --panel:#12292e; --line:#1f3d43;
    --nacre:#eef3ec; --nacre2:#c7d4cd; --ink:#dce7e2; --muted:#7f9298;
    --brass:#c9a24b; --ok:#54c996; --warn:#e0a33a; --dead:#6d8087; --rust:#c07050;
  }
  *{box-sizing:border-box}
  html,body{margin:0}
  body{
    background:
      radial-gradient(120% 80% at 50% -10%, #123037 0%, var(--bg) 55%) fixed,
      var(--bg);
    color:var(--ink);
    font:400 16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
    -webkit-font-smoothing:antialiased; min-height:100vh;
    padding:28px 20px 48px;
  }
  .mono{font-family:ui-monospace,"SF Mono","Cascadia Code",Menlo,Consolas,monospace}
  header{max-width:1040px;margin:0 auto 26px;display:flex;
    align-items:flex-end;justify-content:space-between;gap:16px;flex-wrap:wrap}
  .eyebrow{font-size:11px;letter-spacing:.22em;text-transform:uppercase;
    color:var(--brass);font-weight:600;margin:0 0 6px}
  h1{margin:0;font-size:29px;font-weight:600;letter-spacing:-.01em}
  .sub{margin:4px 0 0;color:var(--muted);font-size:14px}
  .conn{display:flex;align-items:center;gap:9px;font-size:13px;color:var(--muted);
    padding:7px 13px;border:1px solid var(--line);border-radius:999px;background:var(--panel)}
  .dot{width:9px;height:9px;border-radius:50%;background:var(--dead);
    box-shadow:0 0 0 0 rgba(84,201,150,.5)}
  .conn.up .dot{background:var(--ok);animation:pulse 1.9s infinite}
  .conn.down .dot{background:var(--rust)}
  @keyframes pulse{0%{box-shadow:0 0 0 0 rgba(84,201,150,.45)}
    70%{box-shadow:0 0 0 7px rgba(84,201,150,0)}100%{box-shadow:0 0 0 0 rgba(84,201,150,0)}}

  .grid{max-width:1040px;margin:0 auto;display:grid;gap:18px;
    grid-template-columns:repeat(auto-fit,minmax(280px,1fr))}
  .card{background:linear-gradient(180deg,var(--panel),var(--bg2));
    border:1px solid var(--line);border-radius:18px;padding:20px 20px 18px;
    position:relative;overflow:hidden}
  .crow{display:flex;justify-content:space-between;align-items:baseline;
    font-size:12px;letter-spacing:.14em;text-transform:uppercase}
  .crow .who{color:var(--nacre);font-weight:600}
  .crow .ch{color:var(--muted);font-weight:500}

  .shell{display:block;width:100%;height:132px;margin:6px 0 2px}
  .valve{transition:transform .5s cubic-bezier(.22,.61,.36,1);
    transform-box:view-box;transform-origin:34px 75px}
  .valve path.body{fill:url(#nac);stroke:#0a1416;stroke-width:1}
  .valve path.ridge{fill:none;stroke:#0a1416;stroke-width:1;opacity:.18}
  .hinge{fill:var(--brass)}
  .aperture{fill:var(--ok);opacity:.16;transition:all .5s cubic-bezier(.22,.61,.36,1)}
  .card[data-state="LO"] .aperture,.card[data-state="HI"] .aperture{fill:var(--warn)}
  .card[data-state="CLAMP-HI"] .aperture{fill:var(--rust)}

  .read{display:flex;align-items:baseline;gap:7px;margin:8px 0 2px}
  .val{font-size:38px;font-weight:500;letter-spacing:-.02em;color:var(--nacre)}
  .unit{font-size:15px;color:var(--muted)}
  .card[data-state="LO"] .val,.card[data-state="HI"] .val{color:var(--warn)}
  .card[data-state="CLAMP-LO"] .val,.card[data-state="CLAMP-HI"] .val{color:var(--muted)}

  .pill{display:inline-flex;align-items:center;gap:7px;font-size:12px;font-weight:600;
    letter-spacing:.03em;padding:5px 11px;border-radius:999px;
    border:1px solid var(--line);color:var(--muted);background:#0e2024}
  .pill::before{content:"";width:7px;height:7px;border-radius:50%;background:currentColor}
  .card[data-state="OK"] .pill{color:var(--ok)}
  .card[data-state="LO"] .pill,.card[data-state="HI"] .pill{color:var(--warn)}
  .card[data-state="CLAMP-LO"] .pill{color:var(--dead)}
  .card[data-state="CLAMP-HI"] .pill{color:var(--rust)}

  .scale{margin:15px 0 4px;height:6px;border-radius:3px;position:relative;
    background:linear-gradient(90deg,#0a1618 0 8%,#20424a 8% 92%,#0a1618 92% 100%)}
  .mark{position:absolute;top:50%;width:12px;height:12px;border-radius:50%;
    background:var(--nacre);border:2px solid var(--bg2);
    transform:translate(-50%,-50%);left:0;transition:left .5s cubic-bezier(.22,.61,.36,1)}
  .card[data-state="OK"] .mark{background:var(--ok)}
  .card[data-state="LO"] .mark,.card[data-state="HI"] .mark{background:var(--warn)}
  .card[data-state="CLAMP-LO"] .mark{background:var(--dead)}
  .card[data-state="CLAMP-HI"] .mark{background:var(--rust)}
  .ends{display:flex;justify-content:space-between;color:var(--muted);
    font-size:11px;margin-top:5px}
  .foot{display:flex;justify-content:space-between;color:var(--muted);
    font-size:12px;margin-top:12px;padding-top:12px;border-top:1px solid var(--line)}

  footer{max-width:1040px;margin:26px auto 0;color:var(--muted);font-size:12px;
    display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap}
  @media (prefers-reduced-motion:reduce){
    .valve,.aperture,.mark{transition:none}.conn.up .dot{animation:none}}
</style></head>
<body>
  <header>
    <div>
      <p class="eyebrow">MBRACE &middot; bench simulation</p>
      <h1>Oyster gape monitor</h1>
      <p class="sub">Board B &middot; three sensors &middot; live shell aperture</p>
    </div>
    <div class="conn" id="conn"><span class="dot"></span><span id="conntxt">Connecting&hellip;</span></div>
  </header>
  <main class="grid" id="cards"></main>
  <footer>
    <span id="stamp">Waiting for first reading&hellip;</span>
    <span>Reading only trusted when a shell is <b style="color:var(--ok)">on ramp</b>.
      Clamped shells show &ldquo;&mdash;&rdquo; (no distance in the dead band).</span>
  </footer>

<script>
const A_MAX = 26;                 // shell half-opening angle at full gape (deg)
const LABEL = {                    // pill text per flag, in the user's words
  "OK":"On ramp","LO":"Below window","HI":"Above window",
  "CLAMP-LO":"Closed","CLAMP-HI":"Out of range"
};
let els = null;

function shellSVG(){
  return `<svg class="shell" viewBox="0 0 220 150" role="img" aria-label="oyster shell">
    <defs><linearGradient id="nac" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#f3f7f1"/><stop offset="1" stop-color="#b9c8c0"/>
    </linearGradient></defs>
    <ellipse class="aperture" cx="120" cy="75" rx="70" ry="1"/>
    <g class="valve top">
      <path class="body" d="M34,75 C70,38 150,40 192,72 C150,64 90,68 34,75 Z"/>
      <path class="ridge" d="M44,73 C82,50 142,52 184,71"/>
      <path class="ridge" d="M52,72 C88,58 140,59 176,71"/>
    </g>
    <g class="valve bot">
      <path class="body" d="M34,75 C70,112 150,110 192,78 C150,86 90,82 34,75 Z"/>
      <path class="ridge" d="M44,77 C82,100 142,98 184,79"/>
      <path class="ridge" d="M52,78 C88,92 140,91 176,79"/>
    </g>
    <circle class="hinge" cx="34" cy="75" r="3.4"/>
  </svg>`;
}

function build(list){
  const wrap = document.getElementById('cards');
  wrap.innerHTML = '';
  els = list.map((s,i)=>{
    const c = document.createElement('article');
    c.className='card'; c.dataset.state='OK';
    c.innerHTML =
      `<div class="crow"><span class="who">Oyster ${String(i+1).padStart(2,'0')}</span>`+
      `<span class="ch">${s.id} &rarr; GPIO${s.pin}</span></div>`+
      shellSVG()+
      `<div class="read"><span class="val mono">&mdash;</span><span class="unit">mm</span></div>`+
      `<span class="pill">&hellip;</span>`+
      `<div class="scale"><span class="mark"></span></div>`+
      `<div class="ends"><span>${s.lo.toFixed(1)}</span><span>window (mm)</span><span>${s.hi.toFixed(1)}</span></div>`+
      `<div class="foot"><span class="mono raw">&mdash; mV</span><span>${s.lo.toFixed(1)}&ndash;${s.hi.toFixed(1)} mm</span></div>`;
    wrap.appendChild(c);
    return {card:c, top:c.querySelector('.valve.top'), bot:c.querySelector('.valve.bot'),
      ap:c.querySelector('.aperture'), val:c.querySelector('.val'),
      pill:c.querySelector('.pill'), mark:c.querySelector('.mark'),
      raw:c.querySelector('.raw')};
  });
}

function update(list){
  list.forEach((s,i)=>{
    const e = els[i]; if(!e) return;
    let frac = (s.mm - s.lo)/(s.hi - s.lo);
    if(s.flag==="CLAMP-LO") frac = 0;
    if(s.flag==="CLAMP-HI") frac = 1;
    frac = Math.max(0, Math.min(1, frac));
    const ang = frac * A_MAX;
    e.top.style.transform = `rotate(${-ang}deg)`;
    e.bot.style.transform = `rotate(${ang}deg)`;
    e.ap.setAttribute('ry', (1 + frac*26).toFixed(1));
    e.card.dataset.state = s.flag;
    const clamped = (s.flag==="CLAMP-LO" || s.flag==="CLAMP-HI");
    e.val.textContent = clamped ? "\u2014" : s.mm.toFixed(2);
    e.pill.textContent = LABEL[s.flag] || s.flag;
    e.mark.style.left = (8 + frac*84) + "%";       // 8..92% matches the scale track
    e.raw.textContent = s.mv + " mV";
  });
}

function setConn(up){
  const c = document.getElementById('conn');
  c.className = 'conn ' + (up?'up':'down');
  document.getElementById('conntxt').textContent = up ? 'Live' : 'Reconnecting\u2026';
}

async function tick(){
  try{
    const r = await fetch('/data',{cache:'no-store'});
    const d = await r.json();
    if(!els || els.length!==d.s.length) build(d.s);
    update(d.s);
    setConn(true);
    document.getElementById('stamp').textContent =
      'Updated ' + new Date().toLocaleTimeString();
  }catch(e){ setConn(false); }
}
setInterval(tick, 300); tick();
</script>
</body></html>)HTML";

// =============================== firmware ===================================
void sampleStats(uint8_t pin, float &avg_mV, float &sd_mV) {
  double sum = 0, sumsq = 0;
  for (int i = 0; i < AVG_SAMPLES; i++) {
    uint32_t mv = analogReadMilliVolts(pin);
    sum += mv; sumsq += (double)mv * mv;
    delayMicroseconds(SAMPLE_DELAY_US);
  }
  double mean = sum / AVG_SAMPLES, var = sumsq / AVG_SAMPLES - mean * mean;
  if (var < 0) var = 0;
  avg_mV = mean; sd_mV = sqrt(var);
}

float mmFromMv(const Sensor &s, float mv) { return (mv - s.b) / s.m; }

const char* flagFor(const Sensor &s, float mv, float mm) {
  if (mv <= s.clampLo + CLAMP_MARGIN_MV) return "CLAMP-LO";
  if (mv >= s.clampHi - CLAMP_MARGIN_MV) return "CLAMP-HI";
  if (mm <  s.rampLo)                    return "LO";
  if (mm >  s.rampHi)                    return "HI";
  return "OK";
}

String buildJson() {
  String j = "{\"t\":" + String(millis()) + ",\"s\":[";
  for (int i = 0; i < N_SENSORS; i++) {
    float mv, sd; sampleStats(SENSORS[i].pin, mv, sd);
    float mm = mmFromMv(SENSORS[i], mv);
    const char* f = flagFor(SENSORS[i], mv, mm);
    if (i) j += ",";
    j += "{\"id\":\"" + String(SENSORS[i].name) + "\",\"pin\":" + String(SENSORS[i].pin)
       + ",\"mv\":" + String((long)lround(mv))
       + ",\"mm\":" + String(mm, 2)
       + ",\"flag\":\"" + String(f) + "\""
       + ",\"lo\":" + String(SENSORS[i].rampLo, 1)
       + ",\"hi\":" + String(SENSORS[i].rampHi, 1) + "}";
  }
  j += "]}";
  return j;
}

void handleRoot() { server.send_P(200, "text/html", PAGE_HTML); }
void handleData() { server.send(200, "application/json", buildJson()); }

void startWifi() {
#if NET_MODE == NET_AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.println(F("# WiFi: ACCESS POINT"));
  Serial.print(F("#   join network : ")); Serial.println(AP_SSID);
  Serial.print(F("#   password     : ")); Serial.println(AP_PASS);
  Serial.print(F("#   then open    : http://")); Serial.println(ip);
#else
  WiFi.mode(WIFI_STA);
  WiFi.begin(STA_SSID, STA_PASS);
  Serial.print(F("# WiFi: joining ")); Serial.print(STA_SSID);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) { delay(250); Serial.print('.'); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("#   open : http://")); Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("#   [!] not connected — check STA_SSID / STA_PASS"));
  }
#endif
}

void readAllVerbose() {
  Serial.println(F("# one-shot read"));
  for (int i = 0; i < N_SENSORS; i++) {
    float mv, sd; sampleStats(SENSORS[i].pin, mv, sd);
    float mm = mmFromMv(SENSORS[i], mv);
    const char* f = flagFor(SENSORS[i], mv, mm);
    Serial.print(F("  ")); Serial.print(SENSORS[i].name);
    Serial.print(F(" (GPIO")); Serial.print(SENSORS[i].pin); Serial.print(F(")  "));
    Serial.print((long)lround(mv)); Serial.print(F(" mV  ±"));
    Serial.print(sd, 1); Serial.print(F("   ->  "));
    Serial.print(mm, 2); Serial.print(F(" mm   ["));
    Serial.print(f); Serial.println(F("]"));
  }
}

void banner() {
  Serial.println();
  Serial.println(F("=== Oyster_gape : Board B — dashboard firmware ==="));
  Serial.println(F("  read   one-shot verbose read of all 3 sensors"));
  Serial.println(F("  help   this menu"));
  Serial.println(F("  (the live readout is the web dashboard — see the URL above)"));
  Serial.println(F("=================================================="));
}

void setup() {
  analogReadResolution(12);
  for (int i = 0; i < N_SENSORS; i++)
    analogSetPinAttenuation(SENSORS[i].pin, ADC_11db);
  Serial.begin(115200); Serial.setTimeout(50);
  delay(300);
  Serial.println(F("\n# Oyster_gape Board B starting..."));
  startWifi();
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.onNotFound(handleRoot);        // any path shows the dashboard
  server.begin();
  Serial.println(F("# web server up."));
  banner();
}

void loop() {
  server.handleClient();
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n'); line.trim();
    if      (line.equalsIgnoreCase("read")) readAllVerbose();
    else if (line.equalsIgnoreCase("help")) banner();
    else if (line.length())                { Serial.print(F("[?] ")); Serial.println(line); }
  }
}
