import Foundation

/// Mirrors `~/.reminders.json` from the C++ `reminders` app.
struct ReminderRecord: Codable, Identifiable {
    var id: UInt64
    var title: String
    var description: String
    var due: String
    var done: Bool
}

struct ReminderStore: Codable {
    var t1_minutes: Int
    var t2_minutes: Int
    var bell_on_popup: Bool
    var reminders: [ReminderRecord]
}

enum StoreIO {
    static var defaultURL: URL {
        FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".reminders.json")
    }

    static func load(from url: URL = defaultURL) -> ReminderStore? {
        guard let data = try? Data(contentsOf: url) else { return nil }
        let dec = JSONDecoder()
        return try? dec.decode(ReminderStore.self, from: data)
    }
}
