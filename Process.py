from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns

DATA_DIRECTORY = Path("data")
OUTPUT_DIRECTORY = Path("outputs")
EXPECTED_SAMPLES_PER_RECORDING = 50

def load_all_csv_files(data_directory: Path) -> pd.DataFrame:
    csv_files = sorted(data_directory.glob("*.csv"))
    if not csv_files:
        raise FileNotFoundError(
            f"No CSV files were found in {data_directory.resolve()}"
        )
    dataframes = []
    for file_path in csv_files:
        print(f"Loading {file_path.name}")
        dataframes.append(pd.read_csv(file_path))
    combined_dataframe = pd.concat(
        dataframes,
        ignore_index=True,
    )
    return combined_dataframe

def add_recording_key(dataframe: pd.DataFrame) -> pd.DataFrame:
    dataframe = dataframe.copy()
    dataframe["Recording Key"] = (
        dataframe["Shot Type"].astype(str)
        + "_"
        + dataframe["Recording ID"].astype(str)
    )
    return dataframe

def add_relative_time(dataframe: pd.DataFrame) -> pd.DataFrame:
    dataframe = dataframe.copy()
    first_timestamp = dataframe.groupby(
        "Recording Key"
    )["Timestamp"].transform("min")
    dataframe["Relative Timestamp"] = (
        dataframe["Timestamp"] - first_timestamp
    )
    return dataframe

def add_motion_magnitudes(dataframe: pd.DataFrame) -> pd.DataFrame:
    dataframe = dataframe.copy()
    dataframe["Acceleration Magnitude"] = np.sqrt(
        dataframe["Acceleration X"] ** 2
        + dataframe["Acceleration Y"] ** 2
        + dataframe["Acceleration Z"] ** 2
    )
    dataframe["Gyroscope Magnitude"] = np.sqrt(
        dataframe["Gyroscope X"] ** 2
        + dataframe["Gyroscope Y"] ** 2
        + dataframe["Gyroscope Z"] ** 2
    )
    dataframe["Dynamic Acceleration"] = np.abs(
        dataframe["Acceleration Magnitude"] - 1.0
    )
    return dataframe

def plot_recording(
    dataframe: pd.DataFrame,
    recording_key: str,
    output_directory: Path,
) -> None:
    recording = dataframe[
        dataframe["Recording Key"] == recording_key
    ].copy()
 
    recording = recording.sort_values("Timestamp")
    time_values = recording["Relative Timestamp"]
    shot_type = recording["Shot Type"].iloc[0]
    acceleration_figure = plt.figure(figsize=(10, 6))
    for column in ["Acceleration X", "Acceleration Y", "Acceleration Z"]:
        plt.plot(
            time_values,
            recording[column],
            label=column,
        )
    plt.title(
        f"Acceleration Signals: {recording_key} ({shot_type})"
    )
    plt.xlabel("Relative Timestamp")
    plt.ylabel("Acceleration")
    plt.legend()
    plt.tight_layout()
    acceleration_path = (
        output_directory
        / f"{recording_key}_acceleration.png"
    )
    acceleration_figure.savefig(
        acceleration_path,
        dpi=150,
    )
    plt.close(acceleration_figure)

    gyroscope_figure = plt.figure(figsize=(10, 6))
    for column in ["Gyroscope X", "Gyroscope Y", "Gyroscope Z"]:
        plt.plot(
            time_values,
            recording[column],
            label=column,
        )
    plt.title(
        f"Gyroscope Signals: {recording_key} ({shot_type})"
    )
    plt.xlabel("Relative Timestamp")
    plt.ylabel("Angular Velocity")
    plt.legend()
    plt.tight_layout()
    gyroscope_path = (
        output_directory
        / f"{recording_key}_gyroscope.png"
    )
    gyroscope_figure.savefig(
        gyroscope_path,
        dpi=150,
    )
    plt.close(gyroscope_figure)

    magnitude_figure = plt.figure(figsize=(10, 6))
    plt.plot(
        time_values,
        recording["Acceleration Magnitude"],
        label="Acceleration Magnitude",
    )
    plt.plot(
        time_values,
        recording["Gyroscope Magnitude"],
        label="Gyroscope Magnitude",
    )
    plt.title(
        f"Motion Magnitudes: {recording_key} ({shot_type})"
    )
    plt.xlabel("Relative Timestamp")
    plt.ylabel("Magnitude")
    plt.legend()
    plt.tight_layout()
    magnitude_path = (
        output_directory
        / f"{recording_key}_magnitudes.png"
    )
    magnitude_figure.savefig(
        magnitude_path,
        dpi=150,
    )
    plt.close(magnitude_figure)

def shot_comparison(df):
    df = df.groupby(["Shot Type", "Time Step"]).agg(
        {
            "Dynamic Acceleration": "mean",
            "Gyroscope Magnitude": "mean",
        }
    ).reset_index()
    return df

def add_time_step_column(df):
    df = df.copy()
    df["Time Step"] = df.groupby("Recording Key").cumcount() + 1
    return df

def shot_comparison_plot(grouped_df, shot_type_column="Shot Type", time_step_column="Time Step", dynamic_acceleration_column="Dynamic Acceleration", gyroscope_magnitude_column="Gyroscope Magnitude", output_directory=OUTPUT_DIRECTORY):
    shot_types = grouped_df[shot_type_column].unique()
    da_figure=plt.figure(figsize=(10, 6))
    for shot_type in shot_types:
        shot_data = grouped_df[
            grouped_df[shot_type_column] == shot_type
        ]
        plt.plot(
            shot_data[time_step_column],
            shot_data[dynamic_acceleration_column],
            marker="o",
            label=shot_type,
        )
    plt.title("Dynamic Acceleration by Shot Type and Time Step")
    plt.xlabel("Time Step")
    plt.ylabel("Dynamic Acceleration")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.show()
    ga_figure=plt.figure(figsize=(10, 6))

    for shot_type in shot_types:
        shot_data = grouped_df[
            grouped_df[shot_type_column] == shot_type
        ]
        plt.plot(
            shot_data[time_step_column],
            shot_data[gyroscope_magnitude_column],
            marker="o",
            label=shot_type,
        )
    plt.title("Gyroscope Magnitude by Shot Type and Time Step")
    plt.xlabel("Time Step")
    plt.ylabel("Gyroscope Magnitude")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.show()
    ga_figure.savefig(
        output_directory / "gyroscope.png",
        dpi=150,
    )
    plt.close(ga_figure)
    da_figure.savefig(
            output_directory / "dynamic_acceleration.png",
            dpi=150,
        )
    plt.close(da_figure)
 

def main():
    OUTPUT_DIRECTORY.mkdir(exist_ok=True)
    df = pd.read_csv(DATA_DIRECTORY / "All.csv")
    df = add_time_step_column(df)
    shot_comparison_df = shot_comparison(df)
    shot_comparison_plot(shot_comparison_df)
    

if __name__ == "__main__":
    main()