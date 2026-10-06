// Read-only macOS diagnostic: swift tools/scan_advertisements.swift [seconds]
// Never connects, pairs, or writes to a trainer. Only VOLTRA advertisements are printed.
import Foundation
import CoreBluetooth
class Scan: NSObject, CBCentralManagerDelegate {
 var manager: CBCentralManager!
 var seen: [UUID:String] = [:]
 override init() { super.init(); manager = CBCentralManager(delegate:self, queue:nil) }
 func centralManagerDidUpdateState(_ central: CBCentralManager) {
  print("Bluetooth state: \(central.state.rawValue)")
  if central.state == .poweredOn { central.scanForPeripherals(withServices:nil, options:[CBCentralManagerScanOptionAllowDuplicatesKey:true]) }
 }
 func centralManager(_ central:CBCentralManager, didDiscover peripheral:CBPeripheral, advertisementData:[String:Any], rssi RSSI:NSNumber) {
  let name = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? peripheral.name ?? ""
  let uuids = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []
  guard name.hasPrefix("VTR-") || uuids.contains(CBUUID(string:"E4DADA34-0867-8783-9F70-2CA29216C7E4")) else { return }
  let fields = advertisementData.keys.filter { !$0.contains("Timestamp") && !$0.contains("PHY") }.sorted().map { key -> String in
   if let data = advertisementData[key] as? Data { return "\(key)=\(data.map{String(format:"%02x",$0)}.joined())" }
   if let data = advertisementData[key] as? [CBUUID:Data] { return "\(key)=\(data.map{"\($0.key):\($0.value.map{String(format:"%02x",$0)}.joined())"}.sorted())" }
   return "\(key)=\(advertisementData[key]!)"
  }.joined(separator:" ")
  if seen[peripheral.identifier] != fields { seen[peripheral.identifier]=fields; print("\(name) \(peripheral.identifier) \(fields)") }
 }
}
let scan = Scan()
let seconds = CommandLine.arguments.dropFirst().first.flatMap(Double.init) ?? 15
RunLoop.main.run(until:Date().addingTimeInterval(min(60, max(1, seconds))))
scan.manager.stopScan()
