import os
import random
import time
import csv

def generate_dummy_data(output_file, num_normal=5000, num_anomalous=50):
    """
    Generates dummy marine sensor data in the required 15-value string format.
    Format: RecordNum,UnixTime,RawTemp,RawPressure,RawSpec1...RawSpec11
    
    Args:
        output_file (str): Path to the output CSV file.
        num_normal (int): Number of normal data rows to generate.
        num_anomalous (int): Number of anomalous data rows to generate.
    """
    total_rows = num_normal + num_anomalous
    
    # Randomly select indices for the anomalous rows
    anomaly_indices = set(random.sample(range(1, total_rows + 1), num_anomalous))
    
    start_time = int(time.time()) - (total_rows * 60) # Start in the past, 1 minute intervals
    
    data = []
    
    # Generate data
    for i in range(1, total_rows + 1):
        record_num = i
        unix_time = start_time + (i * 60)
        
        # Normal data ranges (simulating raw ADC values, e.g., 12-bit 0-4095)
        raw_temp = random.randint(1200, 1500)      # Simulating -2C to -30C raw thermistor ADC
        raw_pressure = random.randint(2000, 2500)  # Simulating depth pressure ADC
        
        # Spectrometer baseline (simulating 8-bit or 12-bit raw counts)
        raw_specs = [random.randint(50, 150) for _ in range(11)]
        
        # Inject anomalies
        if i in anomaly_indices:
            # Massive spikes in Spectrometer channels 6 and 7
            raw_specs[5] = random.randint(3000, 4000) # RawSpec6 (0-indexed 5)
            raw_specs[6] = random.randint(3000, 4000) # RawSpec7 (0-indexed 6)
            
            # Optionally add some noise to other sensors to make it interesting
            raw_temp += random.randint(-100, 100)
            
        row = [record_num, unix_time, raw_temp, raw_pressure] + raw_specs
        data.append(row)
        
    # Write to CSV
    os.makedirs(os.path.dirname(output_file), exist_ok=True)
    
    with open(output_file, 'w', newline='') as f:
        writer = csv.writer(f)
        # Adding header for ease of use in ML scripts
        header = ['RecordNum', 'UnixTime', 'RawTemp', 'RawPressure'] + [f'RawSpec{i}' for i in range(1, 12)]
        writer.writerow(header)
        writer.writerows(data)
        
    print(f"Generated {total_rows} rows of dummy data ({num_normal} normal, {num_anomalous} anomalous) at {output_file}")

if __name__ == '__main__':
    # File path for the generated dataset
    output_csv_path = os.path.join(os.path.dirname(__file__), 'dummy_marine_data.csv')
    
    start_exec = time.time()
    generate_dummy_data(output_csv_path, num_normal=5000, num_anomalous=50)
    exec_time = time.time() - start_exec
    
    print(f"Execution time: {exec_time:.4f} seconds")
    
    # Basic validation
    file_size = os.path.getsize(output_csv_path)
    print(f"Dataset file size: {file_size / 1024:.2f} KB")
