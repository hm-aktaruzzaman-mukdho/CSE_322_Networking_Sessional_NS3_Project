#!/usr/bin/env python3
"""
Plot throughput and goodput graphs for the wormhole detection MANET experiment.

Usage:
  python3 scratch/plot_wormhole_metrics.py

Output:
  output/throughput.png
  output/goodput.png
"""

import csv
from pathlib import Path
import matplotlib.pyplot as plt


def read_summary(path):
    data = {}
    if not path.exists():
        raise FileNotFoundError(f"Summary file not found: {path}")
    with path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            node_id = int(row["NodeId"])
            data[node_id] = {
                "throughput": float(row["AverageThroughputKbps"]),
                "goodput": float(row["AverageGoodputKbps"]),
            }
    return data


def plot_line(x, y, title, ylabel, output_path):
    plt.figure(figsize=(8, 5), dpi=140)
    plt.plot(x, y, marker="o", linewidth=2, color="#1f77b4")
    plt.xticks(x)
    plt.title(title)
    plt.xlabel("Node ID")
    plt.ylabel(ylabel)
    plt.grid(True, linestyle="--", alpha=0.6)
    plt.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output_path)
    plt.close()


def main():
    project_root = Path(__file__).resolve().parents[1]
    summary_path = project_root / "output" / "wormhole_metrics_summary.csv"
    output_dir = project_root / "output"

    data = read_summary(summary_path)
    node_ids = sorted(data.keys())
    throughput_values = [data[node_id]["throughput"] for node_id in node_ids]
    goodput_values = [data[node_id]["goodput"] for node_id in node_ids]

    plot_line(node_ids, throughput_values, "AODV Throughput in Presence of Wormhole", "Throughput (Kbps)", output_dir / "throughput.png")
    plot_line(node_ids, goodput_values, "AODV Goodput in Presence of Wormhole", "Goodput (Kbps)", output_dir / "goodput.png")

    print(f"Saved {output_dir / 'throughput.png'}")
    print(f"Saved {output_dir / 'goodput.png'}")


if __name__ == "__main__":
    main()
