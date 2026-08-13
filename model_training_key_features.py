from pathlib import Path
import numpy as np
import pandas as pd
from sklearn.model_selection import train_test_split
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import classification_report, confusion_matrix, accuracy_score
import matplotlib.pyplot as plt
import seaborn as sns
import json
from sklearn.tree import plot_tree
import joblib 
import sklearn
 
with open("feature_lists.json", "r") as file:
    feature_lists = json.load(file)
 
zero_importance_features = feature_lists["zero_importance_features"]
top_10_features = feature_lists["top_10_features"]

feature_df = pd.read_csv(
    Path("Data/recording_features.csv")
)

columns_to_exclude = [
    "Recording Key",
    "Recording ID",
    "Shot Type",
    "Sample Count",
]
X = feature_df.drop(
    columns=columns_to_exclude,
    errors="ignore",
)
X = X[top_10_features]
y = feature_df["Shot Type"]

X_train, X_test, y_train, y_test = train_test_split(
    X,
    y,
    test_size=0.3,
    random_state=42,
    stratify=y,
)

print(X_train.shape, X_test.shape, y_train.shape, y_test.shape)
print(y_train.value_counts(normalize=True))

model = RandomForestClassifier(
    n_estimators=300,
    random_state=42,
)

model.fit(X_train, y_train)

predictions = model.predict(X_test)

def evaluate_classification_model(
    model,
    X_test: pd.DataFrame,
    y_test: pd.Series,
    predictions,
) -> None:

    accuracy = accuracy_score(
        y_test,
        predictions,
    )

    print("=" * 60)
    print("MODEL EVALUATION")
    print("=" * 60)

    print(f"\nAccuracy: {accuracy:.2%}")

    print("\nClassification Report:")
    print(
        classification_report(
            y_test,
            predictions,
            digits=3,
        )
    )

    labels = sorted(y_test.unique())

    cm = confusion_matrix(
        y_test,
        predictions,
        labels=labels,

    )
    plt.figure(figsize=(8, 6))

    sns.heatmap(
        cm,
        annot=True,
        fmt="d",
        cmap="Blues",
        xticklabels=labels,
        yticklabels=labels,
    )

    plt.title("Confusion Matrix")
    plt.xlabel("Predicted Shot Type")
    plt.ylabel("Actual Shot Type")

    plt.tight_layout()
    plt.show()

    feature_importance_df = pd.DataFrame(
                {
                    "Feature": X_test.columns,
                    "Importance": model.feature_importances_,
                }
            )

    feature_importance_df = (
        feature_importance_df
        .sort_values(
            by="Importance",
            ascending=False,
        )
        .reset_index(drop=True)
    )

    print("\nTop 15 Most Important Features:")
    print(
        feature_importance_df
        .head(15)
        .to_string(index=False)
    )
    plt.figure(figsize=(10, 7))
    top_features = feature_importance_df.head(15)
    sns.barplot(
        data=top_features,
        x="Importance",
        y="Feature",
    )
    plt.title("Top 15 Feature Importances")
    plt.xlabel("Importance")
    plt.ylabel("Feature")
    plt.tight_layout()
    plt.show()

evaluate_classification_model(
    model=model,
    X_test=X_test,
    y_test=y_test,
    predictions=predictions,
    
)

def visualise_random_forest_tree(

    model,

    X_train,

    tree_index=0,

    max_depth=3,

    figsize=(24, 12),

):
 
    tree = model.estimators_[tree_index]

    plt.figure(figsize=figsize)
 
    plot_tree(
        tree,
        feature_names=X_train.columns,
        class_names=model.classes_,
        filled=True,
        rounded=True,
        proportion=True,
        precision=2,
        max_depth=max_depth,
        fontsize=8,
    )
 
    plt.title(
        f"Random Forest Decision Tree {tree_index + 1}",
        fontsize=16,
    )
    plt.tight_layout()
    plt.show()

visualise_random_forest_tree(
model=model,
X_train=X_train,
tree_index=0,
max_depth=5,
)

def export_model(
    model,
    X_train,
    model_path="cricket_shot_model.joblib",
    metadata_path="model_metadata.json",
):
 
    joblib.dump(
        model,
        model_path,
    )
 
    metadata = {
        "features": X_train.columns.tolist(),
        "classes": model.classes_.tolist(),
        "samples_per_recording": 50,
    }
 
    with open(metadata_path, "w") as file:
        json.dump(
            metadata,
            file,
            indent=4,
        )
 
    print(f"Model saved to: {model_path}")
    print(f"Metadata saved to: {metadata_path}")
 
 
export_model(
    model=model,
    X_train=X_train,
)

model = joblib.load(
    "cricket_shot_model.joblib"
)
with open("model_metadata.json", "r") as file:
    metadata = json.load(file)
    
sample = X_test.iloc[[0]]
prediction = model.predict(sample)
probabilities = model.predict_proba(sample)
print("Prediction:", prediction[0])
print("Probabilities:", probabilities[0])