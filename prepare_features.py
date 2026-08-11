from pathlib import Path
import numpy as np
import pandas as pd

INPUT_FILE = Path("Data/All.csv")
OUTPUT_FILE = Path("Data/recording_features.csv")

EXPECTED_SAMPLES_PER_RECORDING = 50

SENSOR_COLUMNS = [
    "Acceleration X",
    "Acceleration Y",
    "Acceleration Z",
    "Gyroscope X",
    "Gyroscope Y",
    "Gyroscope Z",
]

DERIVED_COLUMNS = [
    "Acceleration Magnitude",
    "Gyroscope Magnitude",
    "Dynamic Acceleration",
]

FEATURE_COLUMNS = SENSOR_COLUMNS + DERIVED_COLUMNS

REQUIRED_COLUMNS = [
    "Recording ID",
    "Timestamp",
    "Shot Type",
    "Recording Key",
    *SENSOR_COLUMNS,
]

def clean_dataset(df: pd.DataFrame) -> pd.DataFrame:
    df = df.copy()

    df["Shot Type"] = (
        df["Shot Type"]
        .astype(str)
        .str.strip()
        .str.title()
    )

    df["Recording Key"] = (
        df["Recording Key"]
        .astype(str)
        .str.strip()
    )

    required_numeric_columns = [
        "Timestamp",
        "Recording ID",
        *SENSOR_COLUMNS,
    ]

    df = df.sort_values(
        by=["Recording Key", "Timestamp"]
    ).reset_index(drop=True)

    df["Time Step"] = (
        df.groupby("Recording Key")
        .cumcount()
        + 1
    )

    return df

def calculate_rms(values: pd.Series) -> float:
    array = values.to_numpy(dtype=float)
    return float(
        np.sqrt(np.mean(array ** 2))
    )

def calculate_energy(values: pd.Series) -> float:
    array = values.to_numpy(dtype=float)
    return float(
        np.sum(array ** 2)
    )

def calculate_total_absolute_change(
    values: pd.Series,
) -> float:
    array = values.to_numpy(dtype=float)
    if len(array) < 2:
        return 0.0
    return float(
        np.sum(np.abs(np.diff(array)))
    )
 
def calculate_maximum_step_change(
    values: pd.Series,
) -> float:
    array = values.to_numpy(dtype=float)
    if len(array) < 2:
        return 0.0
    return float(
        np.max(np.abs(np.diff(array)))
    )

def calculate_peak_time_step(
    values: pd.Series,
) -> int:
    array = values.to_numpy(dtype=float)
    return int(np.argmax(np.abs(array)) + 1)
 
def calculate_zero_crossings(values: pd.Series) -> int:
    array = values.to_numpy(dtype=float)

    signs = np.sign(array)
    signs[signs == 0] = 1

    return int(
        np.sum(signs[1:] != signs[:-1])
    )

def extract_signal_features(
    values: pd.Series,
    prefix: str,
) -> dict:
    return {
        f"{prefix}_mean": values.mean(),
        f"{prefix}_std": values.std(),
        f"{prefix}_min": values.min(),
        f"{prefix}_max": values.max(),
        f"{prefix}_range": values.max() - values.min(),
        f"{prefix}_median": values.median(),
        f"{prefix}_q25": values.quantile(0.25),
        f"{prefix}_q75": values.quantile(0.75),
        f"{prefix}_rms": calculate_rms(values),
        f"{prefix}_energy": calculate_energy(values),
        f"{prefix}_total_abs_change": (
            calculate_total_absolute_change(values)
        ),
        f"{prefix}_max_step_change": (
            calculate_maximum_step_change(values)
        ),
        f"{prefix}_peak_time_step": (
            calculate_peak_time_step(values)
        ),
        f"{prefix}_zero_crossings": (
            calculate_zero_crossings(values)
        ),
        f"{prefix}_first_value": values.iloc[0],
        f"{prefix}_last_value": values.iloc[-1],
    }

def make_safe_column_name(column: str) -> str:
    return (
        column.lower()
        .replace(" ", "_")
        .replace("/", "_")
    )

def extract_recording_features(
    recording: pd.DataFrame,
) -> dict:
    recording = recording.sort_values(
        "Time Step"
    ).reset_index(drop=True)

    features = {
        "Recording Key": recording["Recording Key"].iloc[0],
        "Recording ID": recording["Recording ID"].iloc[0],
        "Shot Type": recording["Shot Type"].iloc[0],
        "Sample Count": len(recording),
    }

    for column in FEATURE_COLUMNS:
        prefix = make_safe_column_name(column)

        signal_features = extract_signal_features(
            recording[column],
            prefix,
        )

        features.update(signal_features)

    features["acceleration_xy_correlation"] = (
        recording["Acceleration X"].corr(
            recording["Acceleration Y"]
        )
    )

    features["acceleration_xz_correlation"] = (
        recording["Acceleration X"].corr(
            recording["Acceleration Z"]
        )
    )

    features["acceleration_yz_correlation"] = (
        recording["Acceleration Y"].corr(
            recording["Acceleration Z"]
        )
    )

    features["gyroscope_xy_correlation"] = (
        recording["Gyroscope X"].corr(
            recording["Gyroscope Y"]
        )
    )

    features["gyroscope_xz_correlation"] = (
        recording["Gyroscope X"].corr(
            recording["Gyroscope Z"]
        )
    )

    features["gyroscope_yz_correlation"] = (
        recording["Gyroscope Y"].corr(
            recording["Gyroscope Z"]
        )
    )

    return features

def create_feature_table(
    df: pd.DataFrame,
) -> pd.DataFrame:
    feature_rows = []

    for _, recording in df.groupby(
        "Recording Key",
        sort=False,
    ):
        feature_rows.append(
            extract_recording_features(recording)
        )

    feature_df = pd.DataFrame(feature_rows)
    feature_df = feature_df.fillna(0)

    return feature_df

def main() -> None:
    df = pd.read_csv("Data/All.csv")
    df = clean_dataset(df)
    feature_df = create_feature_table(
        df
    )
    feature_df.to_csv(
        OUTPUT_FILE,
        index=False,
    )

if __name__ == "__main__":
    main()