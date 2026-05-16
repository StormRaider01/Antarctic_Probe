import os
import pandas as pd
from sklearn.ensemble import IsolationForest
from sklearn.metrics import classification_report, confusion_matrix
import joblib

def main():
    # 1. Data Ingestion
    current_dir = os.path.dirname(__file__)
    data_path = os.path.join(current_dir, 'dummy_marine_data.csv')
    df = pd.read_csv(data_path)

    print(f"Loaded dataset with {len(df)} rows.")

    # 2. Feature Selection
    # Drop reading and time_ms. Keep temp, depth, and 10 Spectrometer channels.
    X = df.drop(columns=['reading', 'time_ms'])
    
    print(f"Features selected for training: {list(X.columns)}")

    # 3. Model Configuration
    # We injected 15 anomalous sequences out of 100 total (15%). 
    contamination_rate = 0.15
    print(f"Configuring Isolation Forest with contamination={contamination_rate:.4f}")
    
    model = IsolationForest(
        n_estimators=100, 
        contamination=contamination_rate, 
        random_state=42,
        n_jobs=-1 # Use all available cores for speed
    )

    # Train the model
    print("Training the Isolation Forest...")
    model.fit(X)

    # Predict anomalies (-1 for anomaly, 1 for normal)
    predictions = model.predict(X)

    # 4. Validation
    # Generate Ground Truth based on anomaly injection logic
    # Phyto: F8_680 > 100
    # CDOM: F1_415 > 100
    # Cyano: F5_555 > 100
    # Since base values for these are < 100 (except at peak, maybe slightly over, but the anomalous ones go into hundreds/thousands)
    # Actually, let's just check if F8 > 500 or F1 > 500 or F5 > 500
    ground_truth = ((df['F8_680'] > 200) | (df['F1_415'] > 200) | (df['F5_555'] > 200)).astype(int)
    
    # Map predictions to 1 (anomaly) and 0 (normal) to match ground truth
    mapped_predictions = [1 if p == -1 else 0 for p in predictions]

    print("\n--- Validation Results ---")
    print("Confusion Matrix:")
    print(confusion_matrix(ground_truth, mapped_predictions))
    print("\nClassification Report:")
    # Use output_dict=False to print text, but might throw error if only 1 class. 
    try:
        print(classification_report(ground_truth, mapped_predictions, target_names=["Normal", "Anomaly"]))
    except:
        pass

    # 5. Export
    model_export_path = os.path.join(current_dir, 'anomaly_model.joblib')
    joblib.dump(model, model_export_path)
    print(f"\nModel successfully saved to {model_export_path}")

if __name__ == '__main__':
    main()
