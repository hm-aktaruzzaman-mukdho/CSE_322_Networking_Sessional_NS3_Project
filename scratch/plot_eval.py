#!/usr/bin/env python3
import os
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


def normalize_text(value: str) -> str:
    return str(value).strip().lower()


def main() -> None:
    csv_path = Path("sim_results.csv")
    if not csv_path.exists():
        raise FileNotFoundError("sim_results.csv not found. Run evaluation first.")

    df = pd.read_csv(csv_path)
    required_cols = [
        "NetType",
        "Scenario",
        "VaryingParamName",
        "ParamValue",
        "Throughput",
        "Delay",
        "PDR",
        "DropRatio",
        "Energy",
    ]
    missing = [c for c in required_cols if c not in df.columns]
    if missing:
        raise ValueError(f"Missing required columns in CSV: {missing}")

    df["NetTypeNorm"] = df["NetType"].map(normalize_text)
    df["ScenarioNorm"] = df["Scenario"].map(normalize_text)
    df["VaryNorm"] = df["VaryingParamName"].map(normalize_text)
    df["ParamValue"] = pd.to_numeric(df["ParamValue"], errors="coerce")

    scenario_label = {
        "baseline": "Baseline",
        "attack": "Attack",
        "defense": "Proposed",
        "proposed": "Proposed",
        "proposed defense": "Proposed",
    }

    varying_map = {
        "nodes": "Nodes",
        "flows": "Flows",
        "pps": "PPS",
        "speed": "Speed",
        "coverage": "Coverage",
        "txrange": "Coverage",
        "txrange multiplier": "Coverage",
    }

    metrics = [
        ("Throughput", "Throughput (Mbps)", "throughput"),
        ("Delay", "End-to-End Delay (ms)", "delay"),
        ("PDR", "Packet Delivery Ratio (%)", "pdr"),
        ("DropRatio", "Packet Drop Ratio (%)", "dropratio"),
        ("Energy", "Energy Consumption (J)", "energy"),
    ]

    net_types = ["wired", "wireless"]
    scenario_order = ["baseline", "attack", "defense"]

    graphs_dir = Path("graphs")
    graphs_dir.mkdir(exist_ok=True)

    for net in net_types:
        net_df = df[df["NetTypeNorm"] == net].copy()
        if net_df.empty:
            continue

        for vary_key, vary_label in [("nodes", "Nodes"), ("flows", "Flows"), ("pps", "PPS"), ("speed", "Speed"), ("coverage", "Coverage")]:
            group_df = net_df[net_df["VaryNorm"].map(lambda x: varying_map.get(x, x) == vary_label)]
            if group_df.empty:
                continue

            for metric_col, y_label, metric_slug in metrics:
                fig, ax = plt.subplots(figsize=(8, 5))
                has_any_line = False

                for sc in scenario_order:
                    sc_df = group_df[group_df["ScenarioNorm"] == sc].copy()
                    if sc_df.empty:
                        continue

                    # Aggregate repeated runs (if any) at same parameter value.
                    agg = (
                        sc_df.groupby("ParamValue", as_index=False)[metric_col]
                        .mean()
                        .sort_values("ParamValue")
                    )
                    if agg.empty:
                        continue

                    ax.plot(
                        agg["ParamValue"],
                        agg[metric_col],
                        marker="o",
                        linewidth=2,
                        label=scenario_label.get(sc, sc.title()),
                    )
                    has_any_line = True

                if not has_any_line:
                    plt.close(fig)
                    continue

                ax.set_title(f"{net.title()} - {y_label} vs {vary_label}")
                ax.set_xlabel(vary_label)
                ax.set_ylabel(y_label)
                ax.grid(True, alpha=0.3)
                ax.legend()
                fig.tight_layout()

                out_name = f"{net}_{metric_slug}_vs_{vary_key}.png"
                fig.savefig(graphs_dir / out_name, dpi=150)
                plt.close(fig)

    print(f"Plots generated in: {graphs_dir.resolve()}")


if __name__ == "__main__":
    main()
