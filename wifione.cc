#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"
#include <fstream>

using namespace ns3;

// === RSSI Logging ===
void LogRssiUAV0(Ptr<const Packet>, uint16_t, WifiTxVector, MpduInfo, SignalNoiseDbm signalNoise, uint16_t)
{
    std::ofstream rssi("rssi_uav0.csv", std::ios_base::app);
    rssi << Simulator::Now().GetSeconds() << "," << signalNoise.signal << std::endl;
    rssi.close();
}

int main(int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse(argc, argv);

    std::ofstream rssiInit("rssi_uav0.csv");
    rssiInit << "Time,RSSI(dBm)\n";
    rssiInit.close();

    NodeContainer sta, ap;
    sta.Create(1);
    ap.Create(1);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(sta);
    mobility.Install(ap);
    sta.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(10.0, 10.0, 0.0));
    ap.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0.0, 0.0, 0.0));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager");

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiMacHelper mac;
    Ssid ssid = Ssid("ns3-80211ax");

    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), "ActiveProbing", BooleanValue(false));
    NetDeviceContainer staDev = wifi.Install(phy, mac, sta);

    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDev = wifi.Install(phy, mac, ap);

    InternetStackHelper stack;
    stack.Install(sta);
    stack.Install(ap);

    Ipv4AddressHelper address;
    address.SetBase("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer staInt = address.Assign(staDev);
    Ipv4InterfaceContainer apInt = address.Assign(apDev);

    OnOffHelper onoff("ns3::UdpSocketFactory", InetSocketAddress(apInt.GetAddress(0), 9));
    onoff.SetAttribute("DataRate", StringValue("5Mbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(1000));
    onoff.SetAttribute("StartTime", TimeValue(Seconds(1.0)));
    onoff.SetAttribute("StopTime", TimeValue(Seconds(10.0)));
    onoff.Install(sta);

    AnimationInterface anim("wifi6_trace.xml");
    anim.SetConstantPosition(sta.Get(0), 10.0, 10.0);
    anim.SetConstantPosition(ap.Get(0), 0.0, 0.0);

    Config::ConnectWithoutContext("/NodeList/0/DeviceList/0/Phy/MonitorSnifferRx", MakeCallback(&LogRssiUAV0));

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(11.0));
    Simulator::Run();

    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    auto stats = monitor->GetFlowStats();
    std::ofstream log("wifi6_metrics.csv");
    log << "FlowID,Source,Dest,Throughput(Mbps),Delay(s),Jitter(s),QoS\n";

    for (const auto& flow : stats)
    {
        auto t = classifier->FindFlow(flow.first);
        double time = flow.second.timeLastRxPacket.GetSeconds() - flow.second.timeFirstTxPacket.GetSeconds();
        double throughput = (flow.second.rxBytes * 8.0) / (time * 1e6);
        double delay = flow.second.rxPackets ? flow.second.delaySum.GetSeconds() / flow.second.rxPackets : 0;
        double jitter = flow.second.rxPackets ? flow.second.jitterSum.GetSeconds() / flow.second.rxPackets : 0;
        double qos = throughput / 5.0; // 5 Mbps demand

        log << flow.first << "," << t.sourceAddress << "," << t.destinationAddress << ","
            << throughput << "," << delay << "," << jitter << "," << qos << "\n";
    }

    log.close();
    Simulator::Destroy();
    return 0;
}

