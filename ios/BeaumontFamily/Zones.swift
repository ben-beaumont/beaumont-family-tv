import Foundation
import CoreLocation

struct Zone: Codable, Identifiable, Equatable {
    var id = UUID()
    var name: String
    var lat: Double
    var lng: Double
    var radius: Double = 150  // metres

    var coordinate: CLLocationCoordinate2D { .init(latitude: lat, longitude: lng) }
}

/// Saved places. "Home" comes from Secrets.xcconfig (so the address never
/// lands in the repo); everything else is added in the app and kept on the phone.
@MainActor
final class ZoneStore: ObservableObject {
    static let homeName = "Home"
    @Published private(set) var custom: [Zone] = []
    private let key = "customZones"

    init() {
        if let data = UserDefaults.standard.data(forKey: key),
           let zones = try? JSONDecoder().decode([Zone].self, from: data) {
            custom = zones
        }
    }

    var home: Zone? {
        let info = Bundle.main.infoDictionary ?? [:]
        guard let lat = Double(info["HomeLat"] as? String ?? ""),
              let lng = Double(info["HomeLng"] as? String ?? ""),
              lat != 0 || lng != 0 else { return nil }
        return Zone(id: UUID(uuidString: "00000000-0000-0000-0000-000000000001")!,
                    name: Self.homeName, lat: lat, lng: lng, radius: 150)
    }

    /// iOS allows 20 monitored regions per app.
    var all: [Zone] { ([home].compactMap { $0 } + custom).prefix(20).map { $0 } }

    func add(name: String, at c: CLLocationCoordinate2D) {
        let trimmed = name.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty, custom.count < 19 else { return }
        custom.append(Zone(name: trimmed, lat: c.latitude, lng: c.longitude))
        save()
    }

    func remove(at offsets: IndexSet) {
        custom.remove(atOffsets: offsets)
        save()
    }

    private func save() {
        if let data = try? JSONEncoder().encode(custom) { UserDefaults.standard.set(data, forKey: key) }
    }
}
