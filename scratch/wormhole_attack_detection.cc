/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <cstdlib>

#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WormholeAttackDetection");

static const uint16_t kBasePort = 9000;
static const double kMuMilliseconds = 2.5; // Detection threshold µ in milliseconds
static const std::string kOutputDir = "output";

class UdpEchoResponder : public Application
{
public:
  static TypeId GetTypeId();
  UdpEchoResponder();
  void Setup(uint16_t port);

private:
  virtual void StartApplication() override;
  virtual void StopApplication() override;
  void HandleRead(Ptr<Socket> socket);

  Ptr<Socket> m_socket;
  uint16_t m_port;
};

TypeId
UdpEchoResponder::GetTypeId()
{
  static TypeId tid = TypeId("ns3::UdpEchoResponder")
                              .SetParent<Application>()
                              .SetGroupName("Applications")
                              .AddConstructor<UdpEchoResponder>();
  return tid;
}

UdpEchoResponder::UdpEchoResponder()
    : m_socket(nullptr), m_port(0)
{
}

void
UdpEchoResponder::Setup(uint16_t port)
{
  m_port = port;
}

void
UdpEchoResponder::StartApplication()
{
  if (!m_socket)
  {
    m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_port);
    m_socket->Bind(local);
    m_socket->SetRecvCallback(MakeCallback(&UdpEchoResponder::HandleRead, this));
  }
}

void
UdpEchoResponder::StopApplication()
{
  if (m_socket)
  {
    m_socket->Close();
    m_socket = nullptr;
  }
}

void
UdpEchoResponder::HandleRead(Ptr<Socket> socket)
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

class RttProbeClient : public Application
{
public:
  static TypeId GetTypeId();
  RttProbeClient();

  void Setup(Ipv4Address remoteAddress,
             uint16_t remotePort,
             Time interval,
             uint32_t packetSize,
             uint32_t maxPackets,
             double muMilliseconds,
             const std::string& logFilename);

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
  double m_muMilliseconds;
  Time m_expectedRtt;
  std::string m_logFilename;
  std::ofstream m_logStream;
};

TypeId
RttProbeClient::GetTypeId()
{
  static TypeId tid = TypeId("ns3::RttProbeClient")
                              .SetParent<Application>()
                              .SetGroupName("Applications")
                              .AddConstructor<RttProbeClient>();
  return tid;
}

RttProbeClient::RttProbeClient()
    : m_socket(nullptr),
      m_remotePort(0),
      m_sendEvent(),
      m_interval(Seconds(0.1)),
      m_packetSize(64),
      m_maxPackets(1000),
      m_packetsSent(0),
      m_muMilliseconds(kMuMilliseconds),
      m_expectedRtt(Seconds(0.0))
{
}

void
RttProbeClient::Setup(Ipv4Address remoteAddress,
                       uint16_t remotePort,
                       Time interval,
                       uint32_t packetSize,
                       uint32_t maxPackets,
                       double muMilliseconds,
                       const std::string& logFilename)
{
  m_remoteAddress = remoteAddress;
  m_remotePort = remotePort;
  m_interval = interval;
  m_packetSize = packetSize;
  m_maxPackets = maxPackets;
  m_muMilliseconds = muMilliseconds;
  m_logFilename = logFilename;
}

void
RttProbeClient::StartApplication()
{
  if (!m_socket)
  {
    m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
    m_socket->Bind();
    m_socket->Connect(InetSocketAddress(m_remoteAddress, m_remotePort));
    m_socket->SetRecvCallback(MakeCallback(&RttProbeClient::HandleRead, this));
  }

  std::system("mkdir -p output");
  m_logStream.open(m_logFilename.c_str(), std::ios::out | std::ios::app);
  if (m_logStream.is_open())
  {
    m_logStream << "Time,OldRTT_ns,NewRTT_ns,Detection\n";
  }

  m_packetsSent = 0;
  SendPacket();
}

void
RttProbeClient::StopApplication()
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
  if (m_logStream.is_open())
  {
    m_logStream.close();
  }
}

void
RttProbeClient::ScheduleTransmit(Time dt)
{
  m_sendEvent = Simulator::Schedule(dt, &RttProbeClient::SendPacket, this);
}

void
RttProbeClient::SendPacket()
{
  if (m_packetsSent >= m_maxPackets)
  {
    return;
  }
  Ptr<Packet> packet = Create<Packet>(m_packetSize);
  TimestampTag tag;
  tag.SetTimestamp(Simulator::Now());
  packet->AddPacketTag(tag);
  m_socket->Send(packet);
  ++m_packetsSent;
  ScheduleTransmit(m_interval);
}

void
RttProbeClient::HandleRead(Ptr<Socket> socket)
{
  Ptr<Packet> packet;
  while ((packet = socket->Recv()))
  {
    TimestampTag tag;
    if (packet->PeekPacketTag(tag))
    {
      Time sendTime = tag.GetTimestamp();
      Time newRtt = Simulator::Now() - sendTime;
      Time oldRtt = m_expectedRtt.IsZero() ? newRtt : m_expectedRtt;
      Time diff = newRtt > oldRtt ? newRtt - oldRtt : oldRtt - newRtt;
      bool wormholeDetected = diff > MilliSeconds(m_muMilliseconds);
      std::string detection = wormholeDetected ? "WormholeDetected" : "NoWormhole";

      std::cout << Simulator::Now().GetSeconds() << " | "
                << oldRtt.GetNanoSeconds() << " | "
                << newRtt.GetNanoSeconds() << " | "
                << detection << std::endl;

      if (m_logStream.is_open())
      {
        m_logStream << Simulator::Now().GetSeconds() << ","
                    << oldRtt.GetNanoSeconds() << ","
                    << newRtt.GetNanoSeconds() << ","
                    << detection << "\n";
      }

      if (m_expectedRtt.IsZero())
      {
        m_expectedRtt = newRtt;
      }
      else
      {
        double oldSeconds = m_expectedRtt.GetSeconds();
        double newSeconds = newRtt.GetSeconds();
        double updated = 0.8 * oldSeconds + 0.2 * newSeconds;
        m_expectedRtt = Seconds(updated);
      }
    }
  }
}

int
main(int argc, char* argv[])
{
  CommandLine cmd;
  double simulationTimeSeconds = 30.0;
  cmd.AddValue("SimulationTime", "Simulation duration in seconds", simulationTimeSeconds);
  cmd.Parse(argc, argv);

  NodeContainer nodes;
  nodes.Create(5);

  MobilityHelper mobility;
  Ptr<PositionAllocator> positionAlloc = CreateObject<RandomRectanglePositionAllocator>();
  positionAlloc->SetAttribute("X",
                              StringValue("ns3::UniformRandomVariable[Min=0.0|Max=300.0]"));
  positionAlloc->SetAttribute("Y",
                              StringValue("ns3::UniformRandomVariable[Min=0.0|Max=300.0]"));

  mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                            "Speed",
                            StringValue("ns3::UniformRandomVariable[Min=1.0|Max=5.0]"),
                            "Pause",
                            StringValue("ns3::ConstantRandomVariable[Constant=2.0]"),
                            "PositionAllocator",
                            PointerValue(positionAlloc));
  mobility.SetPositionAllocator(positionAlloc);
  mobility.Install(nodes);

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

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("1000Mbps"));
  p2p.SetChannelAttribute("Delay", StringValue("0.1ms"));
  NetDeviceContainer wormholeDevices = p2p.Install(nodes.Get(0), nodes.Get(1));

  AodvHelper aodv;
  InternetStackHelper internet;
  internet.SetRoutingHelper(aodv);
  internet.Install(nodes);

  Ipv4AddressHelper address;
  address.SetBase("10.1.0.0", "255.255.255.0");
  Ipv4InterfaceContainer wifiInterfaces = address.Assign(wifiDevices);

  address.SetBase("10.2.0.0", "255.255.255.0");
  Ipv4InterfaceContainer wormholeInterfaces = address.Assign(wormholeDevices);

  std::vector<std::pair<uint32_t, uint32_t>> flows = {
      {0, 1},
      {1, 2},
      {2, 3},
      {3, 4},
      {4, 0}};

  uint16_t port = kBasePort;
  for (const auto& p : flows)
  {
    uint32_t source = p.first;
    uint32_t destination = p.second;

    Ptr<Node> dstNode = nodes.Get(destination);
    Ptr<UdpEchoResponder> echoServer = CreateObject<UdpEchoResponder>();
    echoServer->Setup(port);
    dstNode->AddApplication(echoServer);
    echoServer->SetStartTime(Seconds(1.0));
    echoServer->SetStopTime(Seconds(simulationTimeSeconds + 1.0));

    Ptr<Node> srcNode = nodes.Get(source);
    Ipv4Address remoteAddress = wifiInterfaces.GetAddress(destination);
    Ptr<RttProbeClient> echoClient = CreateObject<RttProbeClient>();
    std::ostringstream filename;
    filename << kOutputDir << "/wormhole_rtt_flow_" << source << "_" << destination << ".csv";
    echoClient->Setup(remoteAddress,
                      port,
                      MilliSeconds(100),
                      64,
                      static_cast<uint32_t>(simulationTimeSeconds * 10),
                      kMuMilliseconds,
                      filename.str());
    srcNode->AddApplication(echoClient);
    echoClient->SetStartTime(Seconds(2.0));
    echoClient->SetStopTime(Seconds(simulationTimeSeconds + 1.0));

    ++port;
  }

  FlowMonitorHelper flowmonHelper;
  Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

  Simulator::Stop(Seconds(simulationTimeSeconds + 2.0));
  Simulator::Run();

  monitor->CheckForLostPackets();

  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
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
  metricsFile.open("output/wormhole_metrics.csv", std::ios::out);
  metricsFile << "FlowId,SrcNode,DestNode,ThroughputKbps,GoodputKbps\n";

  std::map<uint32_t, std::vector<double>> throughputByNode;
  std::map<uint32_t, std::vector<double>> goodputByNode;

  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
  for (auto const& flow : stats)
  {
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(flow.first);
    uint32_t srcNodeId = addressToNode[t.sourceAddress];
    uint32_t dstNodeId = addressToNode[t.destinationAddress];

    Time duration = flow.second.timeLastRxPacket - flow.second.timeFirstTxPacket;
    double throughputKbps = 0.0;
    double goodputKbps = 0.0;
    if (duration.GetSeconds() > 0)
    {
      throughputKbps = flow.second.txBytes * 8.0 / duration.GetSeconds() / 1024.0;
      goodputKbps = flow.second.rxBytes * 8.0 / duration.GetSeconds() / 1024.0;
    }

    throughputByNode[srcNodeId].push_back(throughputKbps);
    goodputByNode[srcNodeId].push_back(goodputKbps);

    metricsFile << flow.first << ","
                << srcNodeId << ","
                << dstNodeId << ","
                << throughputKbps << ","
                << goodputKbps << "\n";
  }

  metricsFile.close();

  std::ofstream summary("output/wormhole_metrics_summary.csv", std::ios::out);
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
  monitor->SerializeToXmlFile("output/wormhole_flowmon.xml", true, true);

  Simulator::Destroy();
  return 0;
}
