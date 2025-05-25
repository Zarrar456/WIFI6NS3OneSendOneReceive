#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/spectrum-wifi-helper.h"
#include <fstream>

using namespace ns3;

// RSSI Logging
void LogRssiUAV0(Ptr<const Packet>, uint16_t, WifiTxVector, MpduInfo, SignalNoiseDbm signalNoise, uint16_t) {
    std::ofstream rssi("rssi_uav0.csv", std::ios_base::app);
    rssi << Simulator::Now().GetSeconds() << "," << signalNoise.signal << std::endl;
    rssi.close();
}
void LogRssiUAV1(Ptr<const Packet>, uint16_t, WifiTxVector, MpduInfo, SignalNoiseDbm signalNoise, uint16_t) {
    std::ofstream rssi("rssi_uav1.csv", std::ios_base::app);
    rssi << Simulator::Now().GetSeconds() << "," << signalNoise.signal << std::endl;
    rssi.close();
}

int main(int argc, char *argv[])
{
    CommandLine cmd;
    cmd.Parse(argc, argv);

    std::ofstream rssiInit0("rssi_uav0.csv");
    rssiInit0 << "Time,RSSI(dBm)\n";
    rssiInit0.close();
    std::ofstream rssiInit1("rssi_uav1.csv");
    rssiInit1 << "Time,RSSI(dBm)\n";
    rssiInit1.close();

    NodeContainer sta, ap;
    sta.Create(2);  // 2 STAs
    ap.Create(1);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(sta);
    mobility.Install(ap);

    sta.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(1.0, 1.0, 0.0));
    sta.Get(1)->GetObject<MobilityModel>()->SetPosition(Vector(13.5, 13.5, 0.0));
    ap.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0.0, 0.0, 0.0));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ax);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("HeMcs5"),
                                 "ControlMode", StringValue("HeMcs0"));

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    Ptr<YansWifiChannel> wifiChannel = channel.Create();

    YansWifiPhyHelper phy;
    phy.SetChannel(wifiChannel);
    phy.Set("ChannelSettings", StringValue("{50, 160, BAND_5GHZ, 0}"));

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

    uint16_t port = 9;
    UdpServerHelper server(port);
    ApplicationContainer serverApp = server.Install(ap.Get(0));
    serverApp.Start(Seconds(0.0));
    serverApp.Stop(Seconds(11.0));

    // UAV0 sends 5 Mbps
    UdpClientHelper client0(apInt.GetAddress(0), port);
    client0.SetAttribute("MaxPackets", UintegerValue(100000000));
    client0.SetAttribute("Interval", TimeValue(Seconds(1472.0 * 8 / 5000000.0)));
    client0.SetAttribute("PacketSize", UintegerValue(1472));
    ApplicationContainer clientApp0 = client0.Install(sta.Get(0));
    clientApp0.Start(Seconds(1.0));
    clientApp0.Stop(Seconds(10.0));

    // UAV1 sends 50 Mbps
    UdpClientHelper client1(apInt.GetAddress(0), port);
    client1.SetAttribute("MaxPackets", UintegerValue(100000000));
    client1.SetAttribute("Interval", TimeValue(Seconds(1472.0 * 8 / 50000000.0)));
    client1.SetAttribute("PacketSize", UintegerValue(1472));
    ApplicationContainer clientApp1 = client1.Install(sta.Get(1));
    clientApp1.Start(Seconds(1.0));
    clientApp1.Stop(Seconds(10.0));

    AnimationInterface anim("wifi6_trace.xml");
    anim.SetConstantPosition(sta.Get(0), 1.0, 1.0);
    anim.SetConstantPosition(sta.Get(1), 13.5, 13.5);
    anim.SetConstantPosition(ap.Get(0), 0.0, 0.0);

    Config::ConnectWithoutContext("/NodeList/0/DeviceList/0/Phy/MonitorSnifferRx", MakeCallback(&LogRssiUAV0));
    Config::ConnectWithoutContext("/NodeList/1/DeviceList/0/Phy/MonitorSnifferRx", MakeCallback(&LogRssiUAV1));

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
          double qos = 0;
       if(t.sourceAddress == staInt.GetAddress(0)) {
        qos = throughput / 5.0;
    } else if(t.sourceAddress == staInt.GetAddress(1)) {
        qos = throughput / 50.0;
    }

        log << flow.first << "," << t.sourceAddress << "," << t.destinationAddress << ","
            << throughput << "," << delay << "," << jitter << "," << qos << "\n";
    }

    log.close();
    Simulator::Destroy();
    return 0;
}

