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

    def evaluate_reading(self, raw_string: str) -> tuple[bool, str]:
        """
        Evaluates a single raw comma-separated sensor reading string to detect anomalies,
        and provides Explainable AI (XAI) classification if anomalous.
        
        Args:
            raw_string (str): 14-value comma-separated string 
                              (e.g., "1,1777968359,20.5,0.1,92...").
                              
        Returns:
            tuple: (is_anomaly: bool, classification_string: str)
        """
        if self.model is None:
            print("Model is not loaded. Cannot evaluate reading.")
            return False, "Model Not Loaded"
            
        try:
            # Parse the comma-separated string into a list of floats.
            values = [float(val.strip()) for val in raw_string.split(',')]
            
            # Ensure we have exactly 14 values as expected by the protocol
            if len(values) != 14:
                print(f"Warning: Expected 14 values, but received {len(values)}.")
                return False, "Invalid Data Length"
                
            # Drop the first two elements: reading (0) and time_ms (1)
            sensor_values = values[2:]
            
            # The model expects a 2D array or dataframe.
            feature_names = ['temp_c', 'depth_m', 'F1_415', 'F2_445', 'F3_480', 'F4_515', 'F5_555', 'F6_590', 'F7_630', 'F8_680', 'clear', 'NIR']
            df_input = pd.DataFrame([sensor_values], columns=feature_names)
            
            # Predict (-1 is anomaly, 1 is normal)
            prediction = self.model.predict(df_input)[0]
            is_anomaly = bool(prediction == -1)
            
            if is_anomaly:
                # Explainable AI Classification
                classification = self._classify_signature(sensor_values)
                return True, classification
            else:
                return False, "Normal"
            
        except ValueError as ve:
            print(f"Data parsing error: {ve}")
            return False, "Parsing Error"
        except Exception as e:
            print(f"Error during inference: {e}")
            return False, "Inference Error"

    def _classify_signature(self, sensor_values: list) -> str:
        """
        Applies deterministic XAI rules based on academic baselines.
        sensor_values: [temp_c, depth_m, F1_415, F2_445, F3_480, F4_515, F5_555, F6_590, F7_630, F8_680, clear, NIR]
        indices: F1=2, F2=3, F3=4, F4=5, F5=6, F6=7, F7=8, F8=9, clear=10, NIR=11
        """
        f1 = sensor_values[2]
        f2 = sensor_values[3] # Excitation
        f3 = sensor_values[4]
        f5 = sensor_values[6]
        f6 = sensor_values[7]
        f8 = sensor_values[9]
        
        # Avoid division by zero
        if f2 < 1.0:
            f2 = 1.0
            
        # Academic baselines & physical sensor calibration: Ratios against excitation F2_445
        if f8 / f2 > 0.075:
            return "Phytoplankton Bloom (Chlorophyll-a)"
        elif (f5 + f6) / f2 > 0.08:
            return "Cyanobacteria (Phycoerythrin)"
        elif (f1 + f3) / f2 > 0.08:
            return "Bacterial Decay (CDOM)"
            
        return "Non-Biological Variance / Sensor Artifact"

if __name__ == '__main__':
    detector = MarineAnomalyDetector()
    
    # 1. Normal String
    normal_string = "7,117,20.50,0.100,0,0,0,0,1,0,0,1,1,2"
    
    # 2. Anomalous String (Phytoplankton)
    anomalous_string = "7,582,20.50,0.100,56,1429,860,76,45,36,58,700,1212,36"
    
    # Measure execution time for normal inference
    start_time_normal = time.perf_counter()
    is_anomaly_normal, cls_normal = detector.evaluate_reading(normal_string)
    end_time_normal = time.perf_counter()
    
    # Measure execution time for anomalous inference
    start_time_anomalous = time.perf_counter()
    is_anomaly_anomalous, cls_anomalous = detector.evaluate_reading(anomalous_string)
    end_time_anomalous = time.perf_counter()
    
    exec_time_normal = end_time_normal - start_time_normal
    exec_time_anomalous = end_time_anomalous - start_time_anomalous
    
    print("\n--- Validation & Performance Test ---")
    print(f"Normal String Evaluation: Is Anomaly? {is_anomaly_normal} | Class: {cls_normal}")
    print(f"Execution Time (Normal): {exec_time_normal:.5f} seconds")
    
    print(f"\nAnomalous String Evaluation: Is Anomaly? {is_anomaly_anomalous} | Class: {cls_anomalous}")
    print(f"Execution Time (Anomalous): {exec_time_anomalous:.5f} seconds")
    
    if exec_time_normal < 2.0 and exec_time_anomalous < 2.0:
        print("\nSUCCESS: Inference time satisfies System Requirement SR3 (< 2.0 seconds).")
    else:
        print("\nFAILURE: Inference time exceeds System Requirement SR3 limit.")
