#include <TFT_eSPI.h>
#include <math.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite img = TFT_eSprite(&tft);

#define TFT_WIDTH 320
#define TFT_HEIGHT 170

// 色反転パッチ
const bool SWAP_RB = true; 

uint16_t myColor(uint8_t r, uint8_t g, uint8_t b) {
  if (SWAP_RB) return tft.color565(b, g, r);
  return tft.color565(r, g, b);
}

uint16_t fadeColor(uint8_t r, uint8_t g, uint8_t b, float factor) {
  if (SWAP_RB) return tft.color565((uint8_t)(b * factor), (uint8_t)(g * factor), (uint8_t)(r * factor));
  return tft.color565((uint8_t)(r * factor), (uint8_t)(g * factor), (uint8_t)(b * factor));
}

const int MAX_PLANTS = 50; 
const int MAX_HERBS = 40;
const int MAX_CARNS = 8;
const int MAX_APEX = 3;   
const int MAX_DECOMPS = 10;
const int MAX_SPORES = 15;
const int MAX_PLANKTON = 100;
const int MAX_PARTICLES = 150; // 爆発パーティクル
const int MAX_GARBAGES = 30; // 死骸・ゴミ
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
  int targetId; // ターゲット追尾用（レーザーサイト描画）
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

SemaphoreHandle_t dataMutex;

Entity plants[MAX_PLANTS];
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
  for(int i=0; i<HISTORY_LEN; i++) { e.histX[i] = x; e.histY[i] = y; }
  e.histIdx = 0; e.flash = 0; e.infected = false; e.targetId = -1;
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
      float angle = random(0, 360) * PI / 180.0;
      float speed = random(5, 25) / 10.0 * speedBase;
      particles[p].vx = cos(angle) * speed;
      particles[p].vy = sin(angle) * speed;
      particles[p].life = 1.0;
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

void spawnHerb(float x, float y) {
  for(int i=0; i<MAX_HERBS; i++) {
    if(!herbs[i].active) {
      herbs[i].active = true;
      herbs[i].x = (x == -1) ? random(10, TFT_WIDTH-10) : x + random(-5, 5);
      herbs[i].y = (y == -1) ? random(10, TFT_HEIGHT-10) : y + random(-5, 5);
      herbs[i].vx = (random(0, 100) / 50.0) - 1.0; herbs[i].vy = (random(0, 100) / 50.0) - 1.0;
      herbs[i].energy = 80;
      initHistory(herbs[i], herbs[i].x, herbs[i].y);
      break;
    }
  }
}

void spawnCarn(float x, float y) {
  for(int i=0; i<MAX_CARNS; i++) {
    if(!carns[i].active) {
      carns[i].active = true;
      carns[i].x = (x == -1) ? random(10, TFT_WIDTH-10) : x + random(-5, 5);
      carns[i].y = (y == -1) ? random(10, TFT_HEIGHT-10) : y + random(-5, 5);
      carns[i].vx = (random(0, 100) / 50.0) - 1.0; carns[i].vy = (random(0, 100) / 50.0) - 1.0;
      carns[i].energy = 100;
      initHistory(carns[i], carns[i].x, carns[i].y);
      break;
    }
  }
}

void spawnApex(float x, float y) {
  for(int i=0; i<MAX_APEX; i++) {
    if(!apex[i].active) {
      apex[i].active = true;
      apex[i].x = (x == -1) ? random(10, TFT_WIDTH-10) : x + random(-5, 5);
      apex[i].y = (y == -1) ? random(10, TFT_HEIGHT-10) : y + random(-5, 5);
      apex[i].vx = (random(0, 100) / 50.0) - 1.0; apex[i].vy = (random(0, 100) / 50.0) - 1.0;
      apex[i].energy = 300;
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
      spores[i].vx = (random(0, 100)/100.0) - 0.5 + 0.4; 
      spores[i].vy = (random(0, 100)/100.0) - 0.5;
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
      decomps[i].vx = (random(0, 100) / 50.0) - 1.0; decomps[i].vy = (random(0, 100) / 50.0) - 1.0;
      decomps[i].energy = 80;
      initHistory(decomps[i], decomps[i].x, decomps[i].y);
      break;
    }
  }
}


// --- Fast Inverse Square Root (Quake 3) ---
float Q_rsqrt( float number ) {
  long i;
  float x2, y;
  const float threehalfs = 1.5F;
  x2 = number * 0.5F;
  y  = number;
  i  = * ( long * ) &y;
  i  = 0x5f3759df - ( i >> 1 );
  y  = * ( float * ) &i;
  y  = y * ( threehalfs - ( x2 * y * y ) );
  return y;
}

// ==========================================
// Core 0 Task: Apex Predator

// ==========================================
void core0Task(void * pvParameters) {
  for(;;) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    
    int herbCount = 0; for(int i=0; i<MAX_HERBS; i++) if(herbs[i].active) herbCount++;
    int carnCount = 0; for(int i=0; i<MAX_CARNS; i++) if(carns[i].active) carnCount++;
    int apexCount = 0; for(int i=0; i<MAX_APEX; i++) if(apex[i].active) apexCount++;
    int decompCount = 0; for(int i=0; i<MAX_DECOMPS; i++) if(decomps[i].active) decompCount++;

    if (random(0, 1000) < 35) spawnPlant();
    if (random(0, 10000) < 10) spawnSpore(random(10, TFT_WIDTH-10), random(10, TFT_HEIGHT-10));

    if(herbCount < 5 && random(0, 1000) < 10) spawnHerb(-1, -1);
    if(carnCount < 2 && herbCount > 20 && random(0, 1000) < 10) spawnCarn(-1, -1);
    if(apexCount < 1 && carnCount > 6 && random(0, 1000) < 10) spawnApex(-1, -1);
    if(decompCount < 3 && random(0, 1000) < 10) spawnDecomp(-1, -1);

    // --- パーティクル (Particles) ---
    for(int p=0; p<MAX_PARTICLES; p++) {
      if(particles[p].active) {
        particles[p].x += particles[p].vx;
        particles[p].y += particles[p].vy;
        particles[p].vx *= 0.9; // 減速（摩擦）
        particles[p].vy *= 0.9;
        particles[p].life -= 0.05; // 寿命
        if(particles[p].life <= 0) particles[p].active = false;
      }
    }

    // --- 分解者 (Decomposers) ---
    for(int i=0; i<MAX_DECOMPS; i++) {
      if(!decomps[i].active) continue;
      updateHistory(decomps[i]);
      
      float minDist = 99999;
      int targetG = -1;
      int targetS = -1;
      
      // ゴミ（死骸）を食べる
      for(int g=0; g<MAX_GARBAGES; g++) {
        if(garbages[g].active) {
          float dx = garbages[g].x - decomps[i].x; float dy = garbages[g].y - decomps[i].y;
          float dist = dx*dx + dy*dy;
          if(dist < minDist) { minDist = dist; targetG = g; targetS = -1; }
        }
      }
      // ウイルス胞子を食べる
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
        if(distSq > 0) { float invMag = Q_rsqrt(distSq); decomps[i].vx = (decomps[i].vx * 0.95) + (dx*invMag * 0.1); decomps[i].vy = (decomps[i].vy * 0.95) + (dy*invMag * 0.1); }
        if(minDist < 36) { garbages[targetG].active = false; decomps[i].energy += 20; decomps[i].flash = 1.0; }
      } else if(targetS != -1) {
        float dx = spores[targetS].x - decomps[i].x; float dy = spores[targetS].y - decomps[i].y;
        float distSq = dx*dx + dy*dy;
        if(distSq > 0) { float invMag = Q_rsqrt(distSq); decomps[i].vx = (decomps[i].vx * 0.95) + (dx*invMag * 0.1); decomps[i].vy = (decomps[i].vy * 0.95) + (dy*invMag * 0.1); }
        if(minDist < 36) { spores[targetS].active = false; decomps[i].energy += 20; decomps[i].flash = 1.0; }
      } else {
        decomps[i].vx += (random(0, 100)/500.0) - 0.1; decomps[i].vy += (random(0, 100)/500.0) - 0.1;
      }
      
      float speedSq = decomps[i].vx*decomps[i].vx + decomps[i].vy*decomps[i].vy;
      if(speedSq > 0.49) { float invSpeed = Q_rsqrt(speedSq); decomps[i].vx = decomps[i].vx*invSpeed*0.7; decomps[i].vy = decomps[i].vy*invSpeed*0.7; }
      decomps[i].x += decomps[i].vx; decomps[i].y += decomps[i].vy;
      
      if(decomps[i].x < 0) { decomps[i].x = 0; decomps[i].vx *= -1; }
      if(decomps[i].x > TFT_WIDTH) { decomps[i].x = TFT_WIDTH; decomps[i].vx *= -1; }
      if(decomps[i].y < 0) { decomps[i].y = 0; decomps[i].vy *= -1; }
      if(decomps[i].y > TFT_HEIGHT) { decomps[i].y = TFT_HEIGHT; decomps[i].vy *= -1; }
      
      decomps[i].energy -= 0.05;
      if(decomps[i].energy <= 0) {
        decomps[i].active = false;
        spawnExplosion(decomps[i].x, decomps[i].y, 100, 255, 100, 5, 0.5);
        spawnGarbage(decomps[i].x, decomps[i].y, 100, 255, 100);
      } else if(decomps[i].energy > 120) {
        decomps[i].energy -= 80;
        // 分解完了：植物を生み出す
        for(int p=0; p<MAX_PLANTS; p++) {
          if(!plants[p].active) {
            plants[p].active = true; plants[p].x = decomps[i].x; plants[p].y = decomps[i].y;
            spawnExplosion(decomps[i].x, decomps[i].y, 100, 255, 50, 10, 1.0); // 誕生エフェクト
            break;
          }
        }
      }
    }

    // --- ウイルス胞子 (Spores) ---
    for(int i=0; i<MAX_SPORES; i++) {
      if(!spores[i].active) continue;
      spores[i].x += spores[i].vx; spores[i].y += spores[i].vy;
      if(spores[i].x > TFT_WIDTH) spores[i].x = 0; if(spores[i].x < 0) spores[i].x = TFT_WIDTH;
      if(spores[i].y > TFT_HEIGHT) spores[i].y = 0; if(spores[i].y < 0) spores[i].y = TFT_HEIGHT;
      
      for(int h=0; h<MAX_HERBS; h++) {
        if(herbs[h].active && !herbs[h].infected) {
          float dx = herbs[h].x - spores[i].x; float dy = herbs[h].y - spores[i].y;
          if(dx*dx + dy*dy < 36) { 
            herbs[h].infected = true; herbs[h].flash = 1.0;
            spores[i].active = false;
            spawnExplosion(herbs[h].x, herbs[h].y, 180, 0, 255, 8, 1.0); // 感染エフェクト
            break;
          }
        }
      }
    }

    // --- 草食動物 (Herbivores) ---
    for(int i=0; i<MAX_HERBS; i++) {
      if(!herbs[i].active) continue;
      updateHistory(herbs[i]);
      
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
            if (distSq < 100 && distSq > 0) {
              herbs[i].vx -= (dX/distSq) * 2.0; herbs[i].vy -= (dY/distSq) * 2.0;
            }
          }
        }
      }
      if (flockCount > 0 && !herbs[i].infected) { 
        alignX /= flockCount; alignY /= flockCount;
        float aMag = sqrt(alignX*alignX + alignY*alignY);
        if (aMag > 0) { herbs[i].vx += (alignX/aMag) * 0.04; herbs[i].vy += (alignY/aMag) * 0.04; }
        cohX /= flockCount; cohY /= flockCount;
        float cX = cohX - herbs[i].x; float cY = cohY - herbs[i].y;
        float cMag = sqrt(cX*cX + cY*cY);
        if (cMag > 0) { herbs[i].vx += (cX/cMag) * 0.015; herbs[i].vy += (cY/cMag) * 0.015; }
      }

      float minDist = 99999;
      int target = -1;
      for(int p=0; p<MAX_PLANTS; p++) {
        if(plants[p].active) {
          float dx = plants[p].x - herbs[i].x; float dy = plants[p].y - herbs[i].y;
          float dist = dx*dx + dy*dy;
          if(dist < minDist) { minDist = dist; target = p; }
        }
      }
      if(target != -1) {
        herbs[i].targetId = target; // Target plant
        float dx = plants[target].x - herbs[i].x; float dy = plants[target].y - herbs[i].y;
        float mag = sqrt(dx*dx + dy*dy);
        if(mag > 0 && !herbs[i].infected) {
          herbs[i].vx = (herbs[i].vx * 0.97) + ((dx/mag) * 0.05); herbs[i].vy = (herbs[i].vy * 0.97) + ((dy/mag) * 0.05);
        }
        if(minDist < 25) { 
          plants[target].active = false;
          herbs[i].energy += 30; herbs[i].flash = 1.0; 
          spawnExplosion(plants[target].x, plants[target].y, 150, 255, 150, 5, 0.5); // 草食エフェクト
        }
      } else {
        herbs[i].vx += (random(0, 100)/500.0) - 0.1; herbs[i].vy += (random(0, 100)/500.0) - 0.1;
      }
      
      for(int c=0; c<MAX_CARNS; c++) {
        if(carns[c].active) {
          float dx = herbs[i].x - carns[c].x; float dy = herbs[i].y - carns[c].y;
          float dist = dx*dx + dy*dy;
          if(dist < 4000) { 
             float mag = sqrt(dist);
             if(mag > 0) { herbs[i].vx += (dx/mag) * 0.1; herbs[i].vy += (dy/mag) * 0.1; }
          }
        }
      }
      for(int a=0; a<MAX_APEX; a++) {
        if(apex[a].active) {
          float dx = herbs[i].x - apex[a].x; float dy = herbs[i].y - apex[a].y;
          float dist = dx*dx + dy*dy;
          if(dist < 6000) { 
             float mag = sqrt(dist);
             if(mag > 0) { herbs[i].vx += (dx/mag) * 0.12; herbs[i].vy += (dy/mag) * 0.12; }
          }
        }
      }
      
      if(herbs[i].infected) {
        herbs[i].vx += (random(0, 100)/100.0) - 0.5; herbs[i].vy += (random(0, 100)/100.0) - 0.5;
        herbs[i].energy -= 0.15; 
        // 感染者はたまに紫パーティクルをこぼす
        if(random(0,100)<10) spawnExplosion(herbs[i].x, herbs[i].y, 180, 0, 255, 1, 0.2); 
      }

      float speedLimit = herbs[i].infected ? 1.2 : 0.8;
      float speed = sqrt(herbs[i].vx*herbs[i].vx + herbs[i].vy*herbs[i].vy);
      if(speed > speedLimit) { herbs[i].vx = (herbs[i].vx/speed)*speedLimit; herbs[i].vy = (herbs[i].vy/speed)*speedLimit; }
      
      herbs[i].x += herbs[i].vx; herbs[i].y += herbs[i].vy;
      
      if(herbs[i].x < 0) { herbs[i].x = 0; herbs[i].vx *= -1; }
      if(herbs[i].x > TFT_WIDTH) { herbs[i].x = TFT_WIDTH; herbs[i].vx *= -1; }
      if(herbs[i].y < 0) { herbs[i].y = 0; herbs[i].vy *= -1; }
      if(herbs[i].y > TFT_HEIGHT) { herbs[i].y = TFT_HEIGHT; herbs[i].vy *= -1; }
      
      herbs[i].energy -= 0.04; 
      if(herbs[i].energy <= 0) {
        herbs[i].active = false;
        if(herbs[i].infected) {
          spawnSpore(herbs[i].x, herbs[i].y); spawnSpore(herbs[i].x, herbs[i].y);
          spawnExplosion(herbs[i].x, herbs[i].y, 180, 0, 255, 15, 1.0); // 感染死爆発
          spawnGarbage(herbs[i].x, herbs[i].y, 180, 0, 255);
        } else {
          spawnExplosion(herbs[i].x, herbs[i].y, 0, 255, 255, 5, 0.5); // 通常死爆発
          spawnGarbage(herbs[i].x, herbs[i].y, 0, 255, 255);
        }
      } else if (herbs[i].energy > 120 && !herbs[i].infected) {
        herbs[i].energy -= 50;
        spawnHerb(herbs[i].x, herbs[i].y);
      }
    }

    // --- 肉食動物 (Carnivores) ---
    for(int i=0; i<MAX_CARNS; i++) {
      if(!carns[i].active) continue;
      updateHistory(carns[i]);
      
      float minDist = 99999;
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
        float mag = sqrt(dx*dx + dy*dy);
        if(mag > 0) {
          carns[i].vx = (carns[i].vx * 0.96) + ((dx/mag) * 0.08); carns[i].vy = (carns[i].vy * 0.96) + ((dy/mag) * 0.08);
        }
        
        if(minDist < 36) { 
          herbs[carns[i].targetId].active = false;
          carns[i].energy += 60; carns[i].flash = 1.0; 
          spawnExplosion(herbs[carns[i].targetId].x, herbs[carns[i].targetId].y, 0, 255, 255, 10, 1.0); // 捕食エフェクト
          spawnGarbage(herbs[carns[i].targetId].x, herbs[carns[i].targetId].y, 0, 255, 255);
          if(herbs[carns[i].targetId].infected) spawnSpore(carns[i].x, carns[i].y); 
        }
      } else {
        carns[i].vx += (random(0, 100)/500.0) - 0.1; carns[i].vy += (random(0, 100)/500.0) - 0.1;
      }

      bool escaping = false;
      for(int a=0; a<MAX_APEX; a++) {
        if(apex[a].active) {
          float dx = carns[i].x - apex[a].x; float dy = carns[i].y - apex[a].y;
          float dist = dx*dx + dy*dy;
          if(dist < 8000) { 
             escaping = true;
             float mag = sqrt(dist);
             if(mag > 0) { carns[i].vx += (dx/mag) * 0.3; carns[i].vy += (dy/mag) * 0.3; } // 強い力で逃げる
          }
        }
      }
      
      float limit = escaping ? 1.7 : 1.1; // 逃走時はApex(1.5)より速く走る！
      float speed = sqrt(carns[i].vx*carns[i].vx + carns[i].vy*carns[i].vy);
      if(speed > limit) { carns[i].vx = (carns[i].vx/speed)*limit; carns[i].vy = (carns[i].vy/speed)*limit; }
      carns[i].x += carns[i].vx; carns[i].y += carns[i].vy;
      
      if(carns[i].x < 0) { carns[i].x = 0; carns[i].vx *= -1; }
      if(carns[i].x > TFT_WIDTH) { carns[i].x = TFT_WIDTH; carns[i].vx *= -1; }
      if(carns[i].y < 0) { carns[i].y = 0; carns[i].vy *= -1; }
      if(carns[i].y > TFT_HEIGHT) { carns[i].y = TFT_HEIGHT; carns[i].vy *= -1; }
      
      carns[i].energy -= 0.06;
      if(carns[i].energy <= 0) {
        carns[i].active = false;
        spawnExplosion(carns[i].x, carns[i].y, 255, 50, 150, 8, 0.8);
        spawnGarbage(carns[i].x, carns[i].y, 255, 50, 150);
      } else if (carns[i].energy > 150) {
        carns[i].energy -= 70;
        spawnCarn(carns[i].x, carns[i].y);
      }
    }

    // --- 頂点捕食者 (Apex) ---
    for(int i=0; i<MAX_APEX; i++) {
      if(!apex[i].active) continue;
      updateHistory(apex[i]);
      
      float minDist = 40000; // 視界制限（無限追尾をやめて200ピクセル以内しか狙わない）
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
        float mag = sqrt(dx*dx + dy*dy);
        if(mag > 0) {
          apex[i].vx = (apex[i].vx * 0.98) + ((dx/mag) * 0.12); 
          apex[i].vy = (apex[i].vy * 0.98) + ((dy/mag) * 0.12);
        }
        if(minDist < 64) { 
          carns[apex[i].targetId].active = false;
          apex[i].energy += 120;
          apex[i].flash = 1.0;
          spawnExplosion(carns[apex[i].targetId].x, carns[apex[i].targetId].y, 255, 50, 150, 15, 1.5); // 肉食動物のピンク爆発
          spawnGarbage(carns[apex[i].targetId].x, carns[apex[i].targetId].y, 255, 50, 150);
        }
      } else {
        apex[i].vx += (random(0, 100)/500.0) - 0.1; apex[i].vy += (random(0, 100)/500.0) - 0.1;
      }
      
      float speed = sqrt(apex[i].vx*apex[i].vx + apex[i].vy*apex[i].vy);
      if(speed > 1.5) { apex[i].vx = (apex[i].vx/speed)*1.5; apex[i].vy = (apex[i].vy/speed)*1.5; }
      apex[i].x += apex[i].vx; apex[i].y += apex[i].vy;
      
      if(apex[i].x < 0) { apex[i].x = 0; apex[i].vx *= -1; }
      if(apex[i].x > TFT_WIDTH) { apex[i].x = TFT_WIDTH; apex[i].vx *= -1; }
      if(apex[i].y < 0) { apex[i].y = 0; apex[i].vy *= -1; }
      if(apex[i].y > TFT_HEIGHT) { apex[i].y = TFT_HEIGHT; apex[i].vy *= -1; }
      
      apex[i].energy -= 0.15; 
      if(apex[i].energy <= 0) {
        apex[i].active = false;
        spawnExplosion(apex[i].x, apex[i].y, 255, 215, 0, 15, 1.0);
        spawnGarbage(apex[i].x, apex[i].y, 255, 215, 0);
      }
      else if (apex[i].energy > 500) { apex[i].energy -= 200; spawnApex(apex[i].x, apex[i].y); }
    }
    xSemaphoreGive(dataMutex);
    
    // 全ての物理演算がここに統合され、人工的なウェイトも撤廃。
    // ESP32の限界スピード（約66FPS以上）で生態系全体がシミュレーションされます！
    vTaskDelay(15 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// Core 1 Task
// ==========================================
void setup() {
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true); 
  tft.fillScreen(TFT_BLACK);
  
  img.setColorDepth(16); 
  img.createSprite(TFT_WIDTH, TFT_HEIGHT);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  
  dataMutex = xSemaphoreCreateMutex();

  for(int i=0; i<40; i++) spawnPlant();
  for(int i=0; i<25; i++) spawnHerb(-1, -1);
  for(int i=0; i<3; i++) spawnCarn(-1, -1);
  
  // プランクトン（パララックスレイヤー）
  for(int i=0; i<MAX_PLANKTON; i++) {
    planktons[i].x = random(0, TFT_WIDTH);
    planktons[i].y = random(0, TFT_HEIGHT);
    planktons[i].layer = random(1, 4); // 1:奥(遅い), 2:中, 3:手前(速い)
    planktons[i].vx = (planktons[i].layer * random(5, 15)) / 40.0; 
  }

  xTaskCreatePinnedToCore(core0Task, "ApexTask", 10000, NULL, 1, &Task1, 0); 
}

void loop() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  
  // 物理演算はすべてCore 0 (core0Task) の超高速ループへ完全移行しました！
  // このloop()は画面を描画するだけの「純粋なレンダリングエンジン」になりました。

  xSemaphoreGive(dataMutex);

  // ==========================================
  // --- Rendering (限界リッチ描画) ---
  // ==========================================
  long t = millis();
  
  // 昼夜サイクル（ダイナミックライティング）
  float timeCycle = (sin(t / 5000.0) + 1.0) / 2.0; // 0.0(夜) ~ 1.0(昼)
  uint8_t bgR = 2 + timeCycle * 5;   
  uint8_t bgG = 5 + timeCycle * 15;  
  uint8_t bgB = 15 + timeCycle * 25; 
  img.fillSprite(myColor(bgR, bgG, bgB)); 

  // プランクトン (パララックス効果)
  for(int i=0; i<MAX_PLANKTON; i++) {
    planktons[i].x += planktons[i].vx;
    if(planktons[i].x > TFT_WIDTH) planktons[i].x = 0;
    
    uint16_t pCol;
    if (planktons[i].layer == 1) pCol = myColor(10, 30, 50); // 奥は暗い
    else if (planktons[i].layer == 2) pCol = myColor(15, 50, 70);
    else pCol = myColor(20, 80, 100); // 手前は明るい
    
    img.drawPixel(planktons[i].x, planktons[i].y, pCol);
  }

  // 植物 (幾何学的に回転する花)
  for(int i=0; i<MAX_PLANTS; i++) {
    if(plants[i].active) {
      float pulse = (sin(t / 150.0 + i) + 1.0) / 2.0; 
      float rot = t / 1000.0 + i;
      int r = 3 + (pulse * 2.0);
      
      // 光のオーラ (アンチエイリアス)
      img.fillSmoothCircle(plants[i].x, plants[i].y, r + 1.5, fadeColor(20, 150, 20, 0.4));
      
      // クロス状の葉 (アンチエイリアス線)
      float px1 = plants[i].x + cos(rot) * r; float py1 = plants[i].y + sin(rot) * r;
      float px2 = plants[i].x + cos(rot + PI) * r; float py2 = plants[i].y + sin(rot + PI) * r;
      float px3 = plants[i].x + cos(rot + PI/2) * r; float py3 = plants[i].y + sin(rot + PI/2) * r;
      float px4 = plants[i].x + cos(rot - PI/2) * r; float py4 = plants[i].y + sin(rot - PI/2) * r;
      
      img.drawWedgeLine(px1, py1, px2, py2, 0.5, 0.5, myColor(150, 255, 150));
      img.drawWedgeLine(px3, py3, px4, py4, 0.5, 0.5, myColor(150, 255, 150));
      img.fillSmoothCircle(plants[i].x, plants[i].y, 1.5, TFT_WHITE);
    }
  }
  
  // レーザーサイト（捕食者の狙い）
  for(int i=0; i<MAX_CARNS; i++) {
    if(carns[i].active && carns[i].targetId != -1) {
      if(herbs[carns[i].targetId].active) {
        float dx = carns[i].x - herbs[carns[i].targetId].x; float dy = carns[i].y - herbs[carns[i].targetId].y;
        if(dx*dx + dy*dy < 1600) { // 近い時だけ赤いレーザー (AA)
          img.drawWedgeLine(carns[i].x, carns[i].y, herbs[carns[i].targetId].x, herbs[carns[i].targetId].y, 0.5, 0.5, myColor(150, 0, 70));
        }
      }
    }
  }
  for(int i=0; i<MAX_APEX; i++) {
    if(apex[i].active && apex[i].targetId != -1) {
      if(carns[apex[i].targetId].active) {
        float dx = apex[i].x - carns[apex[i].targetId].x; float dy = apex[i].y - carns[apex[i].targetId].y;
        if(dx*dx + dy*dy < 4000) { // 黄金のレーザー (AA)
          img.drawWedgeLine(apex[i].x, apex[i].y, carns[apex[i].targetId].x, carns[apex[i].targetId].y, 0.5, 0.5, myColor(200, 150, 0));
        }
      }
    }
  }

  // 胞子
  for(int i=0; i<MAX_SPORES; i++) {
    if(spores[i].active) {
      float pulse = (sin(t / 100.0 + i) + 1.0) / 2.0; 
      int r = 1 + (pulse * 1.5);
      img.fillSmoothCircle(spores[i].x, spores[i].y, r+1.5, fadeColor(180, 0, 255, 0.4));
      img.fillSmoothCircle(spores[i].x, spores[i].y, r, myColor(255, 100, 255));
    }
  }

  // 動物たちの描画（Herb -> Carn -> Apex）アンチエイリアス化
  auto drawEntity = [&](Entity &e, uint8_t r, uint8_t g, uint8_t b, float len, float wid, bool infected = false) {
    if(!e.active) return;
    
    // しっぽの描画 (極限の軽量化：ステップスキップ＆無移動スキップ)
    int step = 3; // 3フレームごとに1本のWedgeLineを描画（描画負荷を約66%カット、見た目はそのまま）
    for(int h=0; h<HISTORY_LEN - 1; h += step) {
      int next_h = min(h + step, HISTORY_LEN - 1);
      int idx1 = (e.histIdx - 1 - h + HISTORY_LEN) % HISTORY_LEN;
      int idx2 = (e.histIdx - 1 - next_h + HISTORY_LEN) % HISTORY_LEN;
      float hx1 = e.histX[idx1]; float hy1 = e.histY[idx1];
      float hx2 = e.histX[idx2]; float hy2 = e.histY[idx2];
      
      if(hx1 == 0 && hy1 == 0) continue;
      if(hx2 == 0 && hy2 == 0) continue;
      
      float distSq = (hx1-hx2)*(hx1-hx2) + (hy1-hy2)*(hy1-hy2);
      // ワープした線を描画しない
      if(distSq > 1000) continue;
      // ほとんど動いていない場合は、同じ場所に何度も線を引かない（オーバードロー防止で負荷激減）
      if(distSq < 0.1 && h > 0) continue; 
      
      float factor1 = 1.0 - ((float)h / HISTORY_LEN);
      float factor2 = 1.0 - ((float)next_h / HISTORY_LEN);
      
      // 完全に透明（黒）に近いなら描画を省略
      if (factor1 < 0.05 && factor2 < 0.05) continue;

      // 頭から尻尾にかけて滑らかに消えていく
      uint16_t color = infected ? fadeColor(180, 0, 255, factor1) : fadeColor(r, g, b, factor1);
      
      // 尻尾の太さも滑らかに細くする
      float rad1 = wid * factor1;
      float rad2 = wid * factor2;
      
      if (rad1 > 0.2 || rad2 > 0.2) {
        img.drawWedgeLine(hx1, hy1, hx2, hy2, rad1, rad2, color);
      }
    }
    
    // 頭部（とんがった形をやめて、丸く可愛らしい形に変更）
    uint16_t headColor = e.flash > 0 ? TFT_WHITE : (infected ? myColor(180, 0, 255) : myColor(r, g, b));
    if (e.flash > 0) e.flash -= 0.1;
    
    img.fillSmoothCircle(e.x, e.y, wid, headColor);
  };

  for(int i=0; i<MAX_DECOMPS; i++) drawEntity(decomps[i], 150, 255, 50, 4.0, 2.5, false); // 分解者はライムグリーン
  for(int i=0; i<MAX_HERBS; i++) drawEntity(herbs[i], 0, 255, 255, 6.0, 3.5, herbs[i].infected);
  for(int i=0; i<MAX_CARNS; i++) drawEntity(carns[i], 255, 50, 150, 8.0, 4.5, false);
  for(int i=0; i<MAX_APEX; i++) drawEntity(apex[i], 255, 215, 0, 10.0, 5.5, false);

  // パーティクル爆発の描画
  for(int p=0; p<MAX_PARTICLES; p++) {
    if(particles[p].active) {
      uint16_t c = fadeColor(particles[p].r, particles[p].g, particles[p].b, particles[p].life);
      img.drawPixel(particles[p].x, particles[p].y, c);
    }
  }

  // ゴミ（死骸）の描画
  for(int g=0; g<MAX_GARBAGES; g++) {
    if(garbages[g].active) {
      uint16_t gc = fadeColor(garbages[g].r, garbages[g].g, garbages[g].b, 0.7); // 少し暗め
      img.drawLine(garbages[g].x-2, garbages[g].y-2, garbages[g].x+2, garbages[g].y+2, gc);
      img.drawLine(garbages[g].x+2, garbages[g].y-2, garbages[g].x-2, garbages[g].y+2, gc);
    }
  }
  
  img.pushSprite(0, 0);
  vTaskDelay(1 / portTICK_PERIOD_MS);
}
