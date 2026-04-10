/**
 * @file aodv-wormhole-eval.cc
 * @brief NS-3 Simulation for Evaluating RTT-Based AODV Wormhole Detection
 *
 * This simulator evaluates the performance of an RTT-based wormhole attack detection
 * mechanism for AODV routing protocol. It supports:
 *  - Two network topologies: wired (CSMA) and wireless (802.11b ad-hoc)
 *  - Three scenarios: baseline (normal network), attack (wormhole present), defense (detection active)
 *  - Parameterized sweeps: nodes, flows, packet rate, mobility, TX range
 *  - Metrics: throughput, latency, PDR, drop ratio, energy consumption
 *
 * Usage:
 *   ./ns3 run "scratch/aodv-wormhole-eval --help"
 *   ./ns3 run "scratch/aodv-wormhole-eval --netType=wireless --scenario=defense --nNodes=60"
 */

#include "ns3/aodv-helper.h"
#include "ns3/applications-module.h"
#include "ns3/basic-energy-source-helper.h"
#include "ns3/core-module.h"
#include "ns3/csma-helper.h"
#include "ns3/double.h"
#include "ns3/energy-source-container.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-helper.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-module.h"
#include "ns3/wifi-radio-energy-model-helper.h"
#include "ns3/yans-wifi-helper.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;


/*

┌─────────────────────────────────────────────────────────┐
│                    WIRELESS VARIANT                      │
│                                                           │
│   [Node 0]    [Node 1]════════[Node 2]    [Node 3]      │
│       ║           ║             ║            ║           │
│       ║    ┌──────║─────────────║────────────║─┐         │
│       ║    │   TUNNEL OVERLAY (1Gbps, 1ms)  │ ║         │
│   [Node 59] ║    │      (attack/defense)      │ ║         │
│       ║      └──────────────────────────────┘ ║         │
│       └──────────────────────────────────────┘  (Random  │
│          802.11b Ad-hoc Mesh                   Waypoint) │
│          Range: ~500m × 500m (txRange=1)                 │
│                                                           │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│                     WIRED VARIANT                        │
│                                                           │
│   [Node 0]─[Node 1]─[Node 2]─[Node 3]─...─[Node 59]    │
│       │       │       │       │               │          │
│       └───────┴───────┴───────┴───────────────┘          │
│            CSMA Shared Bus                               │
│            100Mbps, 2ms per link                         │
│                                                           │
│   Optional Tunnel Overlay (Node 1 ↔ Node 2):            │
│   ════════════════════════════════════════              │
│         1Gbps, 1ms (attack/defense mode)                │
│                                                           │
└─────────────────────────────────────────────────────────┘


*/

/**
 * @namespace Helper functions for string manipulation and scenario classification
 */
namespace
{
/**
 * Convert string to lowercase for case-insensitive comparison
 * @param s Input string
 * @return Lowercase version of input
 */
std::string
ToLower(const std::string& s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}

/**
 * Check if the specified network type is wireless
 * @param netType Network type string ("wireless" or "wired")
 * @return True if wireless, false otherwise
 */
bool
IsWireless(const std::string& netType)
{
    return ToLower(netType) == "wireless";
}

/**
 * Check if scenario includes a wormhole tunnel overlay
 * @param scenario Scenario type ("baseline", "attack", or "defense")
 * @return True if tunnel should be created (attack or defense), false otherwise
 */
bool
HasWormholeTunnel(const std::string& scenario)
{
    const std::string s = ToLower(scenario);
    return s == "attack" || s == "defense";
}

/**
 * Check if scenario uses defense mechanism
 * @param scenario Scenario type ("baseline", "attack", or "defense")
 * @return True if defense is enabled
 */
bool
IsDefense(const std::string& scenario)
{
    return ToLower(scenario) == "defense";
}

/**
 * Check if scenario requires malicious wormhole forwarding behavior
 * @param scenario Scenario type ("baseline", "attack", or "defense")
 * @return True only for attack scenario (defense uses legitimate routing)
 */
bool
NeedMaliciousTunnelBehavior(const std::string& scenario)
{
    return ToLower(scenario) == "attack";
}
} // namespace

/**
 * Main simulation entry point
 * Configures and runs the AODV wormhole detection evaluation simulator
 */
int
main(int argc, char* argv[])
{
    // ============ SIMULATION PARAMETERS ============
    // Network topology and scenario configuration
    std::string netType = "wireless";      // "wireless" (802.11b) or "wired" (CSMA)
    std::string scenario = "baseline";     // "baseline" (no attack), "attack" (wormhole), "defense" (detection enabled)
    
    // Network size and traffic load
    uint32_t nNodes = 60;                  // Number of nodes in network
    uint32_t nFlows = 20;                  // Number of concurrent UDP flows
    uint32_t pps = 100;                    // Packets per second per flow
    
    // Mobility and wireless parameters
    double speed = 10.0;                   // Node movement speed (m/s) for RandomWaypoint model
    double txRange = 1.0;                  // Wireless TX power multiplier (affects transmission range)
    
    // Metrics and output
    std::string varyingParamName = "Manual"; // Name of parameter being swept (for CSV column)
    double paramValue = 0.0;               // Current value of swept parameter
    std::string resultsFile = "sim_results.csv"; // Output CSV file path (supports parallel writes with unique files)

    // ============ COMMAND-LINE ARGUMENT PARSING ============
    // Parse command-line arguments to override defaults
    CommandLine cmd(__FILE__);
    cmd.AddValue("netType", "Network type: wired or wireless", netType);
    cmd.AddValue("scenario", "Scenario: baseline, attack, or defense", scenario);
    cmd.AddValue("nNodes", "Number of nodes", nNodes);
    cmd.AddValue("nFlows", "Number of UDP flows", nFlows);
    cmd.AddValue("pps", "Packets per second per flow", pps);
    cmd.AddValue("speed", "Node mobility speed (m/s, 0=static)", speed);
    cmd.AddValue("txRange", "Wireless TX range multiplier", txRange);
    cmd.AddValue("varyingParamName", "Name of varied parameter for CSV", varyingParamName);
    cmd.AddValue("paramValue", "Value of varied parameter for CSV", paramValue);
    cmd.AddValue("resultsFile", "Output CSV file path", resultsFile);
    cmd.Parse(argc, argv);

    // Normalize input to lowercase
    netType = ToLower(netType);
    scenario = ToLower(scenario);

    // ============ INPUT VALIDATION ============
    if (netType != "wired" && netType != "wireless")
    {
        NS_FATAL_ERROR("--netType must be wired or wireless");
    }
    if (scenario != "baseline" && scenario != "attack" && scenario != "defense")
    {
        NS_FATAL_ERROR("--scenario must be baseline, attack, or defense");
    }

    // ============ SCENARIO FLAGS ============
    // Derived flags determine which features to enable
    const bool wireless = IsWireless(netType);         // True if 802.11b, false if CSMA
    const bool hasTunnel = HasWormholeTunnel(scenario); // True if attack or defense scenario
    const bool defense = IsDefense(scenario);           // True only if defense is active

    // ============ SIMULATION TIMING ============
    const double simTime = 120.0;          // Total simulation duration (seconds)
    const double appStart = 2.0;           // Time before first application starts (seconds)
    const uint32_t packetSize = 512;       // UDP payload size per packet (bytes)

    // ============ NETWORK SETUP ============
    // Create node container for main network topology
    NodeContainer nodes;
    nodes.Create(nNodes);

    // Install network devices on nodes (WiFi or CSMA depending on netType)
    NetDeviceContainer mainDevices;
    if (wireless)
    {
        // ============ WIRELESS TOPOLOGY (802.11b Ad-Hoc) ============
        // IEEE 802.11b WiFi standard (11 Mbps, 2.4 GHz)
        WifiHelper wifi;
        wifi.SetStandard(WIFI_STANDARD_80211b);

        // YANS channel model with default propagation loss and delay models
        YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
        YansWifiPhyHelper phy;
        phy.SetChannel(channel.Create());

        // Configure TX power based on txRange multiplier
        // Formula: 7 dBm + (3 dBm per range unit above 1.0)
        // Default txRange=1.0 → 7 dBm ≈ 100m range
        const double txPowerDbm = 7.0 + 3.0 * std::max(0.0, txRange - 1.0);
        phy.Set("TxPowerStart", DoubleValue(txPowerDbm));
        phy.Set("TxPowerEnd", DoubleValue(txPowerDbm));

        // Ad-hoc WiFi MAC (no access point, peer-to-peer communication)
        WifiMacHelper mac;
        mac.SetType("ns3::AdhocWifiMac");
        mainDevices = wifi.Install(phy, mac, nodes);

        // ============ WIRELESS POSITIONING & MOBILITY ============
        // Position nodes randomly in rectangular area
        // Area size scales with TX range to maintain network connectivity
        MobilityHelper mobility;
        Ptr<RandomRectanglePositionAllocator> posAlloc = CreateObject<RandomRectanglePositionAllocator>();
        const double areaScale = 500.0 * std::max(1.0, txRange);  // Base 500m × 500m, scaled by txRange
        posAlloc->SetAttribute("X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string(areaScale) + "]"));
        posAlloc->SetAttribute("Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string(areaScale) + "]"));
        mobility.SetPositionAllocator(posAlloc);

        // Choose mobility model: static (speed=0) or mobile (RandomWaypoint with speed)
        if (speed > 0.0)
        {
            // RandomWaypoint: nodes move at constant speed between random waypoints
            // Pause for 0.5 seconds at each waypoint before selecting new destination
            std::ostringstream speedRv;
            speedRv << "ns3::UniformRandomVariable[Min=" << speed << "|Max=" << speed << "]";
            mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                                      "Speed",
                                      StringValue(speedRv.str()),
                                      "Pause",
                                      StringValue("ns3::ConstantRandomVariable[Constant=0.5]"),
                                      "PositionAllocator",
                                      PointerValue(posAlloc));
        }
        else
        {
            // Static positioning: nodes remain at initial positions
            mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        }
        mobility.Install(nodes);
    }
    else
    {
        // ============ WIRED TOPOLOGY (CSMA 100Base-T) ============
        // CSMA (Carrier Sense Multiple Access) shared bus network
        CsmaHelper csma;
        csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));  // 100 Mbps data rate
        csma.SetChannelAttribute("Delay", TimeValue(MilliSeconds(2))); // 2 ms propagation delay
        mainDevices = csma.Install(nodes);

        // Wired networks have fixed topology, so all nodes use static positioning
        MobilityHelper mobility;
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobility.Install(nodes);
    }

    // ============ WORMHOLE TUNNEL OVERLAY ============
    // Create a separate high-speed point-to-point link between Node 1 and Node 2
    // This overlay represents an attacker's high-speed tunnel used in attack/defense scenarios
    // In attack mode: tunnel attracts malicious route replies
    // In defense mode: RTT anomaly is detected on this tunnel
    NetDeviceContainer tunnelDevices;
    if (hasTunnel)
    {
        // Wormhole tunnel connects Node 1 (index 1) and Node 2 (index 2)
        NodeContainer wormholePair(nodes.Get(1), nodes.Get(2));
        PointToPointHelper tunnel;
        tunnel.SetDeviceAttribute("DataRate", StringValue("1Gbps"));    // 1 Gbps capacity
        tunnel.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1))); // 1 ms latency
        tunnelDevices = tunnel.Install(wormholePair);
    }

    // ============ ROUTING PROTOCOL CONFIGURATION (AODV) ============
    // Configure AODV with wormhole detection and attack simulation features
    AodvHelper aodv;
    
    // Defense mechanisms: enabled only in defense scenario
    aodv.Set("EnableRttWormholeDetect", BooleanValue(defense));
    aodv.Set("EnableSecondProbeVerification", BooleanValue(defense));

    // Attack simulation: enabled only in attack scenario
    if (NeedMaliciousTunnelBehavior(scenario))
    {
        // Wormhole attack: forge route replies with tunnel as shortest path
        aodv.Set("EnableWrmAttack", BooleanValue(true));
        // IP addresses of tunnel endpoints
        aodv.Set("FirstEndOfWormTunnel", Ipv4AddressValue("10.1.0.1"));      // Wired tunnel end 1
        aodv.Set("SecondEndOfWormTunnel", Ipv4AddressValue("10.1.0.2"));     // Wired tunnel end 2
        aodv.Set("FirstEndWifiWormTunnel", Ipv4AddressValue("10.0.0.2"));    // Wireless tunnel end 1
        aodv.Set("SecondEndWifiWormTunnel", Ipv4AddressValue("10.0.0.3"));   // Wireless tunnel end 2
    }
    else
    {
        // Baseline and defense: normal AODV operation (no malicious forwarding)
        aodv.Set("EnableWrmAttack", BooleanValue(false));
    }

    // ============ INTERNET STACK INSTALLATION ============
    // Install TCP/IP stack with AODV routing on all nodes
    InternetStackHelper internet;
    internet.SetRoutingHelper(aodv);
    internet.Install(nodes);

    // ============ IP ADDRESS ASSIGNMENT ============
    // Assign IP addresses to main network interfaces
    Ipv4AddressHelper mainIp;
    mainIp.SetBase("10.0.0.0", "255.255.0.0");  // Subnet for main network devices (65534 hosts)
    Ipv4InterfaceContainer mainIfaces = mainIp.Assign(mainDevices);

    // Assign IP addresses to tunnel interfaces (only if tunnel exists)
    Ipv4InterfaceContainer tunnelIfaces;
    if (hasTunnel)
    {
        Ipv4AddressHelper tunnelIp;
        tunnelIp.SetBase("10.1.0.0", "255.255.255.252"); // Subnet for point-to-point tunnel (2 hosts: .1 and .2)
        tunnelIfaces = tunnelIp.Assign(tunnelDevices);
    }

    // In defense mode, keep RTT detector enabled and avoid malicious tunnel forwarding.
    // We intentionally do not force interfaces down at runtime because it can leave
    // stale output-interface hints that trigger AODV source-address assertions.

    // Populate routing tables based on current network topology
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // ============ ENERGY MODEL (WIRELESS ONLY) ============
    // Install energy source and consumption model for wireless nodes
    // Energy consumption is tracked as a metric for battery-powered devices
    std::vector<Ptr<EnergySource>> energySources;
    if (wireless)
    {
        // Each node has a basic energy source with 100J initial energy
        BasicEnergySourceHelper basicSource;
        basicSource.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(100.0));
        EnergySourceContainer installedSources = basicSource.Install(nodes);

        // WiFi radio consumes energy from the energy source based on TX/RX activity
        WifiRadioEnergyModelHelper radioModel;
        radioModel.Install(mainDevices, installedSources);

        // Store Ptr references to energy sources for later consumption calculation
        // Using vector instead of EnergySourceContainer avoids destructor crashes
        for (auto it = installedSources.Begin(); it != installedSources.End(); ++it)
        {
            energySources.push_back(*it);
        }
    }

    // ============ APPLICATION LAYER - UDP FLOWS ============
    // Create nFlows concurrent UDP flows with staggered start times
    // This generates network traffic for performance evaluation
    ApplicationContainer allApps;
    for (uint32_t f = 0; f < nFlows; ++f)
    {
        // Select source and destination nodes
        // Source: round-robin across all nodes
        uint32_t src = f % nNodes;
        // Destination: offset from source by half network size + random bias to ensure distance
        uint32_t dst = (src + (nNodes / 2) + (f % 7) + 1) % nNodes;
        if (dst == src)  // Ensure src != dst
        {
            dst = (dst + 1) % nNodes;
        }

        // Unique UDP port for each flow (9000 + flow_id)
        uint16_t port = 9000 + f;
        
        // Install UDP receiver (server) on destination node
        UdpServerHelper server(port);
        ApplicationContainer serverApp = server.Install(nodes.Get(dst));
        serverApp.Start(Seconds(1.0));
        serverApp.Stop(Seconds(simTime - 1.0));
        allApps.Add(serverApp);

        // Install UDP sender (OnOff client) on source node
        Address remoteAddr(InetSocketAddress(mainIfaces.GetAddress(dst), port));
        OnOffHelper onoff("ns3::UdpSocketFactory", remoteAddr);

        // Calculate data rate in bits per second
        const uint64_t bps = static_cast<uint64_t>(pps) * packetSize * 8ULL;
        onoff.SetAttribute("DataRate", DataRateValue(DataRate(bps)));
        onoff.SetAttribute("PacketSize", UintegerValue(packetSize));
        onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));  // Always on
        onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]")); // Never off

        ApplicationContainer clientApp = onoff.Install(nodes.Get(src));
        clientApp.Start(Seconds(appStart + 0.02 * f));  // Stagger start by 0.02s per flow to avoid transients
        clientApp.Stop(Seconds(simTime - 1.0));
        allApps.Add(clientApp);
    }

    // ============ FLOW MONITORING & SIMULATION ============
    // Install flow monitor to track per-flow statistics (throughput, delay, PDR, etc.)
    FlowMonitorHelper fmHelper;
    Ptr<FlowMonitor> monitor = fmHelper.InstallAll();

    // Set simulation end time and run the discrete-event simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // ============ METRICS COLLECTION & AGGREGATION ============
    // Aggregate flow statistics across all monitored flows
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(fmHelper.GetClassifier());

    // Initialize aggregated metrics
    uint64_t totalTxPackets = 0;    // Total packets transmitted by all flows
    uint64_t totalRxPackets = 0;    // Total packets received by all flows
    uint64_t totalLostPackets = 0;  // Total packets lost (tx - rx)
    uint64_t totalRxBytes = 0;      // Total bytes received (for throughput)
    Time totalDelay = Seconds(0.0); // Sum of end-to-end delays

    // Iterate through flow statistics and aggregate metrics
    // Filter to only include UDP flows created by this scenario (ports 9000-9000+nFlows)
    const auto stats = monitor->GetFlowStats();
    for (const auto& kv : stats)
    {
        const FlowMonitor::FlowStats& st = kv.second;
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(kv.first);
        // Only aggregate statistics from application-generated flows
        if (t.destinationPort >= 9000 && t.destinationPort < 9000 + nFlows)
        {
            totalTxPackets += st.txPackets;
            totalRxPackets += st.rxPackets;
            totalLostPackets += st.lostPackets;
            totalRxBytes += st.rxBytes;
            totalDelay += st.delaySum;
        }
    }

    // ============ METRICS CALCULATION ============
    // Compute derived metrics from aggregated flow statistics
    const double activeDuration = std::max(1.0, simTime - appStart);  // Seconds of active traffic
    
    // Throughput: bits received / active duration (Mbps)
    const double throughputMbps = (static_cast<double>(totalRxBytes) * 8.0) / (activeDuration * 1e6);
    
    // Average End-to-End Delay: total delay / number of received packets (ms)
    const double avgDelayMs = (totalRxPackets > 0) ? (1000.0 * totalDelay.GetSeconds() / static_cast<double>(totalRxPackets)) : 0.0;
    
    // Packet Delivery Ratio: received / transmitted × 100 (%)
    const double pdr = (totalTxPackets > 0) ? (100.0 * static_cast<double>(totalRxPackets) / static_cast<double>(totalTxPackets)) : 0.0;
    
    // Drop Ratio: lost / transmitted × 100 (%)
    const double dropRatio = (totalTxPackets > 0) ? (100.0 * static_cast<double>(totalLostPackets) / static_cast<double>(totalTxPackets)) : 0.0;

    // ============ ENERGY CONSUMPTION (WIRELESS ONLY) ============
    // Calculate total energy consumed by WiFi radios across all nodes
    double totalEnergyJ = 0.0;
    if (wireless)
    {
        // Energy consumed = initial energy - remaining energy
        for (const auto& src : energySources)
        {
            totalEnergyJ += (src->GetInitialEnergy() - src->GetRemainingEnergy());
        }
    }

    // ============ RESULTS OUTPUT TO CSV ============
    // Append results to CSV file for centralized result collection
    // Supports parallel execution with unique per-run output files
    
    // Check if CSV file is empty to determine if header needs to be written
    bool writeHeader = false;
    {
        std::ifstream check(resultsFile);
        writeHeader = !check.good() || (check.peek() == std::ifstream::traits_type::eof());
    }

    // Open results file in append mode
    std::ofstream out(resultsFile, std::ios::app);
    if (!out.is_open())
    {
        NS_FATAL_ERROR("Unable to open results CSV for writing");
    }

    // Write CSV header if file was empty
    if (writeHeader)
    {
        out << "NetType,Scenario,VaryingParamName,ParamValue,Throughput,Delay,PDR,DropRatio,Energy\n";
    }

    // Write metrics as single CSV row with 6 decimal precision
    out << std::fixed << std::setprecision(6)
        << netType << ","                    // Network type (wireless or wired)
        << scenario << ","                   // Scenario (baseline, attack, or defense)
        << varyingParamName << ","           // Name of swept parameter
        << paramValue << ","                 // Value of swept parameter
        << throughputMbps << ","             // Throughput (Mbps)
        << avgDelayMs << ","                 // Average delay (ms)
        << pdr << ","                        // Packet delivery ratio (%)
        << dropRatio << ","                  // Drop ratio (%)
        << totalEnergyJ << "\n";              // Energy consumption (J)

    out.close();
    
    // ============ CLEANUP ============
    Simulator::Destroy();
    return 0;
}
