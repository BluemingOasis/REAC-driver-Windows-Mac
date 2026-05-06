import CoreAudio
import SwiftUI

private let defaults = UserDefaults(suiteName: "com.reac.decoder") ?? .standard

struct NetworkInterface: Identifiable, Hashable {
    let id: String
    let displayName: String
}

struct AudioOutput: Identifiable, Hashable {
    let id: AudioDeviceID
    let name: String
}

@main
struct ReacConfigApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .frame(width: 560, height: 260)
        }
    }
}

struct ContentView: View {
    @State private var interfaces: [NetworkInterface] = InterfaceProvider.listInterfaces()
    @State private var outputs: [AudioOutput] = AudioProvider.listOutputs()
    @State private var selectedInterface: String = defaults.string(forKey: "captureInterface") ?? "en0"
    @State private var selectedOutput: AudioDeviceID = AudioDeviceID(defaults.integer(forKey: "monitorOutputDeviceID"))
    @State private var statusText = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            Text("REAC Audio Settings")
                .font(.title2)

            Picker("REAC Ethernet port", selection: $selectedInterface) {
                ForEach(interfaces) { item in
                    Text(item.displayName).tag(item.id)
                }
            }

            Picker("Reaper monitor output", selection: $selectedOutput) {
                ForEach(outputs) { item in
                    Text(item.name).tag(item.id)
                }
            }

            HStack {
                Button("Refresh") {
                    interfaces = InterfaceProvider.listInterfaces()
                    outputs = AudioProvider.listOutputs()
                }

                Spacer()

                Button("Save") {
                    defaults.set(selectedInterface, forKey: "captureInterface")
                    defaults.set(Int(selectedOutput), forKey: "monitorOutputDeviceID")
                    statusText = "Saved. Restart Reaper or reselect the REAC Core Audio device."
                }
                .keyboardShortcut(.defaultAction)
            }

            Text(statusText)
                .foregroundStyle(.secondary)
        }
        .padding(24)
        .onAppear {
            if selectedOutput == 0, let first = outputs.first {
                selectedOutput = first.id
            }
        }
    }
}

enum InterfaceProvider {
    static func listInterfaces() -> [NetworkInterface] {
        var interfaces: [NetworkInterface] = []
        var pointer: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&pointer) == 0, let first = pointer else {
            return [NetworkInterface(id: "en0", displayName: "en0")]
        }
        defer { freeifaddrs(pointer) }

        var seen = Set<String>()
        for item in sequence(first: first, next: { $0.pointee.ifa_next }) {
            let name = String(cString: item.pointee.ifa_name)
            guard name.hasPrefix("en"), !seen.contains(name) else { continue }
            seen.insert(name)
            interfaces.append(NetworkInterface(id: name, displayName: name))
        }

        return interfaces.isEmpty ? [NetworkInterface(id: "en0", displayName: "en0")] : interfaces.sorted { $0.id < $1.id }
    }
}

enum AudioProvider {
    static func listOutputs() -> [AudioOutput] {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        var dataSize: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &dataSize) == noErr else {
            return []
        }

        let count = Int(dataSize) / MemoryLayout<AudioDeviceID>.size
        var devices = [AudioDeviceID](repeating: 0, count: count)
        guard AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &dataSize, &devices) == noErr else {
            return []
        }

        return devices.compactMap { device in
            guard hasOutputStreams(device), let name = deviceName(device) else { return nil }
            return AudioOutput(id: device, name: name)
        }
    }

    private static func hasOutputStreams(_ device: AudioDeviceID) -> Bool {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyStreams,
            mScope: kAudioDevicePropertyScopeOutput,
            mElement: kAudioObjectPropertyElementMain
        )
        var dataSize: UInt32 = 0
        return AudioObjectGetPropertyDataSize(device, &address, 0, nil, &dataSize) == noErr && dataSize > 0
    }

    private static func deviceName(_ device: AudioDeviceID) -> String? {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioObjectPropertyName,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        var name: CFString = "" as CFString
        var dataSize = UInt32(MemoryLayout<CFString>.size)
        let result = withUnsafeMutablePointer(to: &name) { pointer in
            AudioObjectGetPropertyData(device, &address, 0, nil, &dataSize, pointer)
        }
        return result == noErr ? name as String : nil
    }
}
