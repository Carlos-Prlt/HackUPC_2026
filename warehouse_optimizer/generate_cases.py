import os
import random
import math

def write_csv(path, rows):
    with open(path, 'w') as f:
        for row in rows:
            f.write(", ".join(map(str, row)) + "\n")

def make_case_dir(case_id):
    path = f"data/Case{case_id}"
    os.makedirs(path, exist_ok=True)
    return path

# Case 4: Extreme (huge perimeter, many obstacles, 20 bay types)
def gen_case4():
    p = make_case_dir(4)
    # Huge L-shape
    warehouse = [
        [0, 0], [40000, 0], [40000, 10000], [15000, 10000], [15000, 30000], [0, 30000]
    ]
    write_csv(f"{p}/warehouse.csv", warehouse)
    
    obstacles = []
    # scattered obstacles
    random.seed(42)
    for _ in range(50):
        x = random.randint(1000, 14000)
        y = random.randint(1000, 28000)
        obstacles.append([x, y, random.randint(500, 1500), random.randint(500, 1500)])
    write_csv(f"{p}/obstacles.csv", obstacles)
    
    ceiling = [[0, 10000], [20000, 8000], [40000, 10000]]
    write_csv(f"{p}/ceiling.csv", ceiling)
    
    bays = []
    for i in range(20):
        w = random.randint(800, 4000)
        d = random.randint(800, 2000)
        h = random.randint(2000, 6000)
        gap = random.randint(100, 500)
        loads = random.randint(1, 20)
        price = random.randint(500, 5000)
        bays.append([i, w, d, h, gap, loads, price])
    write_csv(f"{p}/types_of_bays.csv", bays)

# Case 5: Diagonal (Diamond shape)
def gen_case5():
    p = make_case_dir(5)
    warehouse = [
        [10000, 0], [20000, 10000], [10000, 20000], [0, 10000]
    ]
    write_csv(f"{p}/warehouse.csv", warehouse)
    write_csv(f"{p}/obstacles.csv", []) # no obstacles
    write_csv(f"{p}/ceiling.csv", [[0, 5000], [20000, 5000]])
    
    bays = []
    for i in range(10):
        bays.append([i, 1000 + i*200, 1000, 3000, 200, 2+i, 1000 + i*100])
    write_csv(f"{p}/types_of_bays.csv", bays)

# Case 6: Difficult (Tight ceiling, many small obstacles forming walls)
def gen_case6():
    p = make_case_dir(6)
    warehouse = [[0, 0], [20000, 0], [20000, 20000], [0, 20000]]
    write_csv(f"{p}/warehouse.csv", warehouse)
    
    obstacles = []
    for i in range(1, 10):
        obstacles.append([i*2000, 0, 500, 15000]) # vertical walls
    write_csv(f"{p}/obstacles.csv", obstacles)
    
    ceiling = [[0, 2500], [10000, 2000], [20000, 2500]]
    write_csv(f"{p}/ceiling.csv", ceiling)
    
    bays = []
    for i in range(10):
        bays.append([i, 1500 + i*100, 800, 1800 + i*100, 100, 1+i, 500 + i*200])
    write_csv(f"{p}/types_of_bays.csv", bays)

# Cases 7 to 10: Normal variants
def gen_normal_case(case_id, width, height, num_obs, num_bays):
    p = make_case_dir(case_id)
    warehouse = [[0, 0], [width, 0], [width, height], [0, height]]
    write_csv(f"{p}/warehouse.csv", warehouse)
    
    obstacles = []
    random.seed(case_id)
    for _ in range(num_obs):
        x = random.randint(1000, width - 2000)
        y = random.randint(1000, height - 2000)
        obstacles.append([x, y, random.randint(500, 2000), random.randint(500, 2000)])
    write_csv(f"{p}/obstacles.csv", obstacles)
    
    ceiling = [[0, 4000], [width//2, 3500], [width, 4000]]
    write_csv(f"{p}/ceiling.csv", ceiling)
    
    bays = []
    for i in range(num_bays):
        bays.append([i, 1200 + i*300, 1000, 2800, 200, 2+i, 1500 + i*250])
    write_csv(f"{p}/types_of_bays.csv", bays)

gen_case4()
gen_case5()
gen_case6()
gen_normal_case(7, 15000, 15000, 2, 8)
gen_normal_case(8, 25000, 12000, 5, 12)
gen_normal_case(9, 10000, 30000, 3, 10)
gen_normal_case(10, 20000, 20000, 8, 15)

print("Generated all cases successfully.")
