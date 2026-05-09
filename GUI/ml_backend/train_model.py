import os
import pandas as pd
from sklearn.ensemble import IsolationForest
from sklearn.metrics import classification_report, confusion_matrix
import joblib

def main():
    # 1. Data Ingestion
    current_dir = os.path.dirname(__file__)
    data_path = os.path.join(current_dir, 'dummy_marine_data.csv')
    df = pd.load_csv(data_path) if hasattr(pd, 'load_csv') else pd.read_csv(data_path)

    print(f"Loaded dataset with {len(df)} rows.")

    # 2. Feature Selection
    # Drop RecordNum and UnixTime. Keep Temp, Pressure, and 11 Spectrometer channels.
    X = df.drop(columns=['RecordNum', 'UnixTime'])
    
    print(f"Features selected for training: {list(X.columns)}")

    # 3. Model Configuration
    # Contamination is exactly 50 anomalies out of 5050 rows.
    contamination_rate = 50 / 5050
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
    # Generate Ground Truth: Anomalies were injected with values 3000-4000 in RawSpec6 and RawSpec7.
    # Normal values are between 50 and 150. We can safely use > 1000 as a threshold for ground truth.
    ground_truth = ((df['RawSpec6'] > 1000) | (df['RawSpec7'] > 1000)).astype(int)
    
    # Map predictions to 1 (anomaly) and 0 (normal) to match ground truth
    mapped_predictions = [1 if p == -1 else 0 for p in predictions]

    print("\n--- Validation Results ---")
    print("Confusion Matrix:")
    print(confusion_matrix(ground_truth, mapped_predictions))
    print("\nClassification Report:")
    print(classification_report(ground_truth, mapped_predictions, target_names=["Normal", "Anomaly"]))

    # 5. Export
    model_export_path = os.path.join(current_dir, 'anomaly_model.joblib')
    joblib.dump(model, model_export_path)
    print(f"\nModel successfully saved to {model_export_path}")

if __name__ == '__main__':
    main()
