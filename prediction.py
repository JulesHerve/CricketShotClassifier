import joblib
import json

model = joblib.load(
    "cricket_shot_model.joblib"
)

with open("model_metadata.json", "r") as file:
    metadata = json.load(file)
    
print(model.classes_)
print(metadata["features"])