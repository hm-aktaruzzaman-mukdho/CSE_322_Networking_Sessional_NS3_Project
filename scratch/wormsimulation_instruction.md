cat > /tmp/summary.txt << 'EOF'
WORMHOLE ATTACK DETECTION - DUAL IMPLEMENTATION SUMMARY
========================================================

PROJECT STRUCTURE:
==================

Two separate implementations created in scratch/ directory:

1. OVERLAY-BASED APPROACH (wormhole_attack_detection.cc)
   - Uses point-to-point link overlay between nodes 0-1
   - Custom UDP echo applications for RTT probing
   - RTT-based detection via exponential moving average comparison
   - Wormhole simulated with 1Gbps link, 0.1ms delay
   
2. AODV NATIVE APPROACH (wormhole_aodv_native.cc)
   - Uses modified AODV routing protocol built-in features
   - AODV-layer wormhole attack and detection
   - No custom applications needed
   - RTT detection integrated into route discovery

GENERATED FILES:
================

Simulation Outputs:
  • output/wormhole_metrics_summary.csv          (overlay version metrics)
  • output/wormhole_aodv_metrics_summary.csv     (AODV version metrics)
  • output/wormhole_rtt_flow_*.csv               (per-flow RTT logs, overlay)
  • output/wormhole_metrics.csv                  (detailed overlay flows)
  • output/wormhole_aodv_metrics.csv             (detailed AODV flows)
  • output/wormhole_flowmon.xml                  (overlay FlowMonitor dump)
  • output/wormhole_aodv_flowmon.xml             (AODV FlowMonitor dump)

Visualization Graphs (4 PNG files):
  ✓ output/throughput.png          (overlay approach)
  ✓ output/goodput.png             (overlay approach)
  ✓ output/throughput_aodv.png     (AODV approach)
  ✓ output/goodput_aodv.png        (AODV approach)

Python Plotting Scripts:
  • scratch/plot_wormhole_metrics.py             (overlay plotting)
  • scratch/plot_wormhole_aodv_metrics.py        (AODV plotting)

RUNNING SIMULATIONS:
====================

Overlay-based (custom applications):
  ./ns3 run scratch/wormhole_attack_detection -- --SimulationTime=30

AODV native (modified routing protocol):
  ./ns3 run scratch/wormhole_aodv_native -- --SimulationTime=30 [--EnableWormhole=true] [--EnableDetection=true]

Generate Graphs:
  python3 scratch/plot_wormhole_metrics.py
  python3 scratch/plot_wormhole_aodv_metrics.py

COMPARISON:
===========

Overlay Approach (wormhole_attack_detection.cc):
- Pros: Easier to visualize, explicit RTT calculations logged to CSV
- Cons: Requires custom application layer code
- Detection: Via custom RTT probe mechanism

AODV Native Approach (wormhole_aodv_native.cc):
- Pros: Proper protocol-level implementation, production-like
- Cons: No direct RTT logs (handled internally by AODV)
- Detection: Integrated into AODV route discovery

METRICS:
========

5-node MANET topology with circular traffic:
  Flows: (0→1), (1→2), (2→3), (3→4), (4→0)
  Both versions show ~7.2 Kbps throughput/goodput per node
  (Limited by 11 Mbps 802.11b Wi-Fi with 5 parallel UDP flows)

Wormhole Configuration:
  µ = 2.5 milliseconds (detection threshold)
  Tunnel endpoints: Nodes 0 and 1
  Tunnel bandwidth: 1000 Mbps (overlay) vs integrated AODV (native)
  
Simulation Time: 15 seconds

EXISTING FILES PRESERVED:
=========================

✓ wormhole_attack_detection.cc   (original - NOT modified)
✓ plot_wormhole_metrics.py        (original - NOT modified)
✓ All other existing files intact
EOF
cat /tmp/summary.txt