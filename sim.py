import math
import random
import matplotlib.pyplot as plt

WIDTH = 320
HEIGHT = 170

MAX_PLANTS = 50
MAX_HERBS = 40
MAX_CARNS = 8
MAX_APEX = 3
MAX_SPORES = 15
MAX_DECOMPS = 10
MAX_GARBAGES = 30

# Parameters to tune
P_PLANT_SPAWN = 0.035
HERB_DRAIN = 0.04
HERB_REP_THRESH = 120.0
HERB_REP_COST = 50.0
HERB_EAT_GAIN = 30.0

CARN_DRAIN = 0.06
CARN_REP_THRESH = 150.0
CARN_REP_COST = 70.0
CARN_EAT_GAIN = 60.0

APEX_DRAIN = 0.15
APEX_REP_THRESH = 500.0
APEX_REP_COST = 200.0
APEX_EAT_GAIN = 120.0

class Entity:
    def __init__(self, x, y, energy, infected=False):
        self.x = x
        self.y = y
        self.vx = random.uniform(-1, 1)
        self.vy = random.uniform(-1, 1)
        self.energy = energy
        self.infected = infected

class Plant:
    def __init__(self, x, y):
        self.x = x
        self.y = y

class Spore:
    def __init__(self, x, y):
        self.x = x
        self.y = y
        self.vx = random.uniform(-0.1, 0.9)
        self.vy = random.uniform(-0.5, 0.5)

class Garbage:
    def __init__(self, x, y):
        self.x = x + random.uniform(-3, 3)
        self.y = y + random.uniform(-3, 3)

plants = []
herbs = []
carns = []
apexs = []
spores = []
decomps = []
garbages = []

def dist2(a, b):
    return (a.x - b.x)**2 + (a.y - b.y)**2

def spawn_garbage(x, y):
    global garbages
    garbages.append(Garbage(x, y))
    if len(garbages) > MAX_GARBAGES:
        garbages.pop(0)

def sim_step():
    global plants, herbs, carns, apexs, spores, decomps, garbages
    
    if random.random() < P_PLANT_SPAWN and len(plants) < MAX_PLANTS:
        plants.append(Plant(random.uniform(5, WIDTH-5), random.uniform(5, HEIGHT-5)))
        
    if random.random() < 0.001 and len(spores) < MAX_SPORES:
        spores.append(Spore(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10)))

    if len(herbs) < 5 and random.random() < 0.01:
        herbs.append(Entity(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10), 80))
    if len(carns) < 2 and len(herbs) > 20 and random.random() < 0.01:
        carns.append(Entity(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10), 100))
    if len(apexs) < 1 and len(carns) > 6 and random.random() < 0.01:
        apexs.append(Entity(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10), 300))
    if len(decomps) < 3 and random.random() < 0.01:
        decomps.append(Entity(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10), 80))

    # Decomposers
    new_decomps = []
    for d in decomps:
        min_d = 99999
        target_g = None
        target_s = None
        
        for g in garbages:
            d_sq = dist2(d, g)
            if d_sq < min_d:
                min_d = d_sq
                target_g = g
                target_s = None
                
        for s in spores:
            d_sq = dist2(d, s)
            if d_sq < min_d:
                min_d = d_sq
                target_s = s
                target_g = None
                
        if target_g:
            dx = target_g.x - d.x
            dy = target_g.y - d.y
            mag = math.sqrt(dist2(d, target_g))
            if mag > 0:
                d.vx = d.vx * 0.95 + (dx/mag)*0.1
                d.vy = d.vy * 0.95 + (dy/mag)*0.1
            if min_d < 36:
                if target_g in garbages: garbages.remove(target_g)
                d.energy += 20
        elif target_s:
            dx = target_s.x - d.x
            dy = target_s.y - d.y
            mag = math.sqrt(dist2(d, target_s))
            if mag > 0:
                d.vx = d.vx * 0.95 + (dx/mag)*0.1
                d.vy = d.vy * 0.95 + (dy/mag)*0.1
            if min_d < 36:
                if target_s in spores: spores.remove(target_s)
                d.energy += 20
        else:
            d.vx += random.uniform(-0.1, 0.1)
            d.vy += random.uniform(-0.1, 0.1)
            
        speed = math.sqrt(d.vx**2 + d.vy**2)
        if speed > 0.7:
            d.vx = (d.vx/speed)*0.7
            d.vy = (d.vy/speed)*0.7
            
        d.x += d.vx
        d.y += d.vy
        if d.x < 0: d.x=0; d.vx*=-1
        if d.x > WIDTH: d.x=WIDTH; d.vx*=-1
        if d.y < 0: d.y=0; d.vy*=-1
        if d.y > HEIGHT: d.y=HEIGHT; d.vy*=-1
        
        d.energy -= 0.05
        if d.energy > 0:
            if d.energy > 120:
                d.energy -= 80
                if len(plants) < MAX_PLANTS:
                    plants.append(Plant(d.x, d.y))
            new_decomps.append(d)
        else:
            spawn_garbage(d.x, d.y)
    decomps = new_decomps

    # Spores update
    for s in spores[:]:
        s.x += s.vx
        s.y += s.vy
        if s.x > WIDTH: s.x = 0
        elif s.x < 0: s.x = WIDTH
        if s.y > HEIGHT: s.y = 0
        elif s.y < 0: s.y = HEIGHT
        
        for h in herbs:
            if not h.infected and dist2(h, s) < 36:
                h.infected = True
                if s in spores: spores.remove(s)
                break

    # Herbs update
    new_herbs = []
    for h in herbs:
        nearest_p = None
        min_d = 99999
        for p in plants:
            d_sq = dist2(h, p)
            if d_sq < min_d:
                min_d = d_sq
                nearest_p = p
        
        if nearest_p:
            dx = nearest_p.x - h.x
            dy = nearest_p.y - h.y
            mag = math.sqrt(dist2(h, nearest_p))
            if mag > 0 and not h.infected:
                h.vx = h.vx * 0.97 + (dx/mag)*0.05
                h.vy = h.vy * 0.97 + (dy/mag)*0.05
            if min_d < 25:
                if nearest_p in plants: plants.remove(nearest_p)
                h.energy += HERB_EAT_GAIN
        else:
            h.vx += random.uniform(-0.1, 0.1)
            h.vy += random.uniform(-0.1, 0.1)
            
        for c in carns:
            dx = h.x - c.x
            dy = h.y - c.y
            d_sq = dx*dx + dy*dy
            if d_sq < 4000:
                mag = math.sqrt(d_sq)
                if mag > 0:
                    h.vx += (dx/mag)*0.1
                    h.vy += (dy/mag)*0.1
                    
        for a in apexs:
            dx = h.x - a.x
            dy = h.y - a.y
            d_sq = dx*dx + dy*dy
            if d_sq < 6000:
                mag = math.sqrt(d_sq)
                if mag > 0:
                    h.vx += (dx/mag)*0.12
                    h.vy += (dy/mag)*0.12
                    
        if h.infected:
            h.vx += random.uniform(-0.5, 0.5)
            h.vy += random.uniform(-0.5, 0.5)
            h.energy -= 0.15
            
        speed = math.sqrt(h.vx**2 + h.vy**2)
        limit = 1.2 if h.infected else 0.8
        if speed > limit:
            h.vx = (h.vx/speed)*limit
            h.vy = (h.vy/speed)*limit
            
        h.x += h.vx
        h.y += h.vy
        if h.x < 0: h.x=0; h.vx*=-1
        if h.x > WIDTH: h.x=WIDTH; h.vx*=-1
        if h.y < 0: h.y=0; h.vy*=-1
        if h.y > HEIGHT: h.y=HEIGHT; h.vy*=-1
        
        h.energy -= HERB_DRAIN
        
        if h.energy > 0:
            if h.energy > HERB_REP_THRESH and not h.infected and len(herbs)+len(new_herbs) < MAX_HERBS:
                h.energy -= HERB_REP_COST
                new_herbs.append(Entity(h.x, h.y, 80))
            new_herbs.append(h)
        else:
            spawn_garbage(h.x, h.y)
            if h.infected and len(spores) < MAX_SPORES - 1:
                spores.append(Spore(h.x, h.y))
                spores.append(Spore(h.x, h.y))
                
    herbs = new_herbs

    # Carns update
    new_carns = []
    for c in carns:
        nearest_h = None
        min_d = 99999
        for h in herbs:
            d_sq = dist2(c, h)
            if d_sq < min_d:
                min_d = d_sq
                nearest_h = h
                
        if nearest_h:
            dx = nearest_h.x - c.x
            dy = nearest_h.y - c.y
            mag = math.sqrt(dist2(c, nearest_h))
            if mag > 0:
                c.vx = c.vx * 0.96 + (dx/mag)*0.08
                c.vy = c.vy * 0.96 + (dy/mag)*0.08
            if min_d < 36:
                nearest_h.energy -= 2.5
                c.energy += 2.5
                c.vx *= 0.5
                c.vy *= 0.5
                nearest_h.vx *= 0.2
                nearest_h.vy *= 0.2
        else:
            c.vx += random.uniform(-0.1, 0.1)
            c.vy += random.uniform(-0.1, 0.1)
            
        escaping = False
        for a in apexs:
            dx = c.x - a.x
            dy = c.y - a.y
            d_sq = dx*dx + dy*dy
            if d_sq < 8000:
                escaping = True
                mag = math.sqrt(d_sq)
                if mag > 0:
                    c.vx += (dx/mag)*0.3
                    c.vy += (dy/mag)*0.3
                    
        speed = math.sqrt(c.vx**2 + c.vy**2)
        limit = 1.7 if escaping else 1.1
        if speed > limit:
            c.vx = (c.vx/speed)*limit
            c.vy = (c.vy/speed)*limit
            
        c.x += c.vx
        c.y += c.vy
        if c.x < 0: c.x=0; c.vx*=-1
        if c.x > WIDTH: c.x=WIDTH; c.vx*=-1
        if c.y < 0: c.y=0; c.vy*=-1
        if c.y > HEIGHT: c.y=HEIGHT; c.vy*=-1
        
        c.energy -= CARN_DRAIN
        
        if c.energy > 0:
            if c.energy > CARN_REP_THRESH and len(carns)+len(new_carns) < MAX_CARNS:
                c.energy -= CARN_REP_COST
                new_carns.append(Entity(c.x, c.y, 100))
            new_carns.append(c)
        else:
            spawn_garbage(c.x, c.y)
    carns = new_carns
    
    # Apex update
    new_apexs = []
    for a in apexs:
        nearest_c = None
        min_d = 40000
        for c in carns:
            d_sq = dist2(a, c)
            if d_sq < min_d:
                min_d = d_sq
                nearest_c = c
                
        if nearest_c:
            dx = nearest_c.x - a.x
            dy = nearest_c.y - a.y
            mag = math.sqrt(dist2(a, nearest_c))
            if mag > 0:
                a.vx = a.vx * 0.98 + (dx/mag)*0.12
                a.vy = a.vy * 0.98 + (dy/mag)*0.12
            if min_d < 64:
                nearest_c.energy -= 5.0
                a.energy += 5.0
                a.vx *= 0.6
                a.vy *= 0.6
                nearest_c.vx *= 0.1
                nearest_c.vy *= 0.1
        else:
            a.vx += random.uniform(-0.1, 0.1)
            a.vy += random.uniform(-0.1, 0.1)
            
        speed = math.sqrt(a.vx**2 + a.vy**2)
        if speed > 1.5:
            a.vx = (a.vx/speed)*1.5
            a.vy = (a.vy/speed)*1.5
            
        a.x += a.vx
        a.y += a.vy
        if a.x < 0: a.x=0; a.vx*=-1
        if a.x > WIDTH: a.x=WIDTH; a.vx*=-1
        if a.y < 0: a.y=0; a.vy*=-1
        if a.y > HEIGHT: a.y=HEIGHT; a.vy*=-1
        
        a.energy -= APEX_DRAIN
        
        if a.energy > 0:
            if a.energy > APEX_REP_THRESH and len(apexs)+len(new_apexs) < MAX_APEX:
                a.energy -= APEX_REP_COST
                new_apexs.append(Entity(a.x, a.y, 300))
            new_apexs.append(a)
        else:
            spawn_garbage(a.x, a.y)
    apexs = new_apexs

def run_sim():
    for _ in range(40): plants.append(Plant(random.uniform(5, WIDTH-5), random.uniform(5, HEIGHT-5)))
    for _ in range(25): herbs.append(Entity(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10), 80))
    for _ in range(3): carns.append(Entity(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10), 100))
    for _ in range(3): decomps.append(Entity(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10), 80))
    
    hist_plants = []
    hist_herbs = []
    hist_carns = []
    hist_apexs = []
    hist_spores = []
    hist_decomps = []
    hist_garbages = []
    
    steps = 30000
    for step in range(steps):
        sim_step()
        if step % 50 == 0:  # Sample every 50 frames to keep plot clean
            hist_plants.append(len(plants))
            hist_herbs.append(len(herbs))
            hist_carns.append(len(carns))
            hist_apexs.append(len(apexs))
            hist_spores.append(len(spores))
            hist_decomps.append(len(decomps))
            hist_garbages.append(len(garbages))
            
    print(f"Stats over {steps} ticks:")
    print(f"Plants: {sum(hist_plants)/len(hist_plants):.1f} / {MAX_PLANTS}")
    print(f"Herbs:  {sum(hist_herbs)/len(hist_herbs):.1f} / {MAX_HERBS}")
    print(f"Carns:  {sum(hist_carns)/len(hist_carns):.1f} / {MAX_CARNS}")
    print(f"Apexs:  {sum(hist_apexs)/len(hist_apexs):.1f} / {MAX_APEX}")
    print(f"Decomp: {sum(hist_decomps)/len(hist_decomps):.1f} / {MAX_DECOMPS}")
    print(f"Garbag: {sum(hist_garbages)/len(hist_garbages):.1f} / {MAX_GARBAGES}")

    # Plot
    plt.figure(figsize=(12, 6))
    plt.plot(hist_plants, label='Plants', color='green')
    plt.plot(hist_herbs, label='Herbivores', color='cyan')
    plt.plot(hist_carns, label='Carnivores', color='hotpink')
    plt.plot(hist_apexs, label='Apex', color='gold')
    plt.plot(hist_spores, label='Spores', color='purple', linestyle='--')
    plt.plot(hist_decomps, label='Decomposers', color='lime')
    plt.plot(hist_garbages, label='Garbage', color='gray', linestyle=':')
    
    plt.title('Ecosystem Population Dynamics (30,000 steps)')
    plt.xlabel('Time (x50 steps)')
    plt.ylabel('Population Count')
    plt.legend(loc='upper right')
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig('ecosystem_plot.png')

if __name__ == '__main__':
    run_sim()
