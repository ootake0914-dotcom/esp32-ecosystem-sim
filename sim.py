import math
import random
import matplotlib.pyplot as plt

WIDTH = 320.0
HEIGHT = 170.0

MAX_PLANTS = 50
MAX_HERBS = 40
MAX_CARNS = 8
MAX_APEX = 3
MAX_SPORES = 15
MAX_DECOMPS = 10
MAX_GARBAGES = 30

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

def dist2(a, b):
    return (a.x - b.x)**2 + (a.y - b.y)**2

class BaseEntity:
    def __init__(self, x, y):
        self.x = x
        self.y = y
        self.alive = True

class Plant(BaseEntity):
    pass

class Garbage(BaseEntity):
    def __init__(self, x, y):
        super().__init__(x + random.uniform(-3, 3), y + random.uniform(-3, 3))

class MovingEntity(BaseEntity):
    def __init__(self, x, y, energy, speed_limit):
        super().__init__(x, y)
        self.vx = random.uniform(-1, 1)
        self.vy = random.uniform(-1, 1)
        self.energy = energy
        self.speed_limit = speed_limit

    def apply_boundary(self, w, h):
        if self.x < 0: self.x = 0; self.vx *= -1
        elif self.x > w: self.x = w; self.vx *= -1
        if self.y < 0: self.y = 0; self.vy *= -1
        elif self.y > h: self.y = h; self.vy *= -1

class Spore(MovingEntity):
    def __init__(self, x, y):
        super().__init__(x, y, 0, 0)
        self.vx = random.uniform(-0.1, 0.9)
        self.vy = random.uniform(-0.5, 0.5)
        
    def apply_boundary(self, w, h):
        self.x %= w
        self.y %= h

class Herbivore(MovingEntity):
    def __init__(self, x, y, energy, speed_limit=-1.0, infected=False, altruism=-1.0, immunity=-1.0):
        super().__init__(x, y, energy, 0.8 if speed_limit == -1.0 else max(0.3, min(2.0, speed_limit + random.uniform(-0.1, 0.1))))
        self.infected = infected
        self.altruism = random.random() if altruism == -1.0 else max(0.0, min(1.0, altruism + random.uniform(-0.1, 0.1)))
        self.immunity = random.random() if immunity == -1.0 else max(0.0, min(1.0, immunity + random.uniform(-0.1, 0.1)))

class Carnivore(MovingEntity):
    def __init__(self, x, y, energy, speed_limit=-1.0):
        super().__init__(x, y, energy, 1.1 if speed_limit == -1.0 else max(0.5, min(2.5, speed_limit + random.uniform(-0.1, 0.1))))

class ApexPredator(MovingEntity):
    def __init__(self, x, y, energy, speed_limit=-1.0):
        super().__init__(x, y, energy, 1.5 if speed_limit == -1.0 else max(0.8, min(3.0, speed_limit + random.uniform(-0.1, 0.1))))

class Decomposer(MovingEntity):
    def __init__(self, x, y, energy):
        super().__init__(x, y, energy, 0.7)

class SpatialHashGrid:
    def __init__(self, width, height, cell_size=20.0):
        self.cell_size = cell_size
        self.cols = int(math.ceil(width / cell_size))
        self.rows = int(math.ceil(height / cell_size))
        self.grid = {}

    def clear(self):
        self.grid.clear()

    def insert(self, entity):
        c = max(0, min(self.cols - 1, int(entity.x // self.cell_size)))
        r = max(0, min(self.rows - 1, int(entity.y // self.cell_size)))
        if (c, r) not in self.grid:
            self.grid[(c, r)] = []
        self.grid[(c, r)].append(entity)

    def get_nearby(self, x, y, radius):
        min_c = max(0, min(self.cols - 1, int((x - radius) // self.cell_size)))
        max_c = max(0, min(self.cols - 1, int((x + radius) // self.cell_size)))
        min_r = max(0, min(self.rows - 1, int((y - radius) // self.cell_size)))
        max_r = max(0, min(self.rows - 1, int((y + radius) // self.cell_size)))
        res = []
        for c in range(min_c, max_c + 1):
            for r in range(min_r, max_r + 1):
                if (c, r) in self.grid:
                    res.extend(self.grid[(c, r)])
        return res

class World:
    def __init__(self):
        self.plants = [Plant(random.uniform(5, WIDTH-5), random.uniform(5, HEIGHT-5)) for _ in range(40)]
        self.herbs = [Herbivore(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10), 80) for _ in range(25)]
        self.carns = [Carnivore(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10), 100) for _ in range(3)]
        self.apexs = []
        self.spores = []
        self.decomps = [Decomposer(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10), 80) for _ in range(3)]
        self.garbages = []
        
        self.grid = SpatialHashGrid(WIDTH, HEIGHT, 20.0)

    def spawn_garbage(self, x, y):
        self.garbages.append(Garbage(x, y))
        if len(self.garbages) > MAX_GARBAGES:
            self.garbages.pop(0)

    def step(self):
        if random.random() < P_PLANT_SPAWN and len(self.plants) < MAX_PLANTS:
            self.plants.append(Plant(random.uniform(5, WIDTH-5), random.uniform(5, HEIGHT-5)))
            
        if random.random() < 0.001 and len(self.spores) < MAX_SPORES:
            self.spores.append(Spore(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10)))

        if len(self.herbs) < 5 and random.random() < 0.01:
            self.herbs.append(Herbivore(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10), 80))
        if len(self.carns) < 2 and len(self.herbs) > 20 and random.random() < 0.01:
            self.carns.append(Carnivore(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10), 100))
        if len(self.apexs) < 1 and len(self.carns) > 6 and random.random() < 0.01:
            self.apexs.append(ApexPredator(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10), 300))
        if len(self.decomps) < 3 and random.random() < 0.01:
            self.decomps.append(Decomposer(random.uniform(10, WIDTH-10), random.uniform(10, HEIGHT-10), 80))

        self.grid.clear()
        for p in self.plants: self.grid.insert(p)
        for h in self.herbs: self.grid.insert(h)
        for c in self.carns: self.grid.insert(c)
        for a in self.apexs: self.grid.insert(a)
        for s in self.spores: self.grid.insert(s)
        for d in self.decomps: self.grid.insert(d)
        for g in self.garbages: self.grid.insert(g)

        # Decomposers
        for d in self.decomps:
            if not d.alive: continue
            min_d = 99999
            target = None
            
            nearby = self.grid.get_nearby(d.x, d.y, 40.0)
            for item in nearby:
                if not item.alive: continue
                if isinstance(item, (Garbage, Spore)):
                    d_sq = dist2(d, item)
                    if d_sq < min_d:
                        min_d = d_sq
                        target = item
                    
            if target:
                dx = target.x - d.x
                dy = target.y - d.y
                mag = math.sqrt(min_d)
                if mag > 0:
                    d.vx = d.vx * 0.95 + (dx/mag)*0.1
                    d.vy = d.vy * 0.95 + (dy/mag)*0.1
                if min_d < 36:
                    target.alive = False
                    d.energy += 20
            else:
                d.vx += random.uniform(-0.1, 0.1)
                d.vy += random.uniform(-0.1, 0.1)
                
            speed = math.sqrt(d.vx**2 + d.vy**2)
            if speed > 0.7:
                d.vx = (d.vx/speed)*0.7
                d.vy = (d.vy/speed)*0.7
                
            d.x += d.vx; d.y += d.vy
            d.apply_boundary(WIDTH, HEIGHT)
            
            d.energy -= 0.05
            if d.energy <= 0:
                d.alive = False
                self.spawn_garbage(d.x, d.y)
            elif d.energy > 120:
                d.energy -= 80
                if len(self.plants) < MAX_PLANTS:
                    self.plants.append(Plant(d.x, d.y))

        # Spores
        for s in self.spores:
            if not s.alive: continue
            s.x += s.vx; s.y += s.vy
            s.apply_boundary(WIDTH, HEIGHT)
            
            nearby = self.grid.get_nearby(s.x, s.y, 20.0)
            for h in nearby:
                if isinstance(h, Herbivore) and h.alive and not h.infected:
                    if dist2(h, s) < 36:
                        s.alive = False
                        if random.random() >= h.immunity:
                            h.infected = True
                        break

        # Herbs
        for h in self.herbs:
            if not h.alive: continue
            nearest_p = None
            min_d = 99999
            
            nearby = self.grid.get_nearby(h.x, h.y, 40.0)
            for p in nearby:
                if isinstance(p, Plant) and p.alive:
                    d_sq = dist2(h, p)
                    if d_sq < min_d:
                        min_d = d_sq
                        nearest_p = p
            
            if nearest_p:
                dx = nearest_p.x - h.x
                dy = nearest_p.y - h.y
                mag = math.sqrt(min_d)
                if mag > 0 and not h.infected:
                    h.vx = h.vx * 0.97 + (dx/mag)*0.05
                    h.vy = h.vy * 0.97 + (dy/mag)*0.05
                if min_d < 25:
                    nearest_p.alive = False
                    h.energy += HERB_EAT_GAIN
            else:
                h.vx += random.uniform(-0.1, 0.1)
                h.vy += random.uniform(-0.1, 0.1)
                
            # Altruism
            for other in nearby:
                if isinstance(other, Herbivore) and other.alive and h != other:
                    if dist2(h, other) < 400 and not h.infected and not other.infected:
                        if h.energy > 60 and other.energy < 30:
                            if random.random() < h.altruism:
                                h.energy -= 1.0
                                other.energy += 1.0
                
            for c in self.carns:
                if not c.alive: continue
                dx = h.x - c.x
                dy = h.y - c.y
                d_sq = dx*dx + dy*dy
                if d_sq < 4000:
                    mag = math.sqrt(d_sq)
                    if mag > 0:
                        h.vx += (dx/mag)*0.1
                        h.vy += (dy/mag)*0.1
                        
            for a in self.apexs:
                if not a.alive: continue
                dx = h.x - a.x
                dy = h.y - a.y
                d_sq = dx*dx + dy*dy
                if d_sq < 6000:
                    mag = math.sqrt(d_sq)
                    if mag > 0:
                        h.vx += (dx/mag)*0.12
                        h.vy += (dy/mag)*0.12
                        
            if h.infected:
                if h.altruism > 0.6:
                    edge_x = -1.0 if h.x < WIDTH/2 else 1.0
                    edge_y = -1.0 if h.y < HEIGHT/2 else 1.0
                    h.vx = h.vx * 0.9 + edge_x * 0.1
                    h.vy = h.vy * 0.9 + edge_y * 0.1
                else:
                    h.vx += random.uniform(-0.5, 0.5)
                    h.vy += random.uniform(-0.5, 0.5)
                h.energy -= 0.15
                
            speed = math.sqrt(h.vx**2 + h.vy**2)
            limit = h.speed_limit + 0.4 if h.infected else h.speed_limit
            if speed > limit:
                h.vx = (h.vx/speed)*limit
                h.vy = (h.vy/speed)*limit
                
            h.x += h.vx; h.y += h.vy
            h.apply_boundary(WIDTH, HEIGHT)
            
            h.energy -= (0.01 + 0.03 * h.speed_limit + 0.02 * h.immunity)
            
            if h.energy <= 0:
                h.alive = False
                self.spawn_garbage(h.x, h.y)
                if h.infected and len(self.spores) < MAX_SPORES - 1:
                    self.spores.append(Spore(h.x, h.y))
                    self.spores.append(Spore(h.x, h.y))
            elif h.energy > HERB_REP_THRESH and not h.infected and len(self.herbs) < MAX_HERBS:
                h.energy -= HERB_REP_COST
                self.herbs.append(Herbivore(h.x, h.y, 80, h.speed_limit, False, h.altruism, h.immunity))

        # Carnivores
        for c in self.carns:
            if not c.alive: continue
            nearest_h = None
            min_d = 10000
            
            for h in self.herbs:
                if not h.alive: continue
                d_sq = dist2(c, h)
                if d_sq < min_d:
                    min_d = d_sq
                    nearest_h = h
                    
            if nearest_h:
                dx = nearest_h.x - c.x
                dy = nearest_h.y - c.y
                mag = math.sqrt(min_d)
                if mag > 0:
                    c.vx = c.vx * 0.96 + (dx/mag)*0.08
                    c.vy = c.vy * 0.96 + (dy/mag)*0.08
                if min_d < 36:
                    nearest_h.energy -= 2.5
                    c.energy += 2.5
                    c.vx *= 0.5; c.vy *= 0.5
                    nearest_h.vx *= 0.2; nearest_h.vy *= 0.2
            else:
                c.vx += random.uniform(-0.1, 0.1)
                c.vy += random.uniform(-0.1, 0.1)
                
            escaping = False
            for a in self.apexs:
                if not a.alive: continue
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
            limit = c.speed_limit + 0.6 if escaping else c.speed_limit
            if speed > limit:
                c.vx = (c.vx/speed)*limit
                c.vy = (c.vy/speed)*limit
                
            c.x += c.vx; c.y += c.vy
            c.apply_boundary(WIDTH, HEIGHT)
            
            c.energy -= CARN_DRAIN * (c.speed_limit / 1.1)
            
            if c.energy <= 0:
                c.alive = False
                self.spawn_garbage(c.x, c.y)
            elif c.energy > CARN_REP_THRESH and len(self.carns) < MAX_CARNS:
                c.energy -= CARN_REP_COST
                self.carns.append(Carnivore(c.x, c.y, 100, c.speed_limit))

        # Apexs
        for a in self.apexs:
            if not a.alive: continue
            nearest_c = None
            min_d = 14400
            
            for c in self.carns:
                if not c.alive: continue
                d_sq = dist2(a, c)
                if d_sq < min_d:
                    min_d = d_sq
                    nearest_c = c
                    
            if nearest_c:
                dx = nearest_c.x - a.x
                dy = nearest_c.y - a.y
                mag = math.sqrt(min_d)
                if mag > 0:
                    a.vx = a.vx * 0.98 + (dx/mag)*0.12
                    a.vy = a.vy * 0.98 + (dy/mag)*0.12
                if min_d < 64:
                    nearest_c.energy -= 5.0
                    a.energy += 5.0
                    a.vx *= 0.6; a.vy *= 0.6
                    nearest_c.vx *= 0.1; nearest_c.vy *= 0.1
            else:
                a.vx += random.uniform(-0.1, 0.1)
                a.vy += random.uniform(-0.1, 0.1)
                
            speed = math.sqrt(a.vx**2 + a.vy**2)
            if speed > a.speed_limit:
                a.vx = (a.vx/speed)*a.speed_limit
                a.vy = (a.vy/speed)*a.speed_limit
                
            a.x += a.vx; a.y += a.vy
            a.apply_boundary(WIDTH, HEIGHT)
            
            a.energy -= APEX_DRAIN * (a.speed_limit / 1.5)
            
            if a.energy <= 0:
                a.alive = False
                self.spawn_garbage(a.x, a.y)
            elif a.energy > APEX_REP_THRESH and len(self.apexs) < MAX_APEX:
                a.energy -= APEX_REP_COST
                self.apexs.append(ApexPredator(a.x, a.y, 300, a.speed_limit))

        # Filter dead entities
        self.plants = [e for e in self.plants if e.alive]
        self.herbs = [e for e in self.herbs if e.alive]
        self.carns = [e for e in self.carns if e.alive]
        self.apexs = [e for e in self.apexs if e.alive]
        self.spores = [e for e in self.spores if e.alive]
        self.decomps = [e for e in self.decomps if e.alive]
        self.garbages = [e for e in self.garbages if e.alive]

def run_sim():
    world = World()
    
    hist_plants = []
    hist_herbs = []
    hist_carns = []
    hist_apexs = []
    hist_spores = []
    hist_decomps = []
    hist_garbages = []
    hist_altruism = []
    hist_immunity = []
    hist_speed = []
    
    steps = 30000
    for step in range(steps):
        world.step()
        if step % 50 == 0:
            hist_plants.append(len(world.plants))
            hist_herbs.append(len(world.herbs))
            hist_carns.append(len(world.carns))
            hist_apexs.append(len(world.apexs))
            hist_spores.append(len(world.spores))
            hist_decomps.append(len(world.decomps))
            hist_garbages.append(len(world.garbages))
            avg_alt = sum(h.altruism for h in world.herbs)/len(world.herbs) if world.herbs else 0
            hist_altruism.append(avg_alt)
            avg_imm = sum(h.immunity for h in world.herbs)/len(world.herbs) if world.herbs else 0
            hist_immunity.append(avg_imm)
            avg_spd = sum(h.speed_limit for h in world.herbs)/len(world.herbs) if world.herbs else 0
            hist_speed.append(avg_spd)
            
    print(f"Stats over {steps} ticks:")
    print(f"Plants: {sum(hist_plants)/len(hist_plants):.1f} / {MAX_PLANTS}")
    print(f"Herbs:  {sum(hist_herbs)/len(hist_herbs):.1f} / {MAX_HERBS}")
    print(f"Carns:  {sum(hist_carns)/len(hist_carns):.1f} / {MAX_CARNS}")
    print(f"Apexs:  {sum(hist_apexs)/len(hist_apexs):.1f} / {MAX_APEX}")
    print(f"Decomp: {sum(hist_decomps)/len(hist_decomps):.1f} / {MAX_DECOMPS}")
    print(f"Garbag: {sum(hist_garbages)/len(hist_garbages):.1f} / {MAX_GARBAGES}")
    if hist_altruism:
        print(f"Final Avg Altruism: {hist_altruism[-1]:.2f}")
        print(f"Final Avg Immunity: {hist_immunity[-1]:.2f}")
        print(f"Final Avg Speed: {hist_speed[-1]:.2f}")

    # Plot
    fig, ax1 = plt.subplots(figsize=(12, 6))
    ax1.plot(hist_plants, label='Plants', color='green')
    ax1.plot(hist_herbs, label='Herbivores', color='cyan')
    ax1.plot(hist_carns, label='Carnivores', color='hotpink')
    ax1.plot(hist_apexs, label='Apex', color='gold')
    ax1.plot(hist_spores, label='Spores', color='purple', linestyle='--')
    ax1.plot(hist_decomps, label='Decomposers', color='lime')
    ax1.plot(hist_garbages, label='Garbage', color='gray', linestyle=':')
    
    ax2 = ax1.twinx()
    ax2.plot(hist_altruism, label='Avg Altruism', color='blue', linestyle='-')
    ax2.plot(hist_immunity, label='Avg Immunity', color='darkblue', linestyle='-.')
    ax2.plot(hist_speed, label='Avg Speed', color='orange', linestyle='--')
    ax2.set_ylabel('Genetics (0.0 - 2.0)', color='blue')
    
    plt.title('Ecosystem & Genetic Evolution (Altruism, Immunity, Speed)')
    ax1.set_xlabel('Time (x50 steps)')
    ax1.set_ylabel('Population Count')
    ax1.legend(loc='upper left')
    ax2.legend(loc='upper right')
    ax1.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig('ecosystem_plot_evolution.png')

if __name__ == '__main__':
    run_sim()
