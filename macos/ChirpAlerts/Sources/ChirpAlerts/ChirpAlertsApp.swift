import AppKit
import SwiftUI

@main
struct ChirpAlertsApp: App {
    @ObservedObject private var engine = ReminderEngine.shared

    init() {
        NSApplication.shared.setActivationPolicy(.accessory)
        DispatchQueue.main.async {
            ReminderEngine.shared.start()
        }
    }

    var body: some Scene {
        MenuBarExtra("CHIRP", systemImage: "bell.badge") {
            VStack(alignment: .leading, spacing: 8) {
                Text("Background check-ins")
                    .font(.headline)
                Text(engine.statusLine)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(3)
                Divider()
                HStack {
                    Text("Next T1")
                    Spacer()
                    Text(engine.nextT1).monospaced()
                }
                HStack {
                    Text("Next T2")
                    Spacer()
                    Text(engine.nextT2).monospaced()
                }
                Divider()
                Button("Test HUD + notification now") {
                    engine.sendTestAlert()
                }
                Button("Reload ~/.reminders.json") {
                    engine.reloadFromDisk()
                }
                Button("Reveal JSON in Finder") {
                    engine.revealInFinder()
                }
                Button("Open JSON in default app") {
                    engine.openJSONInEditor()
                }
                Divider()
                Button("Quit ChirpAlerts") {
                    NSApplication.shared.terminate(nil)
                }
            }
            .padding(12)
            .frame(minWidth: 260)
        }
    }
}
