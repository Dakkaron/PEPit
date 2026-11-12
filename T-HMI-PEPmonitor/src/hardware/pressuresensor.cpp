#include "hardware/pressuresensor.h"
#include <Adafruit_HX711.h>
#include <algorithm>
#include "systemconfig.h"

static Adafruit_HX711 hx711 = Adafruit_HX711(HX7711_DATA_PIN, HX7711_CLOCK_PIN);

void initPressureSensor(String* errorMessage) {
  hx711.begin();
  Serial.print("Tareing air pressure sensor....");
  for (uint32_t i=0; i<=10000000; i++) {
    if (!hx711.isBusy()) {
      break;
    }
    if (i==10000000) {
      errorMessage->concat("Luftdruck-Sensor nicht gefunden!\n");
      return;
    }
  }
  for (uint8_t t=0; t<3; t++) {
    hx711.tareA(hx711.readChannelRaw(CHAN_A_GAIN_64));
    hx711.tareA(hx711.readChannelRaw(CHAN_A_GAIN_64));
  }
  Serial.print("done");

}

void powerDownPressureSensor(bool down) {
  hx711.powerDown(down);
}

namespace {

inline float tmax(float a, float b) { return a > b ? a : b; }
inline float tmin(float a, float b) { return a < b ? a : b; }

struct TareBucket {
  float mean, mn, mx, cross, trend;
  uint8_t n;
  uint32_t q;
  bool used;
};

TareBucket buckets[PRESSURE_TARE_BUCKETS];
uint32_t activeQ = 0xFFFFFFFFu;
float activeSamples[16];
uint8_t activeCount = 0;
float activeMin = 1e9f, activeMax = -1e9f, activeCross = 0;
float maWin[PRESSURE_MA_READINGS];
uint8_t maIndex = 0;
float maTotal = 0;
float tare = 0;
float allowPos = 0.5f, allowNeg = 0.5f;
float anchor = 0, proposed = 0;
uint32_t propStable = 0;
bool haveAcc = false;
float lastAcc = 0, suspVal = 0;
uint16_t suspCnt = 0;
uint16_t skips = 0;
float inhFS = 0, pepFS = 0;
float lastDeviation = 0;
bool started = false;

void resetTare() {
  for (int i=0;i<PRESSURE_TARE_BUCKETS;i++) {
    buckets[i].q = 0xFFFFFFFFu;
    buckets[i].used = false;
    buckets[i].n = 0;
  }
  activeQ = 0xFFFFFFFFu;
  activeCount = 0;
  activeMin = 1e9f; activeMax = -1e9f; activeCross = 0;
  for (int i=0;i<PRESSURE_MA_READINGS;i++) {
    maWin[i] = 0;
  }
  maIndex = 0; maTotal = 0;
  tare = 0;
  allowPos = 0.5f; allowNeg = 0.5f;
  anchor = 0; proposed = 0; propStable = 0;
  haveAcc = false; lastAcc = 0; suspVal = 0; suspCnt = 0;
  skips = 0;
  lastDeviation = 0;
}

void closeBucket(uint32_t q) {
  TareBucket& b = buckets[q % PRESSURE_TARE_BUCKETS];
  b.q = q;
  b.used = false;
  if (activeCount == 0) {
    b.n = 0;
    activeQ = q;
    return;
  }
  float sorted[16];
  for (int i=0;i<activeCount;i++) sorted[i] = activeSamples[i];
  std::sort(sorted, sorted+activeCount);
  int k = activeCount / 4; float sum = 0; int m = 0;
  for (int i=k;i<activeCount-k;i++) { sum += sorted[i]; m++; }
  b.mean = m ? sum/m : sorted[activeCount/2];
  b.trend = 0;
  { int h = activeCount / 2;
    if (h > 0) { float e = 0, l = 0;
      for (int i=0;i<h;i++) e += activeSamples[i];
      for (int i=h;i<activeCount;i++) l += activeSamples[i];
      b.trend = l/(activeCount-h) - e/h; } }
  b.mn = activeMin; b.mx = activeMax; b.n = activeCount; b.cross = activeCross;
  activeQ = q;
  activeCount = 0;
  activeMin = 1e9f; activeMax = -1e9f; activeCross = 0;
}

// atype: 0 none, 1 inhale-action, 2 pep-action
void classifyBucket(int i, float inhTh, float pepTh, int& atype, bool& trans, bool& live) {
  TareBucket& b = buckets[i];
  live = b.q != 0xFFFFFFFFu && b.n > 0;
  atype = 0; trans = false;
  if (!live) return;
  if (b.mx - b.mn > PRESSURE_TRANS_RANGE) { trans = true; return; }
  if (b.mean < tare - inhTh && inhFS > 0) atype = 1;
  else if (b.mean > tare + pepTh && pepFS > 0) atype = 2;
}

void tareUpdate(uint32_t nowQ) {
  float inhTh = tmax(0.4f, 0.25f*inhFS), pepTh = tmax(1.0f, 0.35f*pepFS);
  int cur = nowQ % PRESSURE_TARE_BUCKETS;
  int curType; bool curTrans, curLive;
  classifyBucket(cur, inhTh, pepTh, curType, curTrans, curLive);

  // 1) IDLE: run-scoped candidates ending at the just-closed bucket.
  static float cand[PRESSURE_TARE_BUCKETS];
  static uint32_t cq[PRESSURE_TARE_BUCKETS];
  static int idx[PRESSURE_TARE_BUCKETS];
  int ncand = 0;
  uint32_t sepQ = 0xFFFFFFFFu;
  if (curLive && !curTrans && curType == 0) {
    int runLen = 0;
    int i = cur;
    while (runLen < PRESSURE_TARE_BUCKETS) {
      int t; bool tr, lv;
      classifyBucket(i, inhTh, pepTh, t, tr, lv);
      if (!lv) break;
      if (tr || t != 0) { sepQ = buckets[i].q; break; }
      if (buckets[i].q == 0xFFFFFFFFu) break;
      idx[runLen++] = i;
      if (runLen >= PRESSURE_TARE_BUCKETS) break;
      i = (i + PRESSURE_TARE_BUCKETS - 1) % PRESSURE_TARE_BUCKETS;
      if (nowQ > buckets[i].q && nowQ - buckets[i].q >= PRESSURE_TARE_BUCKETS) break;
    }
    int cmax = runLen - 1;
    if (cmax > PRESSURE_CAND_MAX_AGE) cmax = PRESSURE_CAND_MAX_AGE;
    for (int r=0;r<cmax;r++) {
      int j = idx[r];
      if (buckets[j].n >= PRESSURE_MIN_BUCKET_SAMPLES && fabs(buckets[j].trend) <= PRESSURE_TREND_MAX) {
        cand[ncand] = buckets[j].mean;
        cq[ncand] = buckets[j].q;
        ncand++;
      }
    }
  }
  static float cl[PRESSURE_TARE_BUCKETS];
  static uint32_t clq[PRESSURE_TARE_BUCKETS];
  if (ncand >= 2) {
    float newest = cand[0];
    int cnt = 0;
    float lo = newest, hi = newest;
    for (int c=0;c<ncand;c++) {
      if (fabs(cand[c]-newest) <= PRESSURE_CLUSTER_SPAN) {
        cl[cnt] = cand[c]; clq[cnt] = cq[c]; cnt++;
        if (cand[c] < lo) lo = cand[c];
        if (cand[c] > hi) hi = cand[c];
      }
    }
    if (hi - lo > PRESSURE_CLUSTER_SPAN) {
      static int sel[PRESSURE_TARE_BUCKETS];
      int ns = 0;
      for (int c=0;c<ncand;c++) sel[ns++] = c;
      std::sort(sel, sel+ns, [&](int x, int y){ return cand[x] < cand[y]; });
      int bestA = -1, bestB = -1; int a = 0;
      for (int bnd=0;bnd<=ns;bnd++) {
        if (bnd == ns || cand[sel[bnd]] - cand[sel[a]] > PRESSURE_CLUSTER_SPAN) {
          bool hasNew = false;
          for (int z=a;z<bnd;z++) if (cand[sel[z]] == newest) hasNew = true;
          if (hasNew && bestB - bestA < bnd - a) { bestA = a; bestB = bnd; }
          a = bnd;
        }
      }
      if (bestB - bestA >= 2) {
        cnt = 0;
        for (int z=bestA;z<bestB;z++) { cl[cnt] = cand[sel[z]]; clq[cnt] = cq[sel[z]]; cnt++; }
      } else {
        cnt = 1;
      }
    }
    if (cnt >= 2) {
      std::sort(cl, cl+cnt);
      float med = cl[cnt/2];
      bool near = fabs(med - anchor) <= PRESSURE_ANCHOR_FOLLOW_MAX;
      if (near) {
        float da = med - anchor;
        if (da > PRESSURE_ANCHOR_RATE) da = PRESSURE_ANCHOR_RATE;
        if (da < -PRESSURE_ANCHOR_RATE) da = -PRESSURE_ANCHOR_RATE;
        anchor += da; proposed = med; propStable = 0;
      } else if (cnt >= 3 && fabs(med - proposed) <= 0.45f) {
        proposed = (proposed*propStable + med) / (propStable + 1);
        propStable++;
        if (propStable >= PRESSURE_ANCHOR_TAKE) { anchor = proposed; propStable = 0; }
        else if (propStable >= 8) {
          float da = proposed - anchor;
          if (da > 0.05f) da = 0.05f;
          if (da < -0.05f) da = -0.05f;
          anchor += da;
        }
      } else {
        proposed = med; propStable = 1;
      }
      bool confirmed = propStable >= PRESSURE_ANCHOR_CONFIRM && cnt >= 3;
      if (fabs(med - tare) <= PRESSURE_IDLE_TOLERANCE || confirmed) {
        float dpre = med - tare;
        if (fabs(dpre) >= 0.05f) {
          float d = dpre;
          if (d > PRESSURE_IDLE_STEP) d = PRESSURE_IDLE_STEP;
          if (d < -PRESSURE_IDLE_STEP) d = -PRESSURE_IDLE_STEP;
          bool rec = fabs(med - anchor) <= PRESSURE_ANCHOR_MATCH;
          uint32_t oldestQ = 0;
          for (int z=0;z<cnt;z++) if (clq[z] > oldestQ) oldestQ = clq[z];
          bool adj = sepQ != 0xFFFFFFFFu && oldestQ > sepQ && oldestQ - sepQ <= PRESSURE_SUSPECT_ADJ;
          bool susp = !near && !confirmed && adj;
          if (susp) {
            float gap = tare - anchor;
            if (d*gap > 0 && fabs(gap) >= PRESSURE_ANCHOR_LIMIT) d = 0;
            else { if (d > 0.05f) d = 0.05f; if (d < -0.05f) d = -0.05f; }
          } else {
            float ref = rec ? PRESSURE_REC_REFILL : PRESSURE_CHASE_REFILL;
            float cap = rec ? PRESSURE_REC_CAP : PRESSURE_CHASE_CAP;
            if (allowPos < cap) allowPos += ref; if (allowPos > cap) allowPos = cap;
            if (allowNeg < cap) allowNeg += ref; if (allowNeg > cap) allowNeg = cap;
            float gap = tare - anchor;
            if (!confirmed && d*gap > 0 && fabs(gap) >= PRESSURE_ANCHOR_LIMIT) d = 0;
            if (d > 0) { if (d > allowPos) d = allowPos; allowPos -= d; }
            else if (d < 0) { if (-d > allowNeg) d = -allowNeg; allowNeg += d; }
          }
          if (d != 0) tare += d;
        }
      }
    }
  }

  // 2) CROSSING: just-closed action bucket preceded (through transition buckets)
  // by an opposite-type action: pull tare toward the bracketing cross sample.
  if (curType != 0) {
    int i = (cur + PRESSURE_TARE_BUCKETS - 1) % PRESSURE_TARE_BUCKETS;
    int scanned = 0; int bestIdx = -1; float bestDist = 1e9f; int prevType = 0;
    while (scanned < PRESSURE_XSCAN_MAX) {
      if (nowQ >= buckets[i].q && nowQ - buckets[i].q < PRESSURE_TARE_BUCKETS && buckets[i].n > 0) {
        int t; bool tr, lv;
        classifyBucket(i, inhTh, pepTh, t, tr, lv);
        if (t != 0) { prevType = t; break; }
        if (tr) {
          if (!buckets[i].used) {
            float dist = (buckets[i].mn <= tare && tare <= buckets[i].mx) ? 0.0f
                       : tmin(fabs(buckets[i].mn - tare), fabs(buckets[i].mx - tare));
            if (dist < bestDist) { bestDist = dist; bestIdx = i; }
          }
          scanned++; i = (i + PRESSURE_TARE_BUCKETS - 1) % PRESSURE_TARE_BUCKETS; continue;
        }
        break;
      } else break;
    }
    if (prevType != 0 && prevType != curType && bestIdx >= 0) {
      float cv = buckets[bestIdx].cross;
      if (fabs(cv - tare) <= PRESSURE_CROSS_TOL) {
        float d = cv - tare;
        if (d > PRESSURE_CROSS_STEP) d = PRESSURE_CROSS_STEP;
        if (d < -PRESSURE_CROSS_STEP) d = -PRESSURE_CROSS_STEP;
        tare += d;
        buckets[bestIdx].used = true;
      }
    }
  }
}

}  // namespace

void tareReset() {
  started = false;
}

void tareSetTask(float inhFullScaleMbar, float pepFullScaleMbar) {
  inhFS = inhFullScaleMbar;
  pepFS = pepFullScaleMbar;
}

float pressureDeviationMbar() {
  return lastDeviation;
}

void readPressure(BlowData* blowData) {
  if (systemConfig.simulateBlows) {
    uint32_t blowDuration = blowData->ms - blowData->blowStartMs;
    uint8_t isBlowing = (blowDuration) < (blowData->targetDurationMs+100) ||
                        (blowDuration) > (blowData->targetDurationMs+100+SIMULATE_BLOWS_PAUSE_DURATION);
    blowData->pressure = isBlowing ? 98 : 1; 
  } else {
    if (blowData->ms + 4999 < millis()) {
      started = false;
    }
    if (hx711.isBusy()) {
      return;
    }
    float rawMbar = PRESSURE_SENSOR_SIGN * hx711.readChannel(CHAN_A_GAIN_64) / (PRESSURE_SENSOR_DIVISOR * 1000.0f);
    if (systemConfig.debugLogBlowPressure) {
      Serial.print(F("Channel A (Gain 64): "));
      Serial.print(rawMbar);
      Serial.print(F(" mbar / "));
    }
    uint32_t nowQ = blowData->ms / PRESSURE_TARE_BUCKET_MS;
    if (!started) {
      started = true;
      resetTare();
      activeQ = nowQ;
    }
    if (activeQ != nowQ && activeQ != 0xFFFFFFFFu) {
      closeBucket(nowQ);
      tareUpdate(nowQ);
    }
    if (activeQ == 0xFFFFFFFFu) activeQ = nowQ;

    if (rawMbar < -PRESSURE_MBAR_CUTOFF || rawMbar > PRESSURE_MBAR_CUTOFF) {
      skips++;
    } else {
      if (activeCount < 16) { activeSamples[activeCount] = rawMbar; activeCount++; }
      if (rawMbar < activeMin) activeMin = rawMbar;
      if (rawMbar > activeMax) activeMax = rawMbar;
      if (activeCount == 1 || fabs(rawMbar - tare) < fabs(activeCross - tare)) activeCross = rawMbar;
      float hv;
      if (!haveAcc) { hv = rawMbar; lastAcc = rawMbar; haveAcc = true; }
      else if (fabs(rawMbar - lastAcc) <= PRESSURE_SPIKE_JUMP) { hv = rawMbar; lastAcc = rawMbar; suspCnt = 0; }
      else {
        if (suspCnt > 0 && fabs(rawMbar - suspVal) <= PRESSURE_SPIKE_JUMP) suspCnt++;
        else { suspVal = rawMbar; suspCnt = 1; }
        if (suspCnt >= PRESSURE_SPIKE_CONFIRM) { hv = rawMbar; lastAcc = rawMbar; suspCnt = 0; }
        else hv = lastAcc;
      }
      maTotal -= maWin[maIndex];
      maWin[maIndex] = hv;
      maTotal += maWin[maIndex];
      maIndex = (maIndex + 1) % PRESSURE_MA_READINGS;
    }
    float smoothed = maTotal / PRESSURE_MA_READINGS;
    lastDeviation = smoothed - tare;
    float compMbar = blowData->negativePressure ? (tare - smoothed) : (smoothed - tare);
    if (compMbar < 0) compMbar = 0;
    blowData->pressure = compMbar * PRESSURE_MBAR_CALIBRATION / (float)blowData->targetPressure;
    if (systemConfig.debugLogBlowPressure) {
      Serial.print(F("TAREDBG smooth="));
      Serial.print(smoothed, 3);
      Serial.print(F(" tare="));
      Serial.print(tare, 3);
      Serial.print(F(" anc="));
      Serial.print(anchor, 3);
      Serial.print(F(" pct="));
      Serial.print(blowData->pressure, 2);
      Serial.print(F(" skips="));
      Serial.print(skips);
      Serial.print(F(" neg="));
      Serial.println(blowData->negativePressure ? 1 : 0);
    }
    if ((blowData->ms - blowData->blowStartMs) < blowData->targetDurationMs) {
      blowData->cumulativeError = abs(((int32_t)blowData->pressure - 100)) + ((blowData->cumulativeError * (PRESSURE_SENSOR_CUMULATIVE_ERROR_FACTOR-1)) / PRESSURE_SENSOR_CUMULATIVE_ERROR_FACTOR);
    }
  }
}
