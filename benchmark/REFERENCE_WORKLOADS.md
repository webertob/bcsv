# Reference Time-Series Workloads (Item 11.B)

This document defines the open-data workload mapping used for Item 11.B.

## Goal

Add reference workloads inspired by:
- Chimp (Liakos et al., PVLDB 15(11)): https://www.vldb.org/pvldb/vol15/p3058-liakos.pdf

The implementation uses closest available open datasets with explicit mapping to existing BCSV benchmark profiles.

## Dataset Manifest

Manifest file:
- `benchmark/reference_workloads.json`

Download helper:
- `python3 benchmark/fetch_reference_datasets.py`

Cache location (default):
- `tmp/reference_datasets/`

Datasets are intentionally cached outside version control and are not committed.

## Mapping

| Open Dataset ID | BCSV Profile Mapping | Why it is relevant |
|---|---|---|
| `weather_timeseries` | `weather_timeseries` | Float-heavy multi-signal telemetry with temporal structure |
| `daily_temperature` | `realistic_measurement` | Compact sensor-like univariate series with smooth changes |
| `airline_passengers` | `simulation_smooth` | Strong trend/seasonality useful for time-series compression behavior |

## Usage

Fetch all:

```bash
python3 benchmark/fetch_reference_datasets.py
```

Fetch selected IDs:

```bash
python3 benchmark/fetch_reference_datasets.py --ids weather_timeseries,daily_temperature
```

Force re-download:

```bash
python3 benchmark/fetch_reference_datasets.py --force
```

## Notes

- This mapping is an approximation strategy, not a strict reproduction of the paper’s original evaluation environment.
- Any publication-grade comparison should pin exact dataset provenance and preprocessing in a dedicated reproducibility document.
