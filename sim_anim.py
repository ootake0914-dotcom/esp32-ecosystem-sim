import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation, PillowWriter
import sim
import random

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
    scat_herbs = ax.scatter([], [], c='cyan', s=15, marker='o')
    scat_carns = ax.scatter([], [], c='hotpink', s=20, marker='^')
    scat_apexs = ax.scatter([], [], c='gold', s=40, marker='*')
    scat_spores = ax.scatter([], [], c='purple', s=10, marker='x')
    scat_decomps = ax.scatter([], [], c='lime', s=15, marker='s')
    scat_garbages = ax.scatter([], [], c='gray', s=20, marker='x')

    def init():
        return scat_plants, scat_herbs, scat_carns, scat_apexs, scat_spores, scat_decomps, scat_garbages

    def update(frame):
        for _ in range(2): # 2 steps per frame
            sim.sim_step()
            
        # Update scatters
        if sim.plants:
            scat_plants.set_offsets([[p.x, p.y] for p in sim.plants])
        else:
            scat_plants.set_offsets(empty_offsets())
            
        if sim.herbs:
            scat_herbs.set_offsets([[h.x, h.y] for h in sim.herbs])
        else:
            scat_herbs.set_offsets(empty_offsets())
            
        if sim.carns:
            scat_carns.set_offsets([[c.x, c.y] for c in sim.carns])
        else:
            scat_carns.set_offsets(empty_offsets())
            
        if sim.apexs:
            scat_apexs.set_offsets([[a.x, a.y] for a in sim.apexs])
        else:
            scat_apexs.set_offsets(empty_offsets())
            
        if sim.spores:
            scat_spores.set_offsets([[s.x, s.y] for s in sim.spores])
        else:
            scat_spores.set_offsets(empty_offsets())
            
        if sim.decomps:
            scat_decomps.set_offsets([[d.x, d.y] for d in sim.decomps])
        else:
            scat_decomps.set_offsets(empty_offsets())
            
        if sim.garbages:
            scat_garbages.set_offsets([[g.x, g.y] for g in sim.garbages])
        else:
            scat_garbages.set_offsets(empty_offsets())
            
        return scat_plants, scat_herbs, scat_carns, scat_apexs, scat_spores, scat_decomps, scat_garbages

    def empty_offsets():
        # returns an empty 2D array for scatter offsets
        import numpy as np
        return np.empty((0, 2))

    print("Generating animation...")
    anim = FuncAnimation(fig, update, frames=300, init_func=init, blit=True)
    
    writer = PillowWriter(fps=30)
    anim.save("ecosystem_anim.gif", writer=writer)
    print("Animation saved to ecosystem_anim.gif")

if __name__ == '__main__':
    create_animation()
