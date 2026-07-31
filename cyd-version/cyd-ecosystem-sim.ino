#include <TFT_eSPI.h>
#include <math.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

// ============================================================================
//  CYD Ecosystem Simulation - User Configuration
// ============================================================================

// Hardware GPIO Pin Definitions for CYD (ESP32-2432S028R)
#define CYD_BACKLIGHT_PIN 21

// Touch Screen Hardware Pins (HSPI)
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// Touch Screen Calibration (Adjust if touch coordinates are offset on your CYD)
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3700
#define TOUCH_MIN_Y 300
#define TOUCH_MAX_Y 3800

// Display Canvas Resolution & Offset
#define TFT_WIDTH  320
#define TFT_HEIGHT 170
#define OFFSET_Y   ((240 - TFT_HEIGHT) / 2) // Centered on 320x240 LCD

// Display Color Tuning (Tuned for ILI9341 TN Panel)
const bool SWAP_RB = false; // Set to true if red & blue are inverted on your display 

SPIClass touchSpi = SPIClass(HSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite img = TFT_eSprite(&tft);
TFT_eSprite topHud = TFT_eSprite(&tft);
TFT_eSprite botHud = TFT_eSprite(&tft);

// Clock state variables
int clockHour = 17;
int clockMin  = 54;
int clockSec  = 0;
unsigned long lastClockTick = 0; 

static uint8_t cydLut[256];
static bool cydLutInited = false;

void initCydColorLUT() {
  for (int i = 0; i < 256; i++) {
    float norm = i / 255.0f;
    // S-curve contrast and saturation boost tuned for CYD ILI9341 panel
    float enhanced = norm * norm * (3.0f - 2.0f * norm);
    int val = (int)(enhanced * 265.0f);
    if (val > 255) val = 255;
    if (val < 0) val = 0;
    cydLut[i] = (uint8_t)val;
  }
  cydLutInited = true;
}

uint16_t myColor(uint8_t r, uint8_t g, uint8_t b) {
  if (!cydLutInited) initCydColorLUT();
  uint8_t er = cydLut[r];
  uint8_t eg = cydLut[g];
  uint8_t eb = cydLut[b];
  if (SWAP_RB) return tft.color565(eb, eg, er);
  return tft.color565(er, eg, eb);
}

uint16_t fadeColor(uint8_t r, uint8_t g, uint8_t b, float factor) {
  if (!cydLutInited) initCydColorLUT();
  uint8_t fr = cydLut[(uint8_t)constrain(r * factor + 0.5f, 0.0f, 255.0f)];
  uint8_t fg = cydLut[(uint8_t)constrain(g * factor + 0.5f, 0.0f, 255.0f)];
  uint8_t fb = cydLut[(uint8_t)constrain(b * factor + 0.5f, 0.0f, 255.0f)];
  if (SWAP_RB) return tft.color565(fb, fg, fr);
  return tft.color565(fr, fg, fb);
}

const int MAX_PLANTS = 50; 
const int MAX_HERBS = 40;
const int MAX_CARNS = 8;
const int MAX_APEX = 3;   
const int MAX_DECOMPS = 10;
const int MAX_SPORES = 15;
const int MAX_PLANKTON = 100;
const int MAX_PARTICLES = 150;
const int MAX_GARBAGES = 30;
const int HISTORY_LEN = 30; 

struct Entity {
  bool active;
  float x, y;
  float vx, vy;
  float energy;
  float histX[HISTORY_LEN];
  float histY[HISTORY_LEN];
  int histIdx;
  float flash; 
  bool infected;
  int targetId;
  float speedLimit;
  int age;
  float altruism;
  float immunity;
};

struct Spore { bool active; float x, y, vx, vy; };
struct Plankton { float x, y, vx; int layer; };
struct Particle {
  bool active;
  float x, y, vx, vy, life;
  uint8_t r, g, b;
};
struct Garbage {
  bool active;
  float x, y;
  uint8_t r, g, b;
};

bool isFingerTouching = false;
float fingerX = 0.0f;
float fingerY = 0.0f;

SemaphoreHandle_t dataMutex;

struct Plant { bool active; float x, y; };
Plant plants[MAX_PLANTS];
Entity herbs[MAX_HERBS];
Entity carns[MAX_CARNS];
Entity apex[MAX_APEX];
Entity decomps[MAX_DECOMPS];
Spore spores[MAX_SPORES];
Plankton planktons[MAX_PLANKTON];
Particle particles[MAX_PARTICLES];
Garbage garbages[MAX_GARBAGES];
int garbageIdx = 0;

TaskHandle_t Task1;

void initHistory(Entity &e, float x, float y);
void updateHistory(Entity &e);
void spawnExplosion(float x, float y, uint8_t r, uint8_t g, uint8_t b, int count, float speedBase);
void spawnGarbage(float x, float y, uint8_t r, uint8_t g, uint8_t b);

void initHistory(Entity &e, float x, float y) {
  for(int i=0; i<HISTORY_LEN; i++) { e.histX[i] = -100.0f; e.histY[i] = -100.0f; }
  e.histIdx = 0; e.flash = 0; e.infected = false; e.targetId = -1; e.age = 0;
}

void updateHistory(Entity &e) {
  e.histX[e.histIdx] = e.x; e.histY[e.histIdx] = e.y;
  e.histIdx = (e.histIdx + 1) % HISTORY_LEN;
}

void spawnExplosion(float x, float y, uint8_t r, uint8_t g, uint8_t b, int count, float speedBase) {
  for(int p=0; p<MAX_PARTICLES && count > 0; p++) {
    if(!particles[p].active) {
      particles[p].active = true;
      particles[p].x = x; particles[p].y = y;
      float angle = random(0, 360) * PI * (1.0f / 180.0f);
      float speed = random(5, 25) * (1.0f / 10.0f) * speedBase;
      particles[p].vx = cos(angle) * speed;
      particles[p].vy = sin(angle) * speed;
      particles[p].life = 1.0f;
      particles[p].r = r; particles[p].g = g; particles[p].b = b;
      count--;
    }
  }
}

void spawnGarbage(float x, float y, uint8_t r, uint8_t g, uint8_t b) {
  garbages[garbageIdx].active = true;
  garbages[garbageIdx].x = x + random(-3, 4);
  garbages[garbageIdx].y = y + random(-3, 4);
  garbages[garbageIdx].r = r; garbages[garbageIdx].g = g; garbages[garbageIdx].b = b;
  garbageIdx = (garbageIdx + 1) % MAX_GARBAGES;
}

void spawnPlant() {
  for(int i=0; i<MAX_PLANTS; i++) {
    if(!plants[i].active) {
      plants[i].active = true;
      plants[i].x = random(5, TFT_WIDTH - 5); plants[i].y = random(5, TFT_HEIGHT - 5);
      break;
    }
  }
}

void spawnHerb(float x, float y, float pSpeed = 0.8f, float pAltruism = -1.0f, float pImmunity = -1.0f) {
  for(int i=0; i<MAX_HERBS; i++) {
    if(!herbs[i].active) {
      herbs[i].active = true;
      herbs[i].x = (x == -1) ? random(10, TFT_WIDTH-10) : x + random(-5, 5);
      herbs[i].y = (y == -1) ? random(10, TFT_HEIGHT-10) : y + random(-5, 5);
      herbs[i].vx = (random(0, 100) * (1.0f / 50.0f)) - 1.0f; herbs[i].vy = (random(0, 100) * (1.0f / 50.0f)) - 1.0f;
      herbs[i].energy = 80;
      float newSpeed = pSpeed + (random(0, 200) * (1.0f / 1000.0f)) - 0.1f;
      if(newSpeed < 0.3f) newSpeed = 0.3f; if(newSpeed > 2.0f) newSpeed = 2.0f;
      herbs[i].speedLimit = newSpeed;
      
      if (pAltruism == -1.0f) {
        herbs[i].altruism = random(0, 100) * 0.01f;
      } else {
        herbs[i].altruism = pAltruism + (random(-10, 11) * 0.01f);
        if(herbs[i].altruism < 0.0f) herbs[i].altruism = 0.0f;
        if(herbs[i].altruism > 1.0f) herbs[i].altruism = 1.0f;
      }

      if (pImmunity == -1.0f) {
        herbs[i].immunity = random(0, 100) * 0.01f;
      } else {
        herbs[i].immunity = pImmunity + (random(-10, 11) * 0.01f);
        if(herbs[i].immunity < 0.0f) herbs[i].immunity = 0.0f;
        if(herbs[i].immunity > 1.0f) herbs[i].immunity = 1.0f;
      }
      
      initHistory(herbs[i], herbs[i].x, herbs[i].y);
      break;
    }
  }
}

void spawnCarn(float x, float y, float pSpeed = 1.1f) {
  for(int i=0; i<MAX_CARNS; i++) {
    if(!carns[i].active) {
      carns[i].active = true;
      carns[i].x = (x == -1) ? random(10, TFT_WIDTH-10) : x + random(-5, 5);
      carns[i].y = (y == -1) ? random(10, TFT_HEIGHT-10) : y + random(-5, 5);
      carns[i].vx = (random(0, 100) * (1.0f / 50.0f)) - 1.0f; carns[i].vy = (random(0, 100) * (1.0f / 50.0f)) - 1.0f;
      carns[i].energy = 100;
      float newSpeed = pSpeed + (random(0, 200) * (1.0f / 1000.0f)) - 0.1f;
      if(newSpeed < 0.5f) newSpeed = 0.5f; if(newSpeed > 2.5f) newSpeed = 2.5f;
      carns[i].speedLimit = newSpeed;
      initHistory(carns[i], carns[i].x, carns[i].y);
      break;
    }
  }
}

void spawnApex(float x, float y, float pSpeed = 1.5f) {
  for(int i=0; i<MAX_APEX; i++) {
    if(!apex[i].active) {
      apex[i].active = true;
      apex[i].x = (x == -1) ? random(10, TFT_WIDTH-10) : x + random(-5, 5);
      apex[i].y = (y == -1) ? random(10, TFT_HEIGHT-10) : y + random(-5, 5);
      apex[i].vx = (random(0, 100) * (1.0f / 50.0f)) - 1.0f; apex[i].vy = (random(0, 100) * (1.0f / 50.0f)) - 1.0f;
      apex[i].energy = 300;
      float newSpeed = pSpeed + (random(0, 200) * (1.0f / 1000.0f)) - 0.1f;
      if(newSpeed < 0.8f) newSpeed = 0.8f; if(newSpeed > 3.0f) newSpeed = 3.0f;
      apex[i].speedLimit = newSpeed;
      initHistory(apex[i], apex[i].x, apex[i].y);
      break;
    }
  }
}

void spawnSpore(float x, float y) {
  for(int i=0; i<MAX_SPORES; i++) {
    if(!spores[i].active) {
      spores[i].active = true;
      spores[i].x = x; spores[i].y = y;
      spores[i].vx = (random(0, 100) * (1.0f / 100.0f)) - 0.5f + 0.4f; 
      spores[i].vy = (random(0, 100) * (1.0f / 100.0f)) - 0.5f;
      break;
    }
  }
}

void spawnDecomp(float x, float y) {
  for(int i=0; i<MAX_DECOMPS; i++) {
    if(!decomps[i].active) {
      decomps[i].active = true;
      decomps[i].x = (x == -1) ? random(10, TFT_WIDTH-10) : x + random(-5, 5);
      decomps[i].y = (y == -1) ? random(10, TFT_HEIGHT-10) : y + random(-5, 5);
      decomps[i].vx = (random(0, 100) * (1.0f / 50.0f)) - 1.0f; decomps[i].vy = (random(0, 100) * (1.0f / 50.0f)) - 1.0f;
      decomps[i].energy = 80;
      decomps[i].speedLimit = 0.7f;
      initHistory(decomps[i], decomps[i].x, decomps[i].y);
      break;
    }
  }
}

float Q_rsqrt( float number ) {
  long i;
  float x2, y;
  const float threehalfs = 1.5F;
  x2 = number * 0.5F;
  y  = number;
  memcpy(&i, &y, sizeof(i));
  i  = 0x5f3759df - ( i >> 1 );
  memcpy(&y, &i, sizeof(y));
  y  = y * ( threehalfs - ( x2 * y * y ) );
  return y;
}

void core0Task(void * pvParameters) {
  for(;;) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    
    int herbCount = 0; for(int i=0; i<MAX_HERBS; i++) if(herbs[i].active) herbCount++;
    int carnCount = 0; for(int i=0; i<MAX_CARNS; i++) if(carns[i].active) carnCount++;
    int apexCount = 0; for(int i=0; i<MAX_APEX; i++) if(apex[i].active) apexCount++;
    int decompCount = 0; for(int i=0; i<MAX_DECOMPS; i++) if(decomps[i].active) decompCount++;

    if (random(0, 1000) < 35) spawnPlant();
    if (random(0, 10000) < 10) spawnSpore(random(10, TFT_WIDTH-10), random(10, TFT_HEIGHT-10));

    if(decompCount < 4) spawnDecomp(-1, -1);
    if(herbCount < 5 && random(0, 1000) < 10) spawnHerb(-1, -1);
    if(carnCount < 2 && herbCount > 20 && random(0, 1000) < 10) spawnCarn(-1, -1);
    if(apexCount < 1 && carnCount > 6 && random(0, 1000) < 10) spawnApex(-1, -1);
    if (isFingerTouching) {
      auto applyDoctorFish = [&](Entity &e) {
        if (!e.active) return;
        float dx = fingerX - e.x;
        float dy = fingerY - e.y;
        float distSq = dx * dx + dy * dy;
        if (distSq > 1.0f) {
          float invDist = Q_rsqrt(distSq);
          float nx = dx * invDist;
          float ny = dy * invDist;
          e.vx = e.vx * 0.80f + nx * 0.55f;
          e.vy = e.vy * 0.80f + ny * 0.55f;
        }
      };

      for (int i = 0; i < MAX_HERBS; i++) applyDoctorFish(herbs[i]);
      for (int i = 0; i < MAX_CARNS; i++) applyDoctorFish(carns[i]);
      for (int i = 0; i < MAX_APEX; i++) applyDoctorFish(apex[i]);
      for (int i = 0; i < MAX_DECOMPS; i++) applyDoctorFish(decomps[i]);
    }

    for(int p=0; p<MAX_PARTICLES; p++) {
      if(particles[p].active) {
        particles[p].x += particles[p].vx;
        particles[p].y += particles[p].vy;
        particles[p].vx *= 0.9f;
        particles[p].vy *= 0.9f;
        particles[p].life -= 0.05f;
        if(particles[p].life <= 0) particles[p].active = false;
      }
    }

    for(int i=0; i<MAX_DECOMPS; i++) {
      if(!decomps[i].active) continue;
      updateHistory(decomps[i]);
      decomps[i].age++;
      
      float minDist = 99999;
      int targetG = -1;
      int targetS = -1;
      
      for(int g=0; g<MAX_GARBAGES; g++) {
        if(garbages[g].active) {
          float dx = garbages[g].x - decomps[i].x; float dy = garbages[g].y - decomps[i].y;
          float dist = dx*dx + dy*dy;
          if(dist < minDist) { minDist = dist; targetG = g; targetS = -1; }
        }
      }
      for(int s=0; s<MAX_SPORES; s++) {
        if(spores[s].active) {
          float dx = spores[s].x - decomps[i].x; float dy = spores[s].y - decomps[i].y;
          float dist = dx*dx + dy*dy;
          if(dist < minDist) { minDist = dist; targetS = s; targetG = -1; }
        }
      }

      if(targetG != -1) {
        float dx = garbages[targetG].x - decomps[i].x; float dy = garbages[targetG].y - decomps[i].y;
        float distSq = dx*dx + dy*dy;
        if(distSq > 0) { float invMag = Q_rsqrt(distSq); decomps[i].vx = (decomps[i].vx * 0.95f) + (dx*invMag * 0.1f); decomps[i].vy = (decomps[i].vy * 0.95f) + (dy*invMag * 0.1f); }
        if(minDist < 36) { garbages[targetG].active = false; decomps[i].energy += 20; decomps[i].flash = 1.0f; }
      } else if(targetS != -1) {
        float dx = spores[targetS].x - decomps[i].x; float dy = spores[targetS].y - decomps[i].y;
        float distSq = dx*dx + dy*dy;
        if(distSq > 0) { float invMag = Q_rsqrt(distSq); decomps[i].vx = (decomps[i].vx * 0.95f) + (dx*invMag * 0.1f); decomps[i].vy = (decomps[i].vy * 0.95f) + (dy*invMag * 0.1f); }
        if(minDist < 36) { spores[targetS].active = false; decomps[i].energy += 20; decomps[i].flash = 1.0f; }
      } else {
        decomps[i].vx += (random(0, 100) * (1.0f / 500.0f)) - 0.1f; decomps[i].vy += (random(0, 100) * (1.0f / 500.0f)) - 0.1f;
      }
      
      float speedSq = decomps[i].vx*decomps[i].vx + decomps[i].vy*decomps[i].vy;
      if(speedSq > 0.49f) { float invSpeed = Q_rsqrt(speedSq); decomps[i].vx = decomps[i].vx*invSpeed*0.7f; decomps[i].vy = decomps[i].vy*invSpeed*0.7f; }
      decomps[i].x += decomps[i].vx; decomps[i].y += decomps[i].vy;
      
      if(decomps[i].x < 0) { decomps[i].x = 0; decomps[i].vx *= -1; }
      if(decomps[i].x > TFT_WIDTH) { decomps[i].x = TFT_WIDTH; decomps[i].vx *= -1; }
      if(decomps[i].y < 0) { decomps[i].y = 0; decomps[i].vy *= -1; }
      if(decomps[i].y > TFT_HEIGHT) { decomps[i].y = TFT_HEIGHT; decomps[i].vy *= -1; }
      
      decomps[i].energy -= 0.015f;
      if(decomps[i].energy <= 0) {
        decomps[i].active = false;
        spawnExplosion(decomps[i].x, decomps[i].y, 100, 255, 100, 5, 0.5f);
        spawnGarbage(decomps[i].x, decomps[i].y, 100, 255, 100);
      } else if(decomps[i].energy > 120) {
        decomps[i].energy -= 80;
        for(int p=0; p<MAX_PLANTS; p++) {
          if(!plants[p].active) {
            plants[p].active = true; plants[p].x = decomps[i].x; plants[p].y = decomps[i].y;
            spawnExplosion(decomps[i].x, decomps[i].y, 100, 255, 50, 10, 1.0f);
            break;
          }
        }
      }
    }

    for(int i=0; i<MAX_SPORES; i++) {
      if(!spores[i].active) continue;
      spores[i].x += spores[i].vx; spores[i].y += spores[i].vy;
      if(spores[i].x > TFT_WIDTH) spores[i].x = 0; if(spores[i].x < 0) spores[i].x = TFT_WIDTH;
      if(spores[i].y > TFT_HEIGHT) spores[i].y = 0; if(spores[i].y < 0) spores[i].y = TFT_HEIGHT;
      
      for(int h=0; h<MAX_HERBS; h++) {
        if(herbs[h].active && !herbs[h].infected) {
          float dx = herbs[h].x - spores[i].x; float dy = herbs[h].y - spores[i].y;
          if(dx*dx + dy*dy < 36) { 
            spores[i].active = false;
            if (random(0, 100) >= herbs[h].immunity * 100.0f) {
              herbs[h].infected = true; herbs[h].flash = 1.0f;
              spawnExplosion(herbs[h].x, herbs[h].y, 180, 0, 255, 8, 1.0f);
            } else {
              spawnExplosion(herbs[h].x, herbs[h].y, 255, 255, 255, 3, 0.5f);
            }
            break;
          }
        }
      }
    }

    for(int i=0; i<MAX_HERBS; i++) {
      if(!herbs[i].active) continue;
      updateHistory(herbs[i]);
      herbs[i].age++;
      
      float alignX = 0, alignY = 0, cohX = 0, cohY = 0;
      int flockCount = 0;
      herbs[i].targetId = -1;

      for(int j=0; j<MAX_HERBS; j++) {
        if (i != j && herbs[j].active) {
          float dX = herbs[j].x - herbs[i].x; float dY = herbs[j].y - herbs[i].y;
          float distSq = dX*dX + dY*dY;
          if (distSq < 1600) { 
            alignX += herbs[j].vx; alignY += herbs[j].vy;
            cohX += herbs[j].x; cohY += herbs[j].y;
            flockCount++;
            if (distSq < 100 && distSq > 0.001f) {
              herbs[i].vx -= (dX/distSq) * 2.0f; herbs[i].vy -= (dY/distSq) * 2.0f;
            }
            if (distSq < 400 && !herbs[i].infected && !herbs[j].infected) {
              if (herbs[i].energy > 60 && herbs[j].energy < 30) {
                if (random(0, 100) < herbs[i].altruism * 100.0f) { 
                  herbs[i].energy -= 1.0f;
                  herbs[j].energy += 1.0f;
                  herbs[i].flash = 1.0f;
                }
              }
            }
          }
        }
      }
      if (flockCount > 0 && !herbs[i].infected) { 
        alignX /= flockCount; alignY /= flockCount;
        float aDistSq = alignX*alignX + alignY*alignY;
        if (aDistSq > 0.000001f) { float inv = Q_rsqrt(aDistSq); herbs[i].vx += alignX * inv * 0.04f; herbs[i].vy += alignY * inv * 0.04f; }
        cohX /= flockCount; cohY /= flockCount;
        float cX = cohX - herbs[i].x; float cY = cohY - herbs[i].y;
        float cDistSq = cX*cX + cY*cY;
        if (cDistSq > 0.000001f) { float inv = Q_rsqrt(cDistSq); herbs[i].vx += cX * inv * 0.015f; herbs[i].vy += cY * inv * 0.015f; }
      }

      float minDist = 99999;
      int target = -1;
      for(int p=0; p<MAX_PLANTS; p++) {
        if(plants[p].active) {
          float dx = plants[p].x - herbs[i].x; float dy = plants[p].y - herbs[i].y;
          if (abs(dx) > 60.0f || abs(dy) > 60.0f) continue;
          float distSq = dx*dx + dy*dy;
          if(distSq < minDist) { minDist = distSq; target = p; }
        }
      }
      if(target != -1) {
        herbs[i].targetId = target;
        float dx = plants[target].x - herbs[i].x; float dy = plants[target].y - herbs[i].y;
        float distSq = dx*dx + dy*dy;
        if(distSq > 0.000001f && !herbs[i].infected) {
          float inv = Q_rsqrt(distSq);
          herbs[i].vx = (herbs[i].vx * 0.97f) + (dx * inv * 0.05f); herbs[i].vy = (herbs[i].vy * 0.97f) + (dy * inv * 0.05f);
        }
        if(minDist < 25) { 
          plants[target].active = false;
          herbs[i].energy += 30; herbs[i].flash = 1.0f; 
          spawnExplosion(plants[target].x, plants[target].y, 150, 255, 150, 5, 0.5f);
        }
      } else {
        herbs[i].vx += (random(0, 100) * (1.0f / 500.0f)) - 0.1f; herbs[i].vy += (random(0, 100) * (1.0f / 500.0f)) - 0.1f;
      }
      
      for(int c=0; c<MAX_CARNS; c++) {
        if(carns[c].active) {
          float dx = herbs[i].x - carns[c].x; float dy = herbs[i].y - carns[c].y;
          if (abs(dx) > 70.0f || abs(dy) > 70.0f) continue;
          float distSq = dx*dx + dy*dy;
          if(distSq < 4000 && distSq > 0.000001f) { 
             float inv = Q_rsqrt(distSq); herbs[i].vx += dx * inv * 0.1f; herbs[i].vy += dy * inv * 0.1f;
          }
        }
      }
      for(int a=0; a<MAX_APEX; a++) {
        if(apex[a].active) {
          float dx = herbs[i].x - apex[a].x; float dy = herbs[i].y - apex[a].y;
          if (abs(dx) > 80.0f || abs(dy) > 80.0f) continue;
          float distSq = dx*dx + dy*dy;
          if(distSq < 6000 && distSq > 0.000001f) { 
             float inv = Q_rsqrt(distSq); herbs[i].vx += dx * inv * 0.12f; herbs[i].vy += dy * inv * 0.12f;
          }
        }
      }
      
      if(herbs[i].infected) {
        if (herbs[i].altruism > 0.6f) {
          float edgeX = (herbs[i].x < TFT_WIDTH / 2) ? -1.0f : 1.0f;
          float edgeY = (herbs[i].y < TFT_HEIGHT / 2) ? -1.0f : 1.0f;
          herbs[i].vx = (herbs[i].vx * 0.9f) + (edgeX * 0.1f);
          herbs[i].vy = (herbs[i].vy * 0.9f) + (edgeY * 0.1f);
        } else {
          herbs[i].vx += (random(0, 100) * (1.0f / 100.0f)) - 0.5f; 
          herbs[i].vy += (random(0, 100) * (1.0f / 100.0f)) - 0.5f;
        }
        herbs[i].energy -= 0.15f; 
        if(random(0,100)<10) spawnExplosion(herbs[i].x, herbs[i].y, 180, 0, 255, 1, 0.2f); 
      }

      float speedLimit = herbs[i].infected ? herbs[i].speedLimit + 0.4f : herbs[i].speedLimit;
      float speed = sqrt(herbs[i].vx*herbs[i].vx + herbs[i].vy*herbs[i].vy);
      if(speed > speedLimit) { herbs[i].vx = (herbs[i].vx/speed)*speedLimit; herbs[i].vy = (herbs[i].vy/speed)*speedLimit; }
      
      herbs[i].x += herbs[i].vx; herbs[i].y += herbs[i].vy;
      
      if(herbs[i].x < 0) { herbs[i].x = 0; herbs[i].vx *= -1; }
      if(herbs[i].x > TFT_WIDTH) { herbs[i].x = TFT_WIDTH; herbs[i].vx *= -1; }
      if(herbs[i].y < 0) { herbs[i].y = 0; herbs[i].vy *= -1; }
      if(herbs[i].y > TFT_HEIGHT) { herbs[i].y = TFT_HEIGHT; herbs[i].vy *= -1; }
      
      float energyDrain = 0.01f + 0.03f * herbs[i].speedLimit + 0.02f * herbs[i].immunity;
      herbs[i].energy -= energyDrain; 
      
      if(herbs[i].energy <= 0) {
        herbs[i].active = false;
        if(herbs[i].infected) {
          spawnSpore(herbs[i].x, herbs[i].y); spawnSpore(herbs[i].x, herbs[i].y);
          spawnExplosion(herbs[i].x, herbs[i].y, 180, 0, 255, 15, 1.0f);
          spawnGarbage(herbs[i].x, herbs[i].y, 180, 0, 255);
        } else {
          spawnExplosion(herbs[i].x, herbs[i].y, 0, 255, 255, 5, 0.5f);
          spawnGarbage(herbs[i].x, herbs[i].y, 0, 255, 255);
        }
      } else if (herbs[i].energy > 120 && !herbs[i].infected) {
        herbs[i].energy -= 50;
        spawnHerb(herbs[i].x, herbs[i].y, herbs[i].speedLimit, herbs[i].altruism, herbs[i].immunity);
      }
    }

    for(int i=0; i<MAX_CARNS; i++) {
      if(!carns[i].active) continue;
      updateHistory(carns[i]);
      carns[i].age++;
      
      float minDist = 10000;
      carns[i].targetId = -1;
      for(int h=0; h<MAX_HERBS; h++) {
        if(herbs[h].active) { 
          float dx = herbs[h].x - carns[i].x; float dy = herbs[h].y - carns[i].y;
          float dist = dx*dx + dy*dy;
          if(dist < minDist) { minDist = dist; carns[i].targetId = h; }
        }
      }
      
      if(carns[i].targetId != -1) {
        float dx = herbs[carns[i].targetId].x - carns[i].x; float dy = herbs[carns[i].targetId].y - carns[i].y;
        float distSq = dx*dx + dy*dy;
        if(distSq > 0.000001f) {
          float inv = Q_rsqrt(distSq);
          carns[i].vx = (carns[i].vx * 0.96f) + (dx * inv * 0.08f); carns[i].vy = (carns[i].vy * 0.96f) + (dy * inv * 0.08f);
        }
        
        if(minDist < 36) { 
          herbs[carns[i].targetId].energy -= 2.5f; 
          carns[i].energy += 2.5f; 
          carns[i].flash = 1.0f; 
          carns[i].vx *= 0.5f; carns[i].vy *= 0.5f;
          herbs[carns[i].targetId].vx *= 0.2f; herbs[carns[i].targetId].vy *= 0.2f;
          if(random(0,100) < 30) spawnExplosion(herbs[carns[i].targetId].x, herbs[carns[i].targetId].y, 0, 255, 255, 1, 0.4f);
        }
      } else {
        carns[i].vx += (random(0, 100) * (1.0f / 500.0f)) - 0.1f; carns[i].vy += (random(0, 100) * (1.0f / 500.0f)) - 0.1f;
      }

      bool escaping = false;
      for(int a=0; a<MAX_APEX; a++) {
        if(apex[a].active) {
          float dx = carns[i].x - apex[a].x; float dy = carns[i].y - apex[a].y;
          if (abs(dx) > 90.0f || abs(dy) > 90.0f) continue;
          float distSq = dx*dx + dy*dy;
          if(distSq < 8000) { 
             escaping = true;
             if(distSq > 0.000001f) { float inv = Q_rsqrt(distSq); carns[i].vx += dx * inv * 0.3f; carns[i].vy += dy * inv * 0.3f; }
          }
        }
      }
      
      float limit = escaping ? carns[i].speedLimit + 0.6f : carns[i].speedLimit;
      float speed = sqrt(carns[i].vx*carns[i].vx + carns[i].vy*carns[i].vy);
      if(speed > limit && speed > 0.001f) { carns[i].vx = (carns[i].vx/speed)*limit; carns[i].vy = (carns[i].vy/speed)*limit; }
      carns[i].x += carns[i].vx; carns[i].y += carns[i].vy;
      
      if(carns[i].x < 0) { carns[i].x = 0; carns[i].vx *= -1; }
      if(carns[i].x > TFT_WIDTH) { carns[i].x = TFT_WIDTH; carns[i].vx *= -1; }
      if(carns[i].y < 0) { carns[i].y = 0; carns[i].vy *= -1; }
      if(carns[i].y > TFT_HEIGHT) { carns[i].y = TFT_HEIGHT; carns[i].vy *= -1; }
      
      carns[i].energy -= 0.06f * (carns[i].speedLimit * (1.0f / 1.1f));
      if(carns[i].energy <= 0) {
        carns[i].active = false;
        spawnExplosion(carns[i].x, carns[i].y, 255, 50, 150, 8, 0.8f);
        spawnGarbage(carns[i].x, carns[i].y, 255, 50, 150);
      } else if (carns[i].energy > 150) {
        carns[i].energy -= 70;
        spawnCarn(carns[i].x, carns[i].y, carns[i].speedLimit);
      }
    }

    for(int i=0; i<MAX_APEX; i++) {
      if(!apex[i].active) continue;
      updateHistory(apex[i]);
      apex[i].age++;
      
      float minDist = 14400;
      apex[i].targetId = -1;
      for(int c=0; c<MAX_CARNS; c++) {
        if(carns[c].active) {
          float dx = carns[c].x - apex[i].x; float dy = carns[c].y - apex[i].y;
          float dist = dx*dx + dy*dy;
          if(dist < minDist) { minDist = dist; apex[i].targetId = c; }
        }
      }
      
      if(apex[i].targetId != -1) {
        float dx = carns[apex[i].targetId].x - apex[i].x;
        float dy = carns[apex[i].targetId].y - apex[i].y;
        float distSq = dx*dx + dy*dy;
        if(distSq > 0.000001f) {
          float inv = Q_rsqrt(distSq);
          apex[i].vx = (apex[i].vx * 0.98f) + (dx * inv * 0.12f); 
          apex[i].vy = (apex[i].vy * 0.98f) + (dy * inv * 0.12f);
        }
        if(minDist < 64) { 
          carns[apex[i].targetId].energy -= 5.0f; 
          apex[i].energy += 5.0f;
          apex[i].flash = 1.0f;
          apex[i].vx *= 0.6f; apex[i].vy *= 0.6f;
          carns[apex[i].targetId].vx *= 0.1f; carns[apex[i].targetId].vy *= 0.1f;
          if(random(0,100) < 40) spawnExplosion(carns[apex[i].targetId].x, carns[apex[i].targetId].y, 255, 50, 150, 2, 0.6f); 
        }
      } else {
        apex[i].vx += (random(0, 100) * (1.0f / 500.0f)) - 0.1f; apex[i].vy += (random(0, 100) * (1.0f / 500.0f)) - 0.1f;
      }
      
      float speed = sqrt(apex[i].vx*apex[i].vx + apex[i].vy*apex[i].vy);
      if(speed > apex[i].speedLimit) { apex[i].vx = (apex[i].vx/speed)*apex[i].speedLimit; apex[i].vy = (apex[i].vy/speed)*apex[i].speedLimit; }
      apex[i].x += apex[i].vx; apex[i].y += apex[i].vy;
      
      if(apex[i].x < 0) { apex[i].x = 0; apex[i].vx *= -1; }
      if(apex[i].x > TFT_WIDTH) { apex[i].x = TFT_WIDTH; apex[i].vx *= -1; }
      if(apex[i].y < 0) { apex[i].y = 0; apex[i].vy *= -1; }
      if(apex[i].y > TFT_HEIGHT) { apex[i].y = TFT_HEIGHT; apex[i].vy *= -1; }
      
      apex[i].energy -= 0.15f * (apex[i].speedLimit * (1.0f / 1.5f)); 
      if(apex[i].energy <= 0) {
        apex[i].active = false;
        spawnExplosion(apex[i].x, apex[i].y, 255, 215, 0, 15, 1.0f);
        spawnGarbage(apex[i].x, apex[i].y, 255, 215, 0);
      }
      else if (apex[i].energy > 500) { apex[i].energy -= 200; spawnApex(apex[i].x, apex[i].y, apex[i].speedLimit); }
    }
    xSemaphoreGive(dataMutex);
    
    vTaskDelay(15 / portTICK_PERIOD_MS);
  }
}

void drawHUD() {
  static unsigned long lastUpdate = 0;
  unsigned long now = millis();

  // RTC Clock Tick Calculation
  if (now - lastClockTick >= 1000) {
    unsigned long elapsed = (now - lastClockTick) / 1000;
    lastClockTick += elapsed * 1000;
    clockSec += elapsed;
    if (clockSec >= 60) {
      clockMin += clockSec / 60;
      clockSec %= 60;
      if (clockMin >= 60) {
        clockHour += clockMin / 60;
        clockMin %= 60;
        if (clockHour >= 24) {
          clockHour %= 24;
        }
      }
    }
  }

  // Fast non-blocking SPI touch polling (every 30ms)
  static unsigned long lastTouchPoll = 0;
  if (now - lastTouchPoll > 30) {
    lastTouchPoll = now;
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      int tx = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, 320);
      int ty = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, 240);

      if (ty >= 30 && ty < 195) {
        isFingerTouching = true;
        fingerX = tx;
        fingerY = ty - OFFSET_Y;
      } else {
        isFingerTouching = false;
      }

      static unsigned long lastTouchBtnTime = 0;
      if (now - lastTouchBtnTime > 220) {
        if (ty >= 195) { // Bottom bar touch area
          lastTouchBtnTime = now;
          if (tx >= 165 && tx <= 210) {
            clockHour = (clockHour + 1) % 24;
          } else if (tx >= 215 && tx <= 260) {
            clockMin = (clockMin + 1) % 60;
          } else if (tx >= 265 && tx <= 315) {
            clockSec = 0;
          }
        }
      }
    } else {
      isFingerTouching = false;
    }
  }

  if (now - lastUpdate < 150) return;
  lastUpdate = now;

  int plantCnt = 0; for(int i=0; i<MAX_PLANTS; i++) if(plants[i].active) plantCnt++;
  int herbCnt  = 0; for(int i=0; i<MAX_HERBS; i++) if(herbs[i].active) herbCnt++;
  int carnCnt  = 0; for(int i=0; i<MAX_CARNS; i++) if(carns[i].active) carnCnt++;
  int apexCnt  = 0; for(int i=0; i<MAX_APEX; i++) if(apex[i].active) apexCnt++;
  int decompCnt= 0; for(int i=0; i<MAX_DECOMPS; i++) if(decomps[i].active) decompCnt++;

  // Differential Update: Top Bar (Only redraw if counts changed)
  static int lastP = -1, lastH = -1, lastC = -1, lastA = -1, lastD = -1;
  if (plantCnt != lastP || herbCnt != lastH || carnCnt != lastC || apexCnt != lastA || decompCnt != lastD) {
    lastP = plantCnt; lastH = herbCnt; lastC = carnCnt; lastA = apexCnt; lastD = decompCnt;

    topHud.fillSprite(TFT_BLACK);
    topHud.setTextDatum(MC_DATUM);
    topHud.setTextColor(TFT_WHITE, TFT_BLACK);
    char buf[64];
    
    snprintf(buf, sizeof(buf), "PLANT %02d", plantCnt);
    topHud.drawString(buf, 32, 17, 1);

    snprintf(buf, sizeof(buf), "HERB %02d", herbCnt);
    topHud.drawString(buf, 96, 17, 1);

    snprintf(buf, sizeof(buf), "CARN %02d", carnCnt);
    topHud.drawString(buf, 160, 17, 1);

    snprintf(buf, sizeof(buf), "APEX %02d", apexCnt);
    topHud.drawString(buf, 224, 17, 1);

    snprintf(buf, sizeof(buf), "DECOMP %02d", decompCnt);
    topHud.drawString(buf, 288, 17, 1);

    topHud.pushSprite(0, 0);
  }

  // Differential Update: Bottom Bar (Only redraw if clock changed)
  static int lastSec = -1, lastMin = -1, lastHour = -1;
  if (clockSec != lastSec || clockMin != lastMin || clockHour != lastHour) {
    lastSec = clockSec; lastMin = clockMin; lastHour = clockHour;

    botHud.fillSprite(TFT_BLACK);

    // Big Digital Clock Display (Font 1, size 2: Crisp Retro Pixel Monospaced)
    char buf[64];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", clockHour, clockMin, clockSec);
    botHud.setTextDatum(ML_DATUM);
    botHud.setTextColor(TFT_WHITE, TFT_BLACK);
    botHud.setTextSize(2);
    botHud.drawString(buf, 14, 17, 1);

    // Minimal White Outline Touch Buttons (Font 1, size 1)
    botHud.setTextSize(1);
    botHud.setTextDatum(MC_DATUM);

    // H+ Button
    botHud.drawRoundRect(170, 4, 40, 26, 3, TFT_WHITE);
    botHud.drawString("H+", 190, 17, 1);

    // M+ Button
    botHud.drawRoundRect(218, 4, 40, 26, 3, TFT_WHITE);
    botHud.drawString("M+", 238, 17, 1);

    // 00s Button
    botHud.drawRoundRect(266, 4, 44, 26, 3, TFT_WHITE);
    botHud.drawString("00s", 288, 17, 1);

    botHud.pushSprite(0, 205);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting CYD Ecosystem...");
  Serial.println("TFT Init...");
  tft.init();
  tft.initDMA();
  tft.setRotation(1);
  // ILI9341 display does not invert colors
  tft.invertDisplay(true); 
  tft.fillScreen(TFT_BLACK);
  
  img.setColorDepth(16); 
  img.createSprite(TFT_WIDTH, TFT_HEIGHT);

  topHud.setColorDepth(16);
  topHud.createSprite(320, 34);

  botHud.setColorDepth(16);
  botHud.createSprite(320, 35);

  touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);

  // Initialize clock from compiler timestamp __TIME__ ("HH:MM:SS")
  int ch = 0, cm = 0, cs = 0;
  if (sscanf(__TIME__, "%d:%d:%d", &ch, &cm, &cs) == 3) {
    clockHour = ch;
    clockMin  = cm;
    clockSec  = cs;
  }
  lastClockTick = millis();

  // Turn on CYD Backlight
  pinMode(CYD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(CYD_BACKLIGHT_PIN, HIGH);
  
  dataMutex = xSemaphoreCreateMutex();

  for(int i=0; i<40; i++) spawnPlant();
  for(int i=0; i<25; i++) spawnHerb(-1, -1);
  for(int i=0; i<3; i++) spawnCarn(-1, -1);
  
  for(int i=0; i<MAX_PLANKTON; i++) {
    planktons[i].x = random(0, TFT_WIDTH);
    planktons[i].y = random(0, TFT_HEIGHT);
    planktons[i].layer = random(1, 4);
    planktons[i].vx = (planktons[i].layer * random(5, 15)) * (1.0f / 40.0f); 
  }

  Serial.println("Creating Task1...");
  xTaskCreatePinnedToCore(core0Task, "ApexTask", 10000, NULL, 1, &Task1, 0); 
  Serial.println("Setup Complete!");
}

void loop() {
  long t = millis();
  img.fillSprite(TFT_BLACK); 

  for(int i=0; i<MAX_PLANKTON; i++) {
    planktons[i].x += planktons[i].vx;
    if(planktons[i].x > TFT_WIDTH) planktons[i].x = 0;
    
    uint16_t pCol;
    if (planktons[i].layer == 1) pCol = myColor(25, 45, 70);
    else if (planktons[i].layer == 2) pCol = myColor(45, 80, 120);
    else pCol = myColor(70, 130, 180);
    
    img.drawPixel(planktons[i].x, planktons[i].y, pCol);
  }

  for(int i=0; i<MAX_PLANTS; i++) {
    if(plants[i].active) {
      float pulse = (sin(t * (1.0f / 150.0f) + i) + 1.0f) * (1.0f / 2.0f); 
      float rot = t * (1.0f / 1000.0f) + i;
      int r = 3 + (pulse * 2.0f);
      
      img.fillSmoothCircle(plants[i].x, plants[i].y, r + 1.5f, fadeColor(20, 150, 20, 0.4f));
      
      float px1 = plants[i].x + cos(rot) * r; float py1 = plants[i].y + sin(rot) * r;
      float px2 = plants[i].x + cos(rot + PI) * r; float py2 = plants[i].y + sin(rot + PI) * r;
      float px3 = plants[i].x + cos(rot + PI/2) * r; float py3 = plants[i].y + sin(rot + PI/2) * r;
      float px4 = plants[i].x + cos(rot - PI/2) * r; float py4 = plants[i].y + sin(rot - PI/2) * r;
      
      img.drawWedgeLine(px1, py1, px2, py2, 0.5f, 0.5f, myColor(150, 255, 150));
      img.drawWedgeLine(px3, py3, px4, py4, 0.5f, 0.5f, myColor(150, 255, 150));
      img.fillSmoothCircle(plants[i].x, plants[i].y, 1.5f, TFT_WHITE);
    }
  }
  
  for(int i=0; i<MAX_CARNS; i++) {
    if(carns[i].active) {
      int tid = carns[i].targetId;
      if(tid >= 0 && tid < MAX_HERBS) {
        if(herbs[tid].active) {
          float dx = carns[i].x - herbs[tid].x; float dy = carns[i].y - herbs[tid].y;
          if(dx*dx + dy*dy < 1600) {
            img.drawWedgeLine(carns[i].x, carns[i].y, herbs[tid].x, herbs[tid].y, 0.5f, 0.5f, myColor(150, 0, 70));
          }
        }
      }
    }
  }
  for(int i=0; i<MAX_APEX; i++) {
    if(apex[i].active) {
      int tid = apex[i].targetId;
      if(tid >= 0 && tid < MAX_CARNS) {
        if(carns[tid].active) {
          float dx = apex[i].x - carns[tid].x; float dy = apex[i].y - carns[tid].y;
          if(dx*dx + dy*dy < 4000) {
            img.drawWedgeLine(apex[i].x, apex[i].y, carns[tid].x, carns[tid].y, 0.5f, 0.5f, myColor(200, 150, 0));
          }
        }
      }
    }
  }

  for(int i=0; i<MAX_SPORES; i++) {
    if(spores[i].active) {
      float pulse = (sin(t * (1.0f / 100.0f) + i) + 1.0f) * (1.0f / 2.0f); 
      int r = 1 + (pulse * 1.5f);
      img.fillSmoothCircle(spores[i].x, spores[i].y, r+1.5f, fadeColor(180, 0, 255, 0.4f));
      img.fillSmoothCircle(spores[i].x, spores[i].y, r, myColor(255, 100, 255));
    }
  }

  auto drawEntity = [&](Entity &e, uint8_t r, uint8_t g, uint8_t b, uint8_t tr, uint8_t tg, uint8_t tb, float wid, bool infected = false) {
    if(!e.active) return;

    float ageFactor = min(e.age / 5000.0f, 1.0f);
    uint8_t baseR = r; uint8_t baseG = g; uint8_t baseB = b;
    if (infected) {
      baseR = 180; baseG = 0; baseB = 255;
    } else {
      baseR = r + (int)((tr - r) * ageFactor);
      baseG = g + (int)((tg - g) * ageFactor);
      baseB = b + (int)((tb - b) * ageFactor);
    }
    
    int step = 3;
    for(int h=0; h<HISTORY_LEN - 1; h += step) {
      int next_h = min(h + step, HISTORY_LEN - 1);
      int idx1 = (e.histIdx - 1 - h + HISTORY_LEN) % HISTORY_LEN;
      int idx2 = (e.histIdx - 1 - next_h + HISTORY_LEN) % HISTORY_LEN;
      float hx1 = e.histX[idx1]; float hy1 = e.histY[idx1];
      float hx2 = e.histX[idx2]; float hy2 = e.histY[idx2];
      
      if(hx1 < -50.0f || hy1 < -50.0f) continue;
      if(hx2 < -50.0f || hy2 < -50.0f) continue;
      
      float distSq = (hx1-hx2)*(hx1-hx2) + (hy1-hy2)*(hy1-hy2);
      if(distSq > 1000) continue;
      if(distSq < 1.0f && h > 0) continue; 
      
      float factor1 = 1.0f - ((float)h / HISTORY_LEN);
      float factor2 = 1.0f - ((float)next_h / HISTORY_LEN);
      
      if (factor1 < 0.05f && factor2 < 0.05f) continue;

      uint16_t color = fadeColor(baseR, baseG, baseB, factor1);
      
      float rad1 = wid * factor1;
      float rad2 = wid * factor2;
      
      if (rad1 > 0.2f || rad2 > 0.2f) {
        img.drawWedgeLine(hx1, hy1, hx2, hy2, rad1, rad2, color);
      }
    }
    
    uint16_t headColor = e.flash > 0 ? TFT_WHITE : myColor(baseR, baseG, baseB);
    if (e.flash > 0) e.flash -= 0.1f;
    
    img.fillSmoothCircle(e.x, e.y, wid, headColor);
  };

  for(int i=0; i<MAX_DECOMPS; i++) drawEntity(decomps[i], 150, 255, 50, 0, 150, 0, 2.5f, false);
  for(int i=0; i<MAX_HERBS; i++) drawEntity(herbs[i], 0, 255, 255, 50, 255, 50, 3.5f, herbs[i].infected);
  for(int i=0; i<MAX_CARNS; i++) drawEntity(carns[i], 255, 50, 150, 255, 0, 0, 4.5f, false);
  for(int i=0; i<MAX_APEX; i++) drawEntity(apex[i], 255, 215, 0, 255, 255, 200, 5.5f, false);

  for(int p=0; p<MAX_PARTICLES; p++) {
    if(particles[p].active) {
      uint16_t c = fadeColor(particles[p].r, particles[p].g, particles[p].b, particles[p].life);
      img.drawPixel(particles[p].x, particles[p].y, c);
    }
  }

  for(int g=0; g<MAX_GARBAGES; g++) {
    if(garbages[g].active) {
      uint16_t gc = fadeColor(garbages[g].r, garbages[g].g, garbages[g].b, 0.7f);
      img.drawLine(garbages[g].x-2, garbages[g].y-2, garbages[g].x+2, garbages[g].y+2, gc);
      img.drawLine(garbages[g].x+2, garbages[g].y-2, garbages[g].x-2, garbages[g].y+2, gc);
    }
  }

  img.pushSprite(0, OFFSET_Y);
  drawHUD();
  
  vTaskDelay(1 / portTICK_PERIOD_MS);
}
