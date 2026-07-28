from pathlib import Path

import numpy as np
import pandas as pd

DATA_DIRECTORY = Path("data")
OUTPUT_DIRECTORY = Path("outputs")
OUTPUT_FILENAME = "combined_recordings.csv"
SHOT_TYPE_NAMES = ["Cut", "Drive", "Pull", "Stationary"]


def find_csv_file(data_directory: Path, shot_type_name: str) -> Path:
    matches = [
        file_path
        for file_path in data_directory.glob("*.csv")
        if file_path.stem.lower() == shot_type_name.lower()
    ]
    if not matches:
        raise FileNotFoundError(
            f"No file named {shot_type_name}.csv was found in "
            f"{data_directory.resolve()}"
        )
    return matches[0]


def load_shot_type_csv_files(
    data_directory: Path,
    shot_type_names: list[str],
) -> pd.DataFrame:
    dataframes = []
    for shot_type_name in shot_type_names:
        file_path = find_csv_file(data_directory, shot_type_name)
        print(f"Loading {file_path.name}")
        dataframe = pd.read_csv(file_path)
        if "Shot Type" not in dataframe.columns:
            dataframe["Shot Type"] = shot_type_name
        dataframes.append(dataframe)
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


def save_combined_dataframe(
    dataframe: pd.DataFrame,
    output_directory: Path,
    output_filename: str,
) -> Path:
    output_directory.mkdir(exist_ok=True)
    output_path = output_directory / output_filename
    dataframe.to_csv(output_path, index=False)
    return output_path


def main():
    df = load_shot_type_csv_files(DATA_DIRECTORY, SHOT_TYPE_NAMES)
    df = add_recording_key(df)
    df = add_relative_time(df)
    df = add_motion_magnitudes(df)
    output_path = save_combined_dataframe(
        df,
        OUTPUT_DIRECTORY,
        OUTPUT_FILENAME,
    )
    print(
        f"Saved {len(df)} rows from "
        f"{df['Recording Key'].nunique()} recordings to "
        f"{output_path.resolve()}"
    )


if __name__ == "__main__":
    main()