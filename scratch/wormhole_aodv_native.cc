/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Wormhole Attack Detection using Modified AODV Protocol
 *
 * This version uses the built-in AODV wormhole attack and detection features
 * instead of overlay P2P links and custom UDP applications.
 *
 * AODV handles wormhole generation and RTT-based detection at the routing layer.
 */

#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace ns3;
using namespace ns3::aodv;

NS_LOG_COMPONENT_DEFINE("WormholeAodvNative");

static const uint16_t kBasePort = 9000;
static const double kMuMilliseconds = 2.5; // Detection threshold µ in milliseconds
static const std::string kOutputDir = "output";

class SimpleUdpServer : public Application
{
  public:
    static TypeId GetTypeId();
    SimpleUdpServer();
    void Setup(uint16_t port);

  private:
    virtual void StartApplication() override;
    virtual void StopApplication() override;
    void HandleRead(Ptr<Socket> socket);

    Ptr<Socket> m_socket;
    uint16_t m_port;
};

TypeId
SimpleUdpServer::GetTypeId()
{
    static TypeId tid = TypeId("ns3::SimpleUdpServer")
                            .SetParent<Application>()
                            .SetGroupName("Applications")
                            .AddConstructor<SimpleUdpServer>();
    return tid;
}

SimpleUdpServer::SimpleUdpServer()
    : m_socket(nullptr),
      m_port(0)
{
}

void
SimpleUdpServer::Setup(uint16_t port)
{
    m_port = port;
}

void
SimpleUdpServer::StartApplication()
{
    if (!m_socket)
    {
        m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_port);
        m_socket->Bind(local);
        m_socket->SetRecvCallback(MakeCallback(&SimpleUdpServer::HandleRead, this));
    }
}

void
SimpleUdpServer::StopApplication()
{
    if (m_socket)
    {
        m_socket->Close();
        m_socket = nullptr;
    }
}

void
SimpleUdpServer::HandleRead(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from)))
    {
        if (packet->GetSize() > 0)
        {
            socket->SendTo(packet, 0, from);
        }
    }
}

class SimpleUdpClient : public Application
{
  public:
    static TypeId GetTypeId();
    SimpleUdpClient();

    void Setup(Ipv4Address remoteAddress,
               uint16_t remotePort,
               Time interval,
               uint32_t packetSize,
               uint32_t maxPackets);

  private:
    virtual void StartApplication() override;
    virtual void StopApplication() override;
    void ScheduleTransmit(Time dt);
    void SendPacket();
    void HandleRead(Ptr<Socket> socket);

    Ptr<Socket> m_socket;
    Ipv4Address m_remoteAddress;
    uint16_t m_remotePort;
    EventId m_sendEvent;
    Time m_interval;
    uint32_t m_packetSize;
    uint32_t m_maxPackets;
    uint32_t m_packetsSent;
};

TypeId
SimpleUdpClient::GetTypeId()
{
    static TypeId tid = TypeId("ns3::SimpleUdpClient")
                            .SetParent<Application>()
                            .SetGroupName("Applications")
                            .AddConstructor<SimpleUdpClient>();
    return tid;
}

SimpleUdpClient::SimpleUdpClient()
    : m_socket(nullptr),
      m_remotePort(0),
      m_sendEvent(),
      m_interval(Seconds(0.1)),
      m_packetSize(64),
      m_maxPackets(1000),
      m_packetsSent(0)
{
}

void
SimpleUdpClient::Setup(Ipv4Address remoteAddress,
                       uint16_t remotePort,
                       Time interval,
                       uint32_t packetSize,
                       uint32_t maxPackets)
{
    m_remoteAddress = remoteAddress;
    m_remotePort = remotePort;
    m_interval = interval;
    m_packetSize = packetSize;
    m_maxPackets = maxPackets;
}

void
SimpleUdpClient::StartApplication()
{
    if (!m_socket)
    {
        m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        m_socket->Bind();
        m_socket->Connect(InetSocketAddress(m_remoteAddress, m_remotePort));
        m_socket->SetRecvCallback(MakeCallback(&SimpleUdpClient::HandleRead, this));
    }
    m_packetsSent = 0;
    SendPacket();
}

void
SimpleUdpClient::StopApplication()
{
    if (m_sendEvent.IsRunning())
    {
        Simulator::Cancel(m_sendEvent);
    }
    if (m_socket)
    {
        m_socket->Close();
        m_socket = nullptr;
    }
}

void
SimpleUdpClient::ScheduleTransmit(Time dt)
{
    m_sendEvent = Simulator::Schedule(dt, &SimpleUdpClient::SendPacket, this);
}

void
SimpleUdpClient::SendPacket()
{
    if (m_packetsSent >= m_maxPackets)
    {
        return;
    }
    Ptr<Packet> packet = Create<Packet>(m_packetSize);
    m_socket->Send(packet);
    ++m_packetsSent;
    ScheduleTransmit(m_interval);
}

void
SimpleUdpClient::HandleRead(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    while ((packet = socket->Recv()))
    {
        // Echo received
    }
}

int
main(int argc, char* argv[])
{
    CommandLine cmd;
    double simulationTimeSeconds = 30.0;
    bool enableWormhole = true;
    bool enableDetection = true;
    cmd.AddValue("SimulationTime", "Simulation duration in seconds", simulationTimeSeconds);
    cmd.AddValue("EnableWormhole", "Enable wormhole attack", enableWormhole);
    cmd.AddValue("EnableDetection", "Enable wormhole detection", enableDetection);
    cmd.Parse(argc, argv);

    NodeContainer nodes;
    nodes.Create(5);

    MobilityHelper mobility;
    Ptr<PositionAllocator> positionAlloc = CreateObject<RandomRectanglePositionAllocator>();
    positionAlloc->SetAttribute("X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=300.0]"));
    positionAlloc->SetAttribute("Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=300.0]"));

    mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                              "Speed",
                              StringValue("ns3::UniformRandomVariable[Min=1.0|Max=5.0]"),
                              "Pause",
                              StringValue("ns3::ConstantRandomVariable[Constant=2.0]"),
                              "PositionAllocator",
                              PointerValue(positionAlloc));
    mobility.SetPositionAllocator(positionAlloc);
    mobility.Install(nodes);

    // Wi-Fi setup
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("DsssRate11Mbps"),
                                 "ControlMode",
                                 StringValue("DsssRate11Mbps"));

    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel");

    YansWifiPhyHelper wifiPhy;
    wifiPhy.SetChannel(wifiChannel.Create());

    WifiMacHelper wifiMac;
    wifiMac.SetType("ns3::AdhocWifiMac");

    NetDeviceContainer wifiDevices = wifi.Install(wifiPhy, wifiMac, nodes);

    // AODV routing setup with wormhole attack
    AodvHelper aodv;
    aodv.Set("EnableWrmAttack", BooleanValue(enableWormhole));
    aodv.Set("EnableRttWormholeDetect", BooleanValue(enableDetection));
    aodv.Set("EnableSecondProbeVerification", BooleanValue(true));
    aodv.Set("WormholeMu", DoubleValue(kMuMilliseconds));
    aodv.Set("WormholePd", TimeValue(MilliSeconds(1)));
    aodv.Set("WormholeCtrlPacketBytes", UintegerValue(64));
    aodv.Set("WormholeLinkRateBps", DoubleValue(11000000.0)); // 11 Mbps

    InternetStackHelper internet;
    internet.SetRoutingHelper(aodv);
    internet.Install(nodes);

    // Assign IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.0.0", "255.255.255.0");
    Ipv4InterfaceContainer wifiInterfaces = address.Assign(wifiDevices);

    // Configure wormhole tunnel endpoints in AODV
    // Tunnel between nodes 0 and 1
    if (enableWormhole)
    {
        Ptr<Ipv4> ipv4_node0 = nodes.Get(0)->GetObject<Ipv4>();
        Ptr<Ipv4> ipv4_node1 = nodes.Get(1)->GetObject<Ipv4>();
        Ptr<Ipv4RoutingProtocol> rp0 = ipv4_node0->GetRoutingProtocol();
        Ptr<Ipv4RoutingProtocol> rp1 = ipv4_node1->GetRoutingProtocol();

        // Node 0 end of tunnel
        Ptr<aodv::RoutingProtocol> aodvNode0 = DynamicCast<aodv::RoutingProtocol>(rp0);
        if (aodvNode0)
        {
            aodvNode0->SetWrmAttackEnable(enableWormhole);
            aodvNode0->FirstEndOfWormTunnel = wifiInterfaces.GetAddress(0);
            aodvNode0->SecondEndOfWormTunnel = wifiInterfaces.GetAddress(1);
            NS_LOG_INFO("Node 0 wormhole tunnel: " << aodvNode0->FirstEndOfWormTunnel << " <-> "
                                                   << aodvNode0->SecondEndOfWormTunnel);
        }

        // Node 1 end of tunnel
        Ptr<aodv::RoutingProtocol> aodvNode1 = DynamicCast<aodv::RoutingProtocol>(rp1);
        if (aodvNode1)
        {
            aodvNode1->SetWrmAttackEnable(enableWormhole);
            aodvNode1->FirstEndOfWormTunnel = wifiInterfaces.GetAddress(1);
            aodvNode1->SecondEndOfWormTunnel = wifiInterfaces.GetAddress(0);
            NS_LOG_INFO("Node 1 wormhole tunnel: " << aodvNode1->FirstEndOfWormTunnel << " <-> "
                                                   << aodvNode1->SecondEndOfWormTunnel);
        }

        // Enable detection on all nodes
        if (enableDetection)
        {
            for (uint32_t i = 0; i < nodes.GetN(); ++i)
            {
                Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
                Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol();
                Ptr<aodv::RoutingProtocol> aodvProto = DynamicCast<aodv::RoutingProtocol>(rp);
                if (aodvProto)
                {
                    aodvProto->SetRttWormholeDetectEnable(true);
                    NS_LOG_INFO("Node " << i << " wormhole detection enabled");
                }
            }
        }
    }

    // Create traffic flows
    std::vector<std::pair<uint32_t, uint32_t>> flows = {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 0}};

    uint16_t port = kBasePort;
    for (const auto& flow : flows)
    {
        uint32_t source = flow.first;
        uint32_t destination = flow.second;

        // Echo server on destination
        Ptr<Node> dstNode = nodes.Get(destination);
        Ptr<SimpleUdpServer> echoServer = CreateObject<SimpleUdpServer>();
        echoServer->Setup(port);
        dstNode->AddApplication(echoServer);
        echoServer->SetStartTime(Seconds(1.0));
        echoServer->SetStopTime(Seconds(simulationTimeSeconds + 1.0));

        // UDP client on source
        Ptr<Node> srcNode = nodes.Get(source);
        Ipv4Address remoteAddress = wifiInterfaces.GetAddress(destination);
        Ptr<SimpleUdpClient> echoClient = CreateObject<SimpleUdpClient>();
        echoClient->Setup(remoteAddress,
                          port,
                          MilliSeconds(100),
                          64,
                          static_cast<uint32_t>(simulationTimeSeconds * 10));
        srcNode->AddApplication(echoClient);
        echoClient->SetStartTime(Seconds(2.0));
        echoClient->SetStopTime(Seconds(simulationTimeSeconds + 1.0));

        ++port;
    }

    // Install FlowMonitor
    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

    Simulator::Stop(Seconds(simulationTimeSeconds + 2.0));
    Simulator::Run();

    // Process FlowMonitor results
    monitor->CheckForLostPackets();

    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    std::map<Ipv4Address, uint32_t> addressToNode;
    for (uint32_t idx = 0; idx < nodes.GetN(); ++idx)
    {
        Ptr<Ipv4> ipv4 = nodes.Get(idx)->GetObject<Ipv4>();
        for (uint32_t iface = 0; iface < ipv4->GetNInterfaces(); ++iface)
        {
            for (uint32_t addrIdx = 0; addrIdx < ipv4->GetNAddresses(iface); ++addrIdx)
            {
                Ipv4Address addr = ipv4->GetAddress(iface, addrIdx).GetLocal();
                if (addr != Ipv4Address::GetLoopback())
                {
                    addressToNode[addr] = idx;
                }
            }
        }
    }

    std::ofstream metricsFile;
    std::system("mkdir -p output");
    metricsFile.open("output/wormhole_aodv_metrics.csv", std::ios::out);
    metricsFile << "FlowId,SrcNode,DestNode,ThroughputKbps,GoodputKbps\n";

    std::map<uint32_t, std::vector<double>> throughputByNode;
    std::map<uint32_t, std::vector<double>> goodputByNode;

    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    for (const auto& flow : stats)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
        uint32_t srcNodeId = addressToNode[t.sourceAddress];
        uint32_t dstNodeId = addressToNode[t.destinationAddress];

        Time duration = flow.second.timeLastRxPacket - flow.second.timeFirstTxPacket;
        double throughputKbps = 0.0;
        double goodputKbps = 0.0;
        if (duration.GetSeconds() > 0)
        {
            //   throughputKbps = flow.second.txBytes * 8.0 / duration.GetSeconds() / 1024.0;
            //   goodputKbps = flow.second.rxBytes * 8.0 / duration.GetSeconds() / 1024.0;
            // Fixed_code_For_Loss_Minimization
            // Throughput = all bits transmitted (including any retransmissions at link layer)
            throughputKbps = flow.second.txBytes * 8.0 / duration.GetSeconds() / 1024.0;

            // Goodput = application-layer data successfully received (accounting for packet loss)
            // Better metric: use received packets instead of bytes to separate network efficiency
            double rgoodputKbps = flow.second.rxBytes * 8.0 / duration.GetSeconds() / 1024.0;

            // Goodput = rxBytes (actual received data), Throughput = txBytes (sent data)
            // When packet loss occurs, goodput < throughput
            goodputKbps = rgoodputKbps;
            // Fixed_code_For_Loss_Minimization
        }

        throughputByNode[srcNodeId].push_back(throughputKbps);
        goodputByNode[srcNodeId].push_back(goodputKbps);

        metricsFile << flow.first << "," << srcNodeId << "," << dstNodeId << "," << throughputKbps
                    << "," << goodputKbps << "\n";
    }

    metricsFile.close();

    // Aggregate metrics by node
    std::ofstream summary("output/wormhole_aodv_metrics_summary.csv", std::ios::out);
    summary << "NodeId,AverageThroughputKbps,AverageGoodputKbps\n";
    for (uint32_t nodeId = 0; nodeId < nodes.GetN(); ++nodeId)
    {
        double averageThroughput = 0.0;
        double averageGoodput = 0.0;
        if (!throughputByNode[nodeId].empty())
        {
            for (double v : throughputByNode[nodeId])
            {
                averageThroughput += v;
            }
            averageThroughput /= throughputByNode[nodeId].size();
        }
        if (!goodputByNode[nodeId].empty())
        {
            for (double v : goodputByNode[nodeId])
            {
                averageGoodput += v;
            }
            averageGoodput /= goodputByNode[nodeId].size();
        }
        summary << nodeId << "," << averageThroughput << "," << averageGoodput << "\n";
    }

    summary.close();
    monitor->SerializeToXmlFile("output/wormhole_aodv_flowmon.xml", true, true);

    Simulator::Destroy();
    return 0;
}
