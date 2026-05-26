import AppKit
import Combine
import Foundation
import UserNotifications

@MainActor
final class ReminderEngine: ObservableObject {
    static let shared = ReminderEngine()

    @Published private(set) var statusLine: String = "Loading…"
    @Published private(set) var nextT1: String = "—"
    @Published private(set) var nextT2: String = "—"

    private var storeURL: URL = StoreIO.defaultURL
    private var store: ReminderStore?
    private var lastT1Fire = Date()
    private var lastT2Fire = Date()
    private var tickCancellable: AnyCancellable?
    private var reloadCancellable: AnyCancellable?
    private let hud = HUDPresenter()

    private init() {}

    func start() {
        reloadFromDisk()
        requestNotificationPermission()
        tickCancellable = Timer.publish(every: 1, on: .main, in: .common)
            .autoconnect()
            .sink { [weak self] _ in
                Task { @MainActor in self?.tick() }
            }
        reloadCancellable = Timer.publish(every: 8, on: .main, in: .common)
            .autoconnect()
            .sink { [weak self] _ in
                Task { @MainActor in self?.reloadFromDisk() }
            }
    }

    func reloadFromDisk() {
        storeURL = StoreIO.defaultURL
        if let s = StoreIO.load(from: storeURL) {
            store = s
            statusLine = "Using \(storeURL.path) (\(s.reminders.count) items)"
        } else {
            store = ReminderStore(t1_minutes: 60, t2_minutes: 0, bell_on_popup: true, reminders: [])
            statusLine = "No file yet — \(storeURL.path) (defaults)"
        }
        refreshCountdownLabels()
    }

    private func requestNotificationPermission() {
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .sound]) { granted, _ in
            Task { @MainActor in
                if !granted {
                    self.statusLine += " · Notifications denied (enable in System Settings)"
                }
            }
        }
    }

    private func refreshCountdownLabels() {
        guard let s = store else { return }
        let t1 = max(1, s.t1_minutes)
        let next1 = lastT1Fire.addingTimeInterval(TimeInterval(t1 * 60))
        nextT1 = Self.fmtRemaining(until: next1)

        if s.t2_minutes > 0 {
            let t2 = s.t2_minutes
            let next2 = lastT2Fire.addingTimeInterval(TimeInterval(t2 * 60))
            nextT2 = Self.fmtRemaining(until: next2)
        } else {
            nextT2 = "off"
        }
    }

    private static func fmtRemaining(until: Date) -> String {
        let s = max(0, until.timeIntervalSinceNow)
        let m = Int(s) / 60
        let sec = Int(s) % 60
        return String(format: "%02d:%02d", m, sec)
    }

    private func tick() {
        guard let s = store else { return }
        let now = Date()
        let t1m = max(1, s.t1_minutes)
        let nextT1Date = lastT1Fire.addingTimeInterval(TimeInterval(t1m * 60))
        if now >= nextT1Date {
            lastT1Fire = now
            fireCheckIn(which: "T1", bell: s.bell_on_popup)
        }

        if s.t2_minutes > 0 {
            let t2m = s.t2_minutes
            let nextT2Date = lastT2Fire.addingTimeInterval(TimeInterval(t2m * 60))
            if now >= nextT2Date {
                lastT2Fire = now
                fireCheckIn(which: "T2", bell: s.bell_on_popup)
            }
        }

        refreshCountdownLabels()
    }

    private func openTitles() -> [String] {
        guard let s = store else { return [] }
        return s.reminders.filter { !$0.done }.map(\.title)
    }

    private func fireCheckIn(which: String, bell: Bool) {
        let titles = openTitles()
        let body: String
        if titles.isEmpty {
            body = "No open reminders — you're clear!"
        } else {
            let slice = Array(titles.prefix(12))
            var b = slice.joined(separator: "\n")
            if titles.count > 12 { b += "\n…" }
            body = b
        }

        let content = UNMutableNotificationContent()
        content.title = "⏰ Reminder check-in (\(which))"
        content.body = body
        if bell {
            content.sound = .default
        }
        let req = UNNotificationRequest(identifier: UUID().uuidString, content: content, trigger: nil)
        UNUserNotificationCenter.current().add(req)

        hud.show(titles: titles)
        if bell {
            NSSound.beep()
        }
    }

    /// Menu action: fire notification + HUD immediately (for permission / layout testing).
    func sendTestAlert() {
        let bell = store?.bell_on_popup ?? true
        fireCheckIn(which: "Test", bell: bell)
    }

    func openJSONInEditor() {
        NSWorkspace.shared.open(storeURL)
    }

    func revealInFinder() {
        NSWorkspace.shared.activateFileViewerSelecting([storeURL])
    }
}
