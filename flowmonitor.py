import xml.etree.ElementTree as ET
import matplotlib.pyplot as plt

tree = ET.parse('./output/wormhole_flowmon.xml')
root = tree.getroot()

flows = []
pdrs = []

for flow in root.findall(".//FlowStats/Flow"):
    flow_id = int(flow.get("flowId"))
    tx = int(flow.get("txPackets"))
    rx = int(flow.get("rxPackets"))

    pdr = (rx / tx) * 100 if tx > 0 else 0

    flows.append(flow_id)
    pdrs.append(pdr)

plt.bar(flows, pdrs)
plt.xlabel("Flow ID")
plt.ylabel("PDR (%)")
plt.title("Packet Delivery Ratio")
plt.show()