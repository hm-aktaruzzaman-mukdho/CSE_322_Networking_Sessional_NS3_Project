#!/usr/bin/env python3
"""
Plot throughput and goodput graphs for AODV native wormhole detection experiment.

Usage:
  python3 scratch/plot_wormhole_aodv_metrics.py

Output:
  output/throughput_aodv.png
  output/goodput_aodv.png
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
    plt.plot(x, y, marker="o", linewidth=2, color="#ff7f0e")
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
    summary_path = project_root / "output" / "wormhole_aodv_metrics_summary.csv"
    output_dir = project_root / "output"

    data = read_summary(summary_path)
    node_ids = sorted(data.keys())
    throughput_values = [data[node_id]["throughput"] for node_id in node_ids]
    goodput_values = [data[node_id]["goodput"] for node_id in node_ids]

    plot_line(node_ids, throughput_values, "AODV Throughput with Native Wormhole Attack", "Throughput (Kbps)", output_dir / "throughput_aodv.png")
    plot_line(node_ids, goodput_values, "AODV Goodput with Native Wormhole Attack", "Goodput (Kbps)", output_dir / "goodput_aodv.png")

    print(f"Saved {output_dir / 'throughput_aodv.png'}")
    print(f"Saved {output_dir / 'goodput_aodv.png'}")


if __name__ == "__main__":
    main()
