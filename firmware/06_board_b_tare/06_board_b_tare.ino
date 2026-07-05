/* =============================================================================
   06_board_b_tare.ino
   Oyster_gape project  |  MBRACE
   -----------------------------------------------------------------------------
   PURPOSE
     Board B — 25 mT + TARE. Same web dashboard as 05, with three changes:
       1. Calibration updated to the 25 mT per-sensor lines (data/sim_test_data).
       2. TARE: each sensor's CLOSED reading is captured as a baseline, and the
          dashboard reports TRUE GAPE (opening from closed), not absolute gap.
       3. Baseline is captured automatically ~1.5 s after boot (hold pegs CLOSED),
          and can be re-captured anytime with the `zero` serial command or the
          dashboard's Zero button.

       gape_mm = (mV - baseline_mV) / m

     Note the intercept b CANCELS in that subtraction, so gape is immune to the
     per-unit mounting offset (the rig-fixture + peg-mount standoff we chased
     down). Each unit zeroes to itself; all read 0.00 mm when closed.

   ACCURACY NOTES (documented, not hidden)
     - Auto-capture trusts whatever it sees at capture time. If a peg isn't truly
       closed then, its baseline is wrong until re-zeroed -> that's why `zero`
       stays available, and why auto-capture is gated to on-ramp readings only.
     - The tare removes OFFSET, not the hinge-arc SCALE error. Per the professor,
       that arc error is minimal for this geometry; we operate under that as a
       stated assumption (see deployment_calibration.md).

   NETWORK  — unchanged from 05; credentials/mode in secrets.h (gitignored).
   HARDWARE — unchanged; S1->GPIO34  S2->GPIO35  S3->GPIO32, all ADC1, ÷1.5.
   SERIAL (115200): `zero` re-capture baseline, `read` verbose, `help` menu.
   =============================================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include "secrets.h"          // NET_AP / NET_STA / NET_MODE + credentials

WebServer server(80);

// ---- per-sensor calibration (25 mT — from data/sim_test_data/*_25mT_*.csv) --
struct Sensor {
  const char* name;
  uint8_t pin;
  float m;         // slope, mV/mm
  float b;         // intercept, mV  (cancels for gape; kept for absolute reads)
  float rampLo;    // valid ramp low, mm
  float rampHi;    // valid ramp high, mm
  float clampLo;   // low clamp / wire-break floor, mV
  float clampHi;   // high clamp ceiling, mV
};

Sensor SENSORS[] = {
  //  name  pin      m        b     rampLo rampHi clampLo clampHi
  {  "S1",  34,  214.7f, -1368.4f,  8.6f,  20.5f,  312.0f, 3150.0f },
  {  "S2",  35,  217.7f, -1386.4f,  8.3f,  20.3f,  284.0f, 3150.0f },
  {  "S3",  32,  216.9f, -1355.5f,  8.4f,  20.3f,  296.0f, 3150.0f },
};
const int N_SENSORS = sizeof(SENSORS) / sizeof(SENSORS[0]);

// ---- read routine + tare tunables ------------------------------------------
const int          AVG_SAMPLES     = 100;
const unsigned int SAMPLE_DELAY_US = 250;
const float        CLAMP_MARGIN_MV = 20.0;   // "still pinned" safety net
const float        CLOSED_BAND_MM  = 0.05;   // within this of baseline = "closed"
const unsigned long SETTLE_MS      = 1500;   // wait before auto-capturing baseline
const float        GAPE_FULL_MM    = 12.0;   // visual full-scale for shell + bar

// ---- tare state ------------------------------------------------------------
float baseline_mV[N_SENSORS];
bool  baseline_ok[N_SENSORS] = { false, false, false };
bool  bootCaptured = false;
unsigned long captureAt = 0;

// =============================== the page ===================================
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
  .hgroup{display:flex;align-items:center;gap:10px}
  .conn{display:flex;align-items:center;gap:9px;font-size:13px;color:var(--muted);
    padding:7px 13px;border:1px solid var(--line);border-radius:999px;background:var(--panel)}
  .dot{width:9px;height:9px;border-radius:50%;background:var(--dead);
    box-shadow:0 0 0 0 rgba(84,201,150,.5)}
  .conn.up .dot{background:var(--ok);animation:pulse 1.9s infinite}
  .conn.down .dot{background:var(--rust)}
  @keyframes pulse{0%{box-shadow:0 0 0 0 rgba(84,201,150,.45)}
    70%{box-shadow:0 0 0 7px rgba(84,201,150,0)}100%{box-shadow:0 0 0 0 rgba(84,201,150,0)}}
  .zbtn{font:inherit;font-size:13px;font-weight:600;color:var(--brass);cursor:pointer;
    padding:7px 15px;border:1px solid var(--brass);border-radius:999px;background:transparent;
    letter-spacing:.02em;transition:background .15s}
  .zbtn:hover{background:rgba(201,162,75,.12)}
  .zbtn:active{transform:translateY(1px)}
  .zbtn.done{color:var(--ok);border-color:var(--ok)}

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
  .card[data-state="OVER"] .aperture{fill:var(--rust)}

  .read{display:flex;align-items:baseline;gap:7px;margin:8px 0 2px}
  .val{font-size:38px;font-weight:500;letter-spacing:-.02em;color:var(--nacre)}
  .unit{font-size:15px;color:var(--muted)}
  .card[data-state="OVER"] .val{color:var(--rust)}
  .card[data-state="NOZERO"] .val,.card[data-state="FAULT"] .val{color:var(--muted)}

  .pill{display:inline-flex;align-items:center;gap:7px;font-size:12px;font-weight:600;
    letter-spacing:.03em;padding:5px 11px;border-radius:999px;
    border:1px solid var(--line);color:var(--muted);background:#0e2024}
  .pill::before{content:"";width:7px;height:7px;border-radius:50%;background:currentColor}
  .card[data-state="OPEN"] .pill{color:var(--ok)}
  .card[data-state="CLOSED"] .pill{color:var(--muted)}
  .card[data-state="NOZERO"] .pill{color:var(--warn)}
  .card[data-state="OVER"] .pill{color:var(--rust)}
  .card[data-state="FAULT"] .pill{color:var(--rust)}

  .scale{margin:15px 0 4px;height:6px;border-radius:3px;position:relative;
    background:linear-gradient(90deg,#20424a 0 92%,#0a1618 92% 100%)}
  .mark{position:absolute;top:50%;width:12px;height:12px;border-radius:50%;
    background:var(--nacre);border:2px solid var(--bg2);
    transform:translate(-50%,-50%);left:0;transition:left .5s cubic-bezier(.22,.61,.36,1)}
  .card[data-state="OPEN"] .mark{background:var(--ok)}
  .card[data-state="OVER"] .mark{background:var(--rust)}
  .card[data-state="NOZERO"] .mark{background:var(--warn)}
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
      <p class="sub">Board B &middot; three sensors &middot; true gape from closed (25 mT)</p>
    </div>
    <div class="hgroup">
      <button class="zbtn" id="zero">Zero</button>
      <div class="conn" id="conn"><span class="dot"></span><span id="conntxt">Connecting&hellip;</span></div>
    </div>
  </header>
  <main class="grid" id="cards"></main>
  <footer>
    <span id="stamp">Waiting for first reading&hellip;</span>
    <span>Gape is measured from <b style="color:var(--ok)">closed</b> (auto-zeroed at
      power-on). Press <b>Zero</b> to re-baseline after re-seating a peg.</span>
  </footer>

<script>
const A_MAX = 26;                 // shell half-opening angle at full gape (deg)
const GAPE_FULL = 12;             // mm, visual full-scale
const LABEL = {
  CLOSED:"Closed", OPEN:"Open", OVER:"Over-range",
  NOZERO:"No baseline", FAULT:"Sensor fault"
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
    c.className='card'; c.dataset.state='CLOSED';
    c.innerHTML =
      `<div class="crow"><span class="who">Oyster ${String(i+1).padStart(2,'0')}</span>`+
      `<span class="ch">${s.id} &rarr; GPIO${s.pin}</span></div>`+
      shellSVG()+
      `<div class="read"><span class="val mono">&mdash;</span><span class="unit">mm gape</span></div>`+
      `<span class="pill">&hellip;</span>`+
      `<div class="scale"><span class="mark"></span></div>`+
      `<div class="ends"><span>0</span><span>gape (mm)</span><span>${GAPE_FULL}</span></div>`+
      `<div class="foot"><span class="mono raw">&mdash; mV</span><span class="base">zero &mdash;</span></div>`;
    wrap.appendChild(c);
    return {card:c, top:c.querySelector('.valve.top'), bot:c.querySelector('.valve.bot'),
      ap:c.querySelector('.aperture'), val:c.querySelector('.val'),
      pill:c.querySelector('.pill'), mark:c.querySelector('.mark'),
      raw:c.querySelector('.raw'), base:c.querySelector('.base')};
  });
}

function update(list){
  list.forEach((s,i)=>{
    const e = els[i]; if(!e) return;
    const st = s.state;
    let frac = s.gape / GAPE_FULL;
    if(st==="OVER") frac = 1;
    if(st==="CLOSED" || st==="NOZERO" || st==="FAULT") frac = 0;
    frac = Math.max(0, Math.min(1, frac));
    const ang = frac * A_MAX;
    e.top.style.transform = `rotate(${-ang}deg)`;
    e.bot.style.transform = `rotate(${ang}deg)`;
    e.ap.setAttribute('ry', (1 + frac*26).toFixed(1));
    e.card.dataset.state = st;
    let txt;
    if(st==="CLOSED") txt = "0.00";
    else if(st==="OPEN" || st==="OVER") txt = s.gape.toFixed(2);
    else txt = "\u2014";                       // NOZERO, FAULT
    e.val.textContent = txt;
    e.pill.textContent = LABEL[st] || st;
    e.mark.style.left = (frac*100) + "%";
    e.raw.textContent = s.mv + " mV";
    e.base.textContent = (s.base > 0) ? ("zero " + s.base + " mV") : "not zeroed";
  });
}

function setConn(up){
  const c = document.getElementById('conn');
  c.className = 'conn ' + (up?'up':'down');
  document.getElementById('conntxt').textContent = up ? 'Live' : 'Reconnecting\u2026';
}

document.getElementById('zero').onclick = async (ev)=>{
  const btn = ev.currentTarget;
  try{
    await fetch('/zero', {cache:'no-store'});
    btn.textContent = 'Zeroed'; btn.classList.add('done');
    setTimeout(()=>{ btn.textContent='Zero'; btn.classList.remove('done'); }, 1400);
    tick();
  }catch(e){}
};

async function tick(){
  try{
    const r = await fetch('/data',{cache:'no-store'});
    const d = await r.json();
    if(!els || els.length!==d.s.length) build(d.s);
    update(d.s);
    setConn(true);
    document.getElementById('stamp').textContent =
      (d.cap ? 'Updated ' : 'Capturing baseline\u2026 ') + new Date().toLocaleTimeString();
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
bool  onRamp(const Sensor &s, float mv) {
  return (mv > s.clampLo + CLAMP_MARGIN_MV) && (mv < s.clampHi - CLAMP_MARGIN_MV);
}

// Capture each sensor's CLOSED reading as its baseline — but only if it's on the
// ramp. An off-ramp reading (clamped/disconnected) is refused, not zeroed to junk.
void captureBaselines() {
  Serial.println(F("# capturing baseline (pegs must be CLOSED):"));
  for (int i = 0; i < N_SENSORS; i++) {
    float mv, sd; sampleStats(SENSORS[i].pin, mv, sd);
    if (onRamp(SENSORS[i], mv)) {
      baseline_mV[i] = mv; baseline_ok[i] = true;
      Serial.print(F("    ")); Serial.print(SENSORS[i].name);
      Serial.print(F(" zeroed @ ")); Serial.print((long)lround(mv)); Serial.println(F(" mV"));
    } else {
      baseline_ok[i] = false;
      Serial.print(F("    ")); Serial.print(SENSORS[i].name);
      Serial.print(F(" NOT zeroed — off ramp (")); Serial.print((long)lround(mv));
      Serial.println(F(" mV). Close the peg, then send 'zero'."));
    }
  }
}

String buildJson() {
  String j = "{\"t\":" + String(millis()) + ",\"cap\":" + String(bootCaptured ? 1 : 0) + ",\"s\":[";
  for (int i = 0; i < N_SENSORS; i++) {
    float mv, sd; sampleStats(SENSORS[i].pin, mv, sd);
    const char* st; float gape = 0;
    if (!baseline_ok[i]) {
      st = "NOZERO";
    } else {
      gape = (mv - baseline_mV[i]) / SENSORS[i].m;          // b cancels here
      if      (mv >= SENSORS[i].clampHi - CLAMP_MARGIN_MV) st = "OVER";
      else if (mv <= SENSORS[i].clampLo + CLAMP_MARGIN_MV) st = "FAULT";
      else if (gape <= CLOSED_BAND_MM)                     st = "CLOSED";
      else                                                 st = "OPEN";
    }
    long base = baseline_ok[i] ? (long)lround(baseline_mV[i]) : -1;
    if (i) j += ",";
    j += "{\"id\":\"" + String(SENSORS[i].name) + "\",\"pin\":" + String(SENSORS[i].pin)
       + ",\"mv\":" + String((long)lround(mv))
       + ",\"gape\":" + String(gape < 0 ? 0.0f : gape, 2)
       + ",\"base\":" + String(base)
       + ",\"state\":\"" + String(st) + "\"}";
  }
  j += "]}";
  return j;
}

void handleRoot() { server.send_P(200, "text/html", PAGE_HTML); }
void handleData() { server.send(200, "application/json", buildJson()); }
void handleZero() { Serial.println(F("# zero (dashboard)")); captureBaselines();
                    server.send(200, "application/json", "{\"ok\":true}"); }

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
  Serial.println(F("# one-shot read (abs mm | gape mm)"));
  for (int i = 0; i < N_SENSORS; i++) {
    float mv, sd; sampleStats(SENSORS[i].pin, mv, sd);
    float mm = mmFromMv(SENSORS[i], mv);
    Serial.print(F("  ")); Serial.print(SENSORS[i].name);
    Serial.print(F(" (GPIO")); Serial.print(SENSORS[i].pin); Serial.print(F(")  "));
    Serial.print((long)lround(mv)); Serial.print(F(" mV  ->  abs "));
    Serial.print(mm, 2); Serial.print(F(" mm"));
    if (baseline_ok[i]) {
      float gape = (mv - baseline_mV[i]) / SENSORS[i].m;
      Serial.print(F("  |  gape ")); Serial.print(gape < 0 ? 0.0f : gape, 2);
      Serial.print(F(" mm  (zero ")); Serial.print((long)lround(baseline_mV[i])); Serial.print(F(" mV)"));
    } else {
      Serial.print(F("  |  gape -- (not zeroed)"));
    }
    Serial.println();
  }
}

void banner() {
  Serial.println();
  Serial.println(F("=== Oyster_gape : Board B — 25 mT + tare ==="));
  Serial.println(F("  zero   re-capture the closed baseline (hold pegs closed first)"));
  Serial.println(F("  read   one-shot verbose read (abs mm + gape)"));
  Serial.println(F("  help   this menu"));
  Serial.println(F("  (live readout is the web dashboard — see the URL above)"));
  Serial.println(F("============================================"));
}

void setup() {
  analogReadResolution(12);
  for (int i = 0; i < N_SENSORS; i++)
    analogSetPinAttenuation(SENSORS[i].pin, ADC_11db);
  Serial.begin(115200); Serial.setTimeout(50);
  delay(300);
  Serial.println(F("\n# Oyster_gape Board B (25 mT + tare) starting..."));
  startWifi();
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/zero", handleZero);
  server.onNotFound(handleRoot);
  server.begin();
  Serial.println(F("# web server up."));
  banner();
  captureAt = millis() + SETTLE_MS;    // auto-capture baseline after settle
  Serial.println(F("# auto-zero armed — keep the pegs CLOSED for ~2 s..."));
}

void loop() {
  server.handleClient();

  if (!bootCaptured && millis() >= captureAt) {   // one-shot auto baseline
    captureBaselines();
    bootCaptured = true;
  }

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n'); line.trim();
    if      (line.equalsIgnoreCase("zero")) captureBaselines();
    else if (line.equalsIgnoreCase("read")) readAllVerbose();
    else if (line.equalsIgnoreCase("help")) banner();
    else if (line.length())                { Serial.print(F("[?] ")); Serial.println(line); }
  }
}
