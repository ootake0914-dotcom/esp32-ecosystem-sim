import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation, PillowWriter
import matplotlib.collections as mcoll
import numpy as np
import sim
import random

# For effects
particles = []

def spawn_explosion(x, y, color):
    for _ in range(12):
        vx = random.uniform(-3, 3)
        vy = random.uniform(-3, 3)
        particles.append({'x': x, 'y': y, 'vx': vx, 'vy': vy, 'life': 1.0, 'color': color})

def create_animation():
    fig, ax = plt.subplots(figsize=(8, 4.25))
    ax.set_xlim(0, sim.WIDTH)
    ax.set_ylim(0, sim.HEIGHT)
    ax.set_facecolor('black')
    ax.set_title("Ecosystem Simulation")
    
    # Pre-populate
    for _ in range(40): sim.plants.append(sim.Plant(random.uniform(5, sim.WIDTH-5), random.uniform(5, sim.HEIGHT-5)))
    for _ in range(25): sim.herbs.append(sim.Entity(random.uniform(10, sim.WIDTH-10), random.uniform(10, sim.HEIGHT-10), 80))
    for _ in range(3): sim.carns.append(sim.Entity(random.uniform(10, sim.WIDTH-10), random.uniform(10, sim.HEIGHT-10), 100))
    for _ in range(3): sim.decomps.append(sim.Entity(random.uniform(10, sim.WIDTH-10), random.uniform(10, sim.HEIGHT-10), 80))

    scat_plants = ax.scatter([], [], c='green', s=10, marker='o')
    scat_spores = ax.scatter([], [], c='purple', s=10, marker='x')
    scat_garbages = ax.scatter([], [], c='gray', s=20, marker='x')
    scat_particles = ax.scatter([], [], c=[], s=2, marker='o')

    # Trails using LineCollection
    trail_collection = mcoll.LineCollection([], colors=[], linewidths=[])
    ax.add_collection(trail_collection)
    
    # Store history inside entities
    for lst in [sim.herbs, sim.carns, sim.apexs, sim.decomps]:
        for e in lst:
            e.hist = []
            e.color = 'cyan' if lst is sim.herbs else 'hotpink' if lst is sim.carns else 'gold' if lst is sim.apexs else 'lime'

    def update(frame):
        # We need to detect deaths to spawn explosions.
        old_herbs = set(sim.herbs)
        old_carns = set(sim.carns)
        
        for _ in range(2): # 2 steps per frame
            sim.sim_step()
            
            # Ensure history array exists for new entities
            for lst, col in [(sim.herbs, 'cyan'), (sim.carns, 'hotpink'), (sim.apexs, 'gold'), (sim.decomps, 'lime')]:
                for e in lst:
                    if not hasattr(e, 'hist'):
                        e.hist = []
                        e.color = col
                    e.hist.append((e.x, e.y))
                    if len(e.hist) > 15: # Tail length
                        e.hist.pop(0)

        # Detect deaths (for explosions)
        dead_herbs = old_herbs - set(sim.herbs)
        for h in dead_herbs:
            spawn_explosion(h.x, h.y, 'cyan')
        dead_carns = old_carns - set(sim.carns)
        for c in dead_carns:
            spawn_explosion(c.x, c.y, 'hotpink')

        # Update particles
        p_offsets = []
        p_colors = []
        for p in particles[:]:
            p['x'] += p['vx']
            p['y'] += p['vy']
            p['life'] -= 0.1
            if p['life'] <= 0:
                particles.remove(p)
            else:
                p_offsets.append([p['x'], p['y']])
                p_colors.append(p['color'])
                
        if p_offsets:
            scat_particles.set_offsets(p_offsets)
            scat_particles.set_color(p_colors)
            # Cannot easily set per-point alpha in old matplotlib, so we rely on color fading if possible, or just skip it.
            # We'll just draw them solid since their life makes them disappear.
        else:
            scat_particles.set_offsets(np.empty((0,2)))

        # Update lines (trails)
        segments = []
        colors = []
        linewidths = []
        
        for lst in [sim.herbs, sim.carns, sim.apexs, sim.decomps]:
            for e in lst:
                pts = e.hist
                if len(pts) > 1:
                    # Draw wedges: segment by segment, getting thicker
                    for i in range(len(pts)-1):
                        segments.append([pts[i], pts[i+1]])
                        colors.append(e.color)
                        linewidths.append((i / len(pts)) * 3)

        trail_collection.set_segments(segments)
        trail_collection.set_color(colors)
        trail_collection.set_linewidths(linewidths)
        trail_collection.set_alpha(0.6)

        # Draw current head positions as scatters (implied by the thickest part of the tail, but let's just use tails for bodies like ESP32)
        
        # Update scatters for static/simple entities
        scat_plants.set_offsets([[p.x, p.y] for p in sim.plants] if sim.plants else np.empty((0,2)))
        scat_spores.set_offsets([[s.x, s.y] for s in sim.spores] if sim.spores else np.empty((0,2)))
        scat_garbages.set_offsets([[g.x, g.y] for g in sim.garbages] if sim.garbages else np.empty((0,2)))
            
        return scat_plants, scat_spores, scat_garbages, scat_particles, trail_collection

    print("Generating animation with effects...")
    anim = FuncAnimation(fig, update, frames=300, blit=False)
    
    writer = PillowWriter(fps=30)
    anim.save("ecosystem_anim.gif", writer=writer)
    print("Animation saved to ecosystem_anim.gif")

if __name__ == '__main__':
    create_animation()
