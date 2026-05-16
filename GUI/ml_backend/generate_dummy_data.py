import os
import random
import time
import csv

def generate_dummy_data(output_file, num_sequences=100):
    """
    Generates dummy marine sensor data in the new 14-value 11-step ramp format.
    Format: reading, time_ms, temp_c, depth_m, F1_415, F2_445, F3_480, F4_515, F5_555, F6_590, F7_630, F8_680, clear, NIR
    
    Args:
        output_file (str): Path to the output CSV file.
        num_sequences (int): Number of 11-row readings to generate.
    """
    data = []
    
    # Let's say 50 normal, ~17 Phyto, ~17 CDOM, ~16 Cyano (50 anomalous total)
    num_normal = 50
    num_phyto = 17
    num_cdom = 17
    num_cyano = 16
    
    sequence_types = (
        ['normal'] * num_normal + 
        ['phyto'] * num_phyto + 
        ['cdom'] * num_cdom + 
        ['cyano'] * num_cyano
    )
    random.shuffle(sequence_types)
    
    for reading_id, seq_type in enumerate(sequence_types, start=1):
        temp_c = round(random.uniform(-1.5, 2.0), 2)
        depth_m = round(random.uniform(0.1, 50.0), 3)
        base_time = (reading_id - 1) * 300000 # 5 minutes apart in ms
        
        # Determine the peak value for the excitation LED (F2_445)
        # It typically ramps up to around 4000-5000
        f2_max = random.uniform(3500, 4500)
        
        for step in range(11):
            time_ms = base_time + (step * 116)
            
            # Base ramp for excitation
            f2 = int((f2_max / 10) * step)
            
            # Default low values for other channels, scaling slightly with LED
            f1 = int(random.uniform(0.01, 0.05) * f2)
            f3 = int(random.uniform(0.01, 0.05) * f2)
            f4 = int(random.uniform(0.01, 0.03) * f2)
            f5 = int(random.uniform(0.01, 0.03) * f2)
            f6 = int(random.uniform(0.01, 0.02) * f2)
            f7 = int(random.uniform(0.01, 0.02) * f2)
            f8 = int(random.uniform(0.01, 0.02) * f2) # Default very low
            
            clear = int(f2 * 1.5)
            nir = int(random.uniform(0.01, 0.02) * f2)
            
            # Inject Anomalies based on XAI constraints (~0.09 ratio)
            if seq_type == 'phyto':
                # Phytoplankton (Chlorophyll-a) -> F8_680 reaches ~0.09 ratio
                f8 = int(random.uniform(0.085, 0.10) * f2) 
            elif seq_type == 'cdom':
                # Bacterial Decay (CDOM) -> F1_415 and F3_480 combined reach ~0.09
                f1 = int(random.uniform(0.04, 0.05) * f2)
                f3 = int(random.uniform(0.04, 0.05) * f2)
            elif seq_type == 'cyano':
                # Cyanobacteria (Phycoerythrin) -> F5_555 and F6_590 combined reach ~0.09
                f5 = int(random.uniform(0.04, 0.05) * f2)
                f6 = int(random.uniform(0.04, 0.05) * f2)
                
            row = [reading_id, time_ms, temp_c, depth_m, f1, f2, f3, f4, f5, f6, f7, f8, clear, nir]
            data.append(row)
            
    # Write to CSV
    os.makedirs(os.path.dirname(output_file), exist_ok=True)
    
    with open(output_file, 'w', newline='') as f:
        writer = csv.writer(f)
        header = ['reading', 'time_ms', 'temp_c', 'depth_m', 'F1_415', 'F2_445', 'F3_480', 'F4_515', 'F5_555', 'F6_590', 'F7_630', 'F8_680', 'clear', 'NIR']
        writer.writerow(header)
        writer.writerows(data)
        
    print(f"Generated {len(data)} rows of dummy data (11 rows per reading) at {output_file}")
    print(f"Distribution: {num_normal} Normal, {num_phyto} Phyto, {num_cdom} CDOM, {num_cyano} Cyano")

if __name__ == '__main__':
    output_csv_path = os.path.join(os.path.dirname(__file__), 'dummy_marine_data.csv')
    
    start_exec = time.time()
    generate_dummy_data(output_csv_path, num_sequences=100)
    exec_time = time.time() - start_exec
    
    print(f"Execution time: {exec_time:.4f} seconds")
