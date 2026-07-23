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
  if (SWAP_RB) return tft.color565((uint8_t)(b * factor + 0.5f), (uint8_t)(g * factor + 0.5f), (uint8_t)(r * factor + 0.5f));
  return tft.color565((uint8_t)(r * factor + 0.5f), (uint8_t)(g * factor + 0.5f), (uint8_t)(b * factor + 0.5f));
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
  int targetId;
  float speedLimit;
  int age;
  float altruism; // 利他性遺伝子 (0.0:利己的 〜 1.0:自己犠牲的)
  float immunity; // 免疫力遺伝子 (0.0:燃費良 〜 1.0:ウイルス耐性)
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
  for(int i=0; i<HISTORY_LEN; i++) { e.histX[i] = -100.0f; e.histY[i] = -100.0f; } // 番兵として-100を使用
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
  garbages[garbageIdx].active = true; // 意図的なリングバッファ上書き（古いゴミから消える）
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
      
      // 利他性の遺伝と突然変異
      if (pAltruism == -1.0f) {
        herbs[i].altruism = random(0, 100) * 0.01f; // 初代はランダム
      } else {
        herbs[i].altruism = pAltruism + (random(-10, 11) * 0.01f);
        if(herbs[i].altruism < 0.0f) herbs[i].altruism = 0.0f;
        if(herbs[i].altruism > 1.0f) herbs[i].altruism = 1.0f;
      }

      // 免疫力の遺伝と突然変異
      if (pImmunity == -1.0f) {
        herbs[i].immunity = random(0, 100) * 0.01f; // 初代はランダム
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


// --- Fast Inverse Square Root (Quake 3) ---
float Q_rsqrt( float number ) {
  long i;
  float x2, y;
  const float threehalfs = 1.5F;
  x2 = number * 0.5F;
  y  = number;
  memcpy(&i, &y, sizeof(i)); // strict-aliasing違反回避
  i  = 0x5f3759df - ( i >> 1 );
  memcpy(&y, &i, sizeof(y)); // strict-aliasing違反回避
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
        particles[p].vx *= 0.9f; // 減速（摩擦）
        particles[p].vy *= 0.9f;
        particles[p].life -= 0.05f; // 寿命
        if(particles[p].life <= 0) particles[p].active = false;
      }
    }

    // --- 分解者 (Decomposers) ---
    for(int i=0; i<MAX_DECOMPS; i++) {
      if(!decomps[i].active) continue;
      updateHistory(decomps[i]);
      decomps[i].age++;
      
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
      
      decomps[i].energy -= 0.05f;
      if(decomps[i].energy <= 0) {
        decomps[i].active = false;
        spawnExplosion(decomps[i].x, decomps[i].y, 100, 255, 100, 5, 0.5f);
        spawnGarbage(decomps[i].x, decomps[i].y, 100, 255, 100);
      } else if(decomps[i].energy > 120) {
        decomps[i].energy -= 80;
        // 分解完了：植物を生み出す
        for(int p=0; p<MAX_PLANTS; p++) {
          if(!plants[p].active) {
            plants[p].active = true; plants[p].x = decomps[i].x; plants[p].y = decomps[i].y;
            spawnExplosion(decomps[i].x, decomps[i].y, 100, 255, 50, 10, 1.0f); // 誕生エフェクト
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
            spores[i].active = false;
            // 免疫力遺伝子による感染ブロック判定
            if (random(0, 100) >= herbs[h].immunity * 100.0f) {
              herbs[h].infected = true; herbs[h].flash = 1.0f;
              spawnExplosion(herbs[h].x, herbs[h].y, 180, 0, 255, 8, 1.0f); // 感染エフェクト
            } else {
              spawnExplosion(herbs[h].x, herbs[h].y, 255, 255, 255, 3, 0.5f); // 免疫で弾いたエフェクト（白）
            }
            break;
          }
        }
      }
    }

    // --- 草食動物 (Herbivores) ---
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
            if (distSq < 100 && distSq > 0) {
              herbs[i].vx -= (dX/distSq) * 2.0f; herbs[i].vy -= (dY/distSq) * 2.0f;
            }
            // 【利他行動1】餌（エネルギー）の分け合い
            if (distSq < 400 && !herbs[i].infected && !herbs[j].infected) {
              if (herbs[i].energy > 60 && herbs[j].energy < 30) {
                // 利他性が高いほど、高い確率でエネルギーを分け与える
                if (random(0, 100) < herbs[i].altruism * 100.0f) { 
                  herbs[i].energy -= 1.0f;
                  herbs[j].energy += 1.0f;
                  herbs[i].flash = 1.0f; // 分け与えた個体は白く光る
                }
              }
            }
          }
        }
      }
      if (flockCount > 0 && !herbs[i].infected) { 
        alignX /= flockCount; alignY /= flockCount;
        float aMag = sqrt(alignX*alignX + alignY*alignY);
        if (aMag > 0) { herbs[i].vx += (alignX/aMag) * 0.04f; herbs[i].vy += (alignY/aMag) * 0.04f; }
        cohX /= flockCount; cohY /= flockCount;
        float cX = cohX - herbs[i].x; float cY = cohY - herbs[i].y;
        float cMag = sqrt(cX*cX + cY*cY);
        if (cMag > 0) { herbs[i].vx += (cX/cMag) * 0.015f; herbs[i].vy += (cY/cMag) * 0.015f; }
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
          herbs[i].vx = (herbs[i].vx * 0.97f) + ((dx/mag) * 0.05f); herbs[i].vy = (herbs[i].vy * 0.97f) + ((dy/mag) * 0.05f);
        }
        if(minDist < 25) { 
          plants[target].active = false;
          herbs[i].energy += 30; herbs[i].flash = 1.0f; 
          spawnExplosion(plants[target].x, plants[target].y, 150, 255, 150, 5, 0.5f); // 草食エフェクト
        }
      } else {
        herbs[i].vx += (random(0, 100) * (1.0f / 500.0f)) - 0.1f; herbs[i].vy += (random(0, 100) * (1.0f / 500.0f)) - 0.1f;
      }
      
      for(int c=0; c<MAX_CARNS; c++) {
        if(carns[c].active) {
          float dx = herbs[i].x - carns[c].x; float dy = herbs[i].y - carns[c].y;
          float dist = dx*dx + dy*dy;
          if(dist < 4000) { 
             float mag = sqrt(dist);
             if(mag > 0) { herbs[i].vx += (dx/mag) * 0.1f; herbs[i].vy += (dy/mag) * 0.1f; }
          }
        }
      }
      for(int a=0; a<MAX_APEX; a++) {
        if(apex[a].active) {
          float dx = herbs[i].x - apex[a].x; float dy = herbs[i].y - apex[a].y;
          float dist = dx*dx + dy*dy;
          if(dist < 6000) { 
             float mag = sqrt(dist);
             if(mag > 0) { herbs[i].vx += (dx/mag) * 0.12f; herbs[i].vy += (dy/mag) * 0.12f; }
          }
        }
      }
      
      if(herbs[i].infected) {
        // 【利他行動2】感染時の自主隔離
        if (herbs[i].altruism > 0.6f) {
          // 利他性が高い個体は、仲間にうつさないよう画面の隅（壁）へ向かおうとする
          float edgeX = (herbs[i].x < TFT_WIDTH / 2) ? -1.0f : 1.0f;
          float edgeY = (herbs[i].y < TFT_HEIGHT / 2) ? -1.0f : 1.0f;
          herbs[i].vx = (herbs[i].vx * 0.9f) + (edgeX * 0.1f);
          herbs[i].vy = (herbs[i].vy * 0.9f) + (edgeY * 0.1f);
        } else {
          // 利己的・普通の個体は構わずフラフラ歩き回る（パンデミックの原因になる）
          herbs[i].vx += (random(0, 100) * (1.0f / 100.0f)) - 0.5f; 
          herbs[i].vy += (random(0, 100) * (1.0f / 100.0f)) - 0.5f;
        }
        herbs[i].energy -= 0.15f; 
        // 感染者はたまに紫パーティクルをこぼす
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
      
      // 代謝（エネルギー消費）。スピードが速いほど、また免疫力が高いほど消費が激しい。
      float energyDrain = 0.01f + 0.03f * herbs[i].speedLimit + 0.02f * herbs[i].immunity;
      herbs[i].energy -= energyDrain; 
      
      if(herbs[i].energy <= 0) {
        herbs[i].active = false;
        if(herbs[i].infected) {
          spawnSpore(herbs[i].x, herbs[i].y); spawnSpore(herbs[i].x, herbs[i].y);
          spawnExplosion(herbs[i].x, herbs[i].y, 180, 0, 255, 15, 1.0f); // 感染死爆発
          spawnGarbage(herbs[i].x, herbs[i].y, 180, 0, 255);
        } else {
          spawnExplosion(herbs[i].x, herbs[i].y, 0, 255, 255, 5, 0.5f); // 通常死爆発
          spawnGarbage(herbs[i].x, herbs[i].y, 0, 255, 255);
        }
      } else if (herbs[i].energy > 120 && !herbs[i].infected) {
        herbs[i].energy -= 50;
        spawnHerb(herbs[i].x, herbs[i].y, herbs[i].speedLimit, herbs[i].altruism, herbs[i].immunity);
      }
    }

    // --- 肉食動物 (Carnivores) ---
    for(int i=0; i<MAX_CARNS; i++) {
      if(!carns[i].active) continue;
      updateHistory(carns[i]);
      carns[i].age++;
      
      float minDist = 10000; // 視界制限（半径100ピクセル以内しか狙わない。逃げ道を作る）
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
          carns[i].vx = (carns[i].vx * 0.96f) + ((dx/mag) * 0.08f); carns[i].vy = (carns[i].vy * 0.96f) + ((dy/mag) * 0.08f);
        }
        
        if(minDist < 36) { 
          // 食いつき処理（時間をかけて捕食）
          herbs[carns[i].targetId].energy -= 2.5f; 
          carns[i].energy += 2.5f; 
          carns[i].flash = 1.0f; 
          // 噛み付いている間は両者とも足が遅くなる
          carns[i].vx *= 0.5f; carns[i].vy *= 0.5f;
          herbs[carns[i].targetId].vx *= 0.2f; herbs[carns[i].targetId].vy *= 0.2f;
          // 血飛沫（たまに出る）
          if(random(0,100) < 30) spawnExplosion(herbs[carns[i].targetId].x, herbs[carns[i].targetId].y, 0, 255, 255, 1, 0.4f);
        }
      } else {
        carns[i].vx += (random(0, 100) * (1.0f / 500.0f)) - 0.1f; carns[i].vy += (random(0, 100) * (1.0f / 500.0f)) - 0.1f;
      }

      bool escaping = false;
      for(int a=0; a<MAX_APEX; a++) {
        if(apex[a].active) {
          float dx = carns[i].x - apex[a].x; float dy = carns[i].y - apex[a].y;
          float dist = dx*dx + dy*dy;
          if(dist < 8000) { 
             escaping = true;
             float mag = sqrt(dist);
             if(mag > 0) { carns[i].vx += (dx/mag) * 0.3f; carns[i].vy += (dy/mag) * 0.3f; } // 強い力で逃げる
          }
        }
      }
      
      float limit = escaping ? carns[i].speedLimit + 0.6f : carns[i].speedLimit; // 逃走時はApex(1.5f)より速く走る！
      float speed = sqrt(carns[i].vx*carns[i].vx + carns[i].vy*carns[i].vy);
      if(speed > limit) { carns[i].vx = (carns[i].vx/speed)*limit; carns[i].vy = (carns[i].vy/speed)*limit; }
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

    // --- 頂点捕食者 (Apex) ---
    for(int i=0; i<MAX_APEX; i++) {
      if(!apex[i].active) continue;
      updateHistory(apex[i]);
      apex[i].age++;
      
      float minDist = 14400; // 視界制限（半径120ピクセル以内しか狙わない）
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
          apex[i].vx = (apex[i].vx * 0.98f) + ((dx/mag) * 0.12f); 
          apex[i].vy = (apex[i].vy * 0.98f) + ((dy/mag) * 0.12f);
        }
        if(minDist < 64) { 
          // 頂点捕食者の食いつき処理
          carns[apex[i].targetId].energy -= 5.0f; 
          apex[i].energy += 5.0f;
          apex[i].flash = 1.0f;
          // 丸呑みではなく、少しもみ合いになる
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
    planktons[i].vx = (planktons[i].layer * random(5, 15)) * (1.0f / 40.0f); 
  }

  xTaskCreatePinnedToCore(core0Task, "ApexTask", 10000, NULL, 1, &Task1, 0); 
}

void loop() {
  // ==========================================
  // --- Rendering (限界リッチ描画) ---
  // ==========================================
  // 【意図的に緩い同期（テアリング許容）】
  // ここで描画全体をミューテックスで囲むと、Core 1の重い描画処理中（数十ms）に
  // Core 0の物理演算が完全にブロックされ、並列処理のメリットが死んで激重になります。
  // そのため、見た目のシミュレーションとして割り切り、ロックを行わずに配列を読みます。
  // （一瞬の座標ズレやテアリングは許容する、組み込み特有のパフォーマンス優先設計）
  
  long t = millis();
  
  // 昼夜サイクル（ダイナミックライティング）
  float timeCycle = (sin(t * (1.0f / 5000.0f)) + 1.0f) * (1.0f / 2.0f); // 0.0f(夜) ~ 1.0f(昼)
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
      float pulse = (sin(t * (1.0f / 150.0f) + i) + 1.0f) * (1.0f / 2.0f); 
      float rot = t * (1.0f / 1000.0f) + i;
      int r = 3 + (pulse * 2.0f);
      
      // 光のオーラ (アンチエイリアス)
      img.fillSmoothCircle(plants[i].x, plants[i].y, r + 1.5f, fadeColor(20, 150, 20, 0.4f));
      
      // クロス状の葉 (アンチエイリアス線)
      float px1 = plants[i].x + cos(rot) * r; float py1 = plants[i].y + sin(rot) * r;
      float px2 = plants[i].x + cos(rot + PI) * r; float py2 = plants[i].y + sin(rot + PI) * r;
      float px3 = plants[i].x + cos(rot + PI/2) * r; float py3 = plants[i].y + sin(rot + PI/2) * r;
      float px4 = plants[i].x + cos(rot - PI/2) * r; float py4 = plants[i].y + sin(rot - PI/2) * r;
      
      img.drawWedgeLine(px1, py1, px2, py2, 0.5f, 0.5f, myColor(150, 255, 150));
      img.drawWedgeLine(px3, py3, px4, py4, 0.5f, 0.5f, myColor(150, 255, 150));
      img.fillSmoothCircle(plants[i].x, plants[i].y, 1.5f, TFT_WHITE);
    }
  }
  
  // レーザーサイト（捕食者の狙い）
  for(int i=0; i<MAX_CARNS; i++) {
    if(carns[i].active) {
      int tid = carns[i].targetId;
      if(tid >= 0 && tid < MAX_HERBS) {
        if(herbs[tid].active) {
          float dx = carns[i].x - herbs[tid].x; float dy = carns[i].y - herbs[tid].y;
          if(dx*dx + dy*dy < 1600) { // 近い時だけ赤いレーザー (AA)
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
          if(dx*dx + dy*dy < 4000) { // 黄金のレーザー (AA)
            img.drawWedgeLine(apex[i].x, apex[i].y, carns[tid].x, carns[tid].y, 0.5f, 0.5f, myColor(200, 150, 0));
          }
        }
      }
    }
  }

  // 胞子
  for(int i=0; i<MAX_SPORES; i++) {
    if(spores[i].active) {
      float pulse = (sin(t * (1.0f / 100.0f) + i) + 1.0f) * (1.0f / 2.0f); 
      int r = 1 + (pulse * 1.5f);
      img.fillSmoothCircle(spores[i].x, spores[i].y, r+1.5f, fadeColor(180, 0, 255, 0.4f));
      img.fillSmoothCircle(spores[i].x, spores[i].y, r, myColor(255, 100, 255));
    }
  }

  // 動物たちの描画（Herb -> Carn -> Apex）アンチエイリアス化
  auto drawEntity = [&](Entity &e, uint8_t r, uint8_t g, uint8_t b, uint8_t tr, uint8_t tg, uint8_t tb, float len, float wid, bool infected = false) {
    if(!e.active) return;

    // 長生きによる色の変化（歴戦のオーラ色へシフト）
    float ageFactor = min(e.age / 5000.0f, 1.0f); // 約1.5分生存でMAX
    uint8_t baseR = r; uint8_t baseG = g; uint8_t baseB = b;
    if (infected) {
      baseR = 180; baseG = 0; baseB = 255;
    } else {
      baseR = r + (int)((tr - r) * ageFactor);
      baseG = g + (int)((tg - g) * ageFactor);
      baseB = b + (int)((tb - b) * ageFactor);
    }
    
    // しっぽの描画 (極限の軽量化：ステップスキップ＆無移動スキップ)
    int step = 3; // 3フレームごとに1本のWedgeLineを描画（描画負荷を約66%カット、見た目はそのまま）
    for(int h=0; h<HISTORY_LEN - 1; h += step) {
      int next_h = min(h + step, HISTORY_LEN - 1);
      int idx1 = (e.histIdx - 1 - h + HISTORY_LEN) % HISTORY_LEN;
      int idx2 = (e.histIdx - 1 - next_h + HISTORY_LEN) % HISTORY_LEN;
      float hx1 = e.histX[idx1]; float hy1 = e.histY[idx1];
      float hx2 = e.histX[idx2]; float hy2 = e.histY[idx2];
      
      if(hx1 < -50.0f || hy1 < -50.0f) continue;
      if(hx2 < -50.0f || hy2 < -50.0f) continue;
      
      float distSq = (hx1-hx2)*(hx1-hx2) + (hy1-hy2)*(hy1-hy2);
      // ワープした線を描画しない
      if(distSq > 1000) continue;
      // ほとんど動いていない場合（1ピクセル未満）は、同じ場所に何度も線を引かない（負荷をさらに激減）
      if(distSq < 1.0f && h > 0) continue; 
      
      float factor1 = 1.0f - ((float)h / HISTORY_LEN);
      float factor2 = 1.0f - ((float)next_h / HISTORY_LEN);
      
      // 完全に透明（黒）に近いなら描画を省略
      if (factor1 < 0.05f && factor2 < 0.05f) continue;

      // 頭から尻尾にかけて滑らかに消えていく
      uint16_t color = fadeColor(baseR, baseG, baseB, factor1);
      
      // 尻尾の太さも滑らかに細くする
      float rad1 = wid * factor1;
      float rad2 = wid * factor2;
      
      if (rad1 > 0.2f || rad2 > 0.2f) {
        img.drawWedgeLine(hx1, hy1, hx2, hy2, rad1, rad2, color);
      }
    }
    
    // 頭部（とんがった形をやめて、丸く可愛らしい形に変更）
    uint16_t headColor = e.flash > 0 ? TFT_WHITE : myColor(baseR, baseG, baseB);
    if (e.flash > 0) e.flash -= 0.1f;
    
    img.fillSmoothCircle(e.x, e.y, wid, headColor);
  };

  for(int i=0; i<MAX_DECOMPS; i++) drawEntity(decomps[i], 150, 255, 50, 0, 150, 0, 4.0f, 2.5f, false); // 分解者: ライム -> ディープグリーン
  for(int i=0; i<MAX_HERBS; i++) drawEntity(herbs[i], 0, 255, 255, 50, 255, 50, 6.0f, 3.5f, herbs[i].infected); // 草食: シアン -> エメラルドグリーン
  for(int i=0; i<MAX_CARNS; i++) drawEntity(carns[i], 255, 50, 150, 255, 0, 0, 8.0f, 4.5f, false); // 肉食: ピンク -> ブラッドレッド（純赤）
  for(int i=0; i<MAX_APEX; i++) drawEntity(apex[i], 255, 215, 0, 255, 255, 200, 10.0f, 5.5f, false); // 頂点: ゴールド -> プラチナ（白金）

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
      uint16_t gc = fadeColor(garbages[g].r, garbages[g].g, garbages[g].b, 0.7f); // 少し暗め
      img.drawLine(garbages[g].x-2, garbages[g].y-2, garbages[g].x+2, garbages[g].y+2, gc);
      img.drawLine(garbages[g].x+2, garbages[g].y-2, garbages[g].x-2, garbages[g].y+2, gc);
    }
  }
  
  img.pushSprite(0, 0);
  
  vTaskDelay(1 / portTICK_PERIOD_MS);
}
