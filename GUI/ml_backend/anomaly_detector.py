import os
import time
import joblib
import pandas as pd
import numpy as np

class MarineAnomalyDetector:
    def __init__(self):
        """
        Initialises the anomaly detector by loading the pre-trained Isolation Forest model.
        """
        self.model = None
        current_dir = os.path.dirname(__file__)
        model_path = os.path.join(current_dir, 'anomaly_model.joblib')
        
        try:
            self.model = joblib.load(model_path)
            print("Successfully loaded anomaly_model.joblib.")
        except FileNotFoundError:
            print(f"Error: Model file not found at {model_path}. Please train the model first.")
        except Exception as e:
            print(f"Error loading model: {e}")

    def evaluate_reading(self, raw_string: str) -> bool:
        """
        Evaluates a single raw comma-separated sensor reading string to detect anomalies.
        
        Args:
            raw_string (str): 15-value comma-separated string 
                              (e.g., "1,1777968359,1358,2183,92...").
                              
        Returns:
            bool: True if the reading is an anomaly, False if normal.
        """
        if self.model is None:
            print("Model is not loaded. Cannot evaluate reading.")
            return False
            
        try:
            # Parse the comma-separated string into a list of integers.
            # float() first handles both "1358" (simulator) and "1400.0000" (dongle wire format).
            values = [int(float(val.strip())) for val in raw_string.split(',')]
            
            # Ensure we have exactly 15 values as expected by the protocol
            if len(values) != 15:
                print(f"Warning: Expected 15 values, but received {len(values)}.")
                return False
                
            # Drop the first two elements: RecordNum (0) and UnixTime (1)
            sensor_values = values[2:]
            
            # The model expects a 2D array or dataframe. We use a dataframe with the exact feature names 
            # to prevent scikit-learn warnings about missing feature names during inference.
            feature_names = ['RawTemp', 'RawPressure'] + [f'RawSpec{i}' for i in range(1, 12)]
            df_input = pd.DataFrame([sensor_values], columns=feature_names)
            
            # Predict (-1 is anomaly, 1 is normal)
            prediction = self.model.predict(df_input)[0]
            
            # Return True for anomaly, False for normal
            return bool(prediction == -1)
            
        except ValueError as ve:
            print(f"Data parsing error: {ve}")
            return False
        except Exception as e:
            print(f"Error during inference: {e}")
            return False

if __name__ == '__main__':
    detector = MarineAnomalyDetector()
    
    # 1. Normal String (simulated from our dataset parameters: low spectrometer values)
    normal_string = "1,1777968359,1358,2183,92,105,88,110,95,100,120,80,90,110,105"
    
    # 2. Anomalous String (simulated massive spikes in Spec6 and Spec7)
    anomalous_string = "2,1777968419,1358,2183,92,105,88,110,95,3500,3800,80,90,110,105"
    
    # Measure execution time for normal inference
    start_time_normal = time.perf_counter()
    is_anomaly_normal = detector.evaluate_reading(normal_string)
    end_time_normal = time.perf_counter()
    
    # Measure execution time for anomalous inference
    start_time_anomalous = time.perf_counter()
    is_anomaly_anomalous = detector.evaluate_reading(anomalous_string)
    end_time_anomalous = time.perf_counter()
    
    exec_time_normal = end_time_normal - start_time_normal
    exec_time_anomalous = end_time_anomalous - start_time_anomalous
    
    print("\n--- Validation & Performance Test ---")
    print(f"Normal String Evaluation: Is Anomaly? {is_anomaly_normal}")
    print(f"Execution Time (Normal): {exec_time_normal:.5f} seconds")
    
    print(f"\nAnomalous String Evaluation: Is Anomaly? {is_anomaly_anomalous}")
    print(f"Execution Time (Anomalous): {exec_time_anomalous:.5f} seconds")
    
    if exec_time_normal < 2.0 and exec_time_anomalous < 2.0:
        print("\nSUCCESS: Inference time satisfies System Requirement SR3 (< 2.0 seconds).")
    else:
        print("\nFAILURE: Inference time exceeds System Requirement SR3 limit.")
