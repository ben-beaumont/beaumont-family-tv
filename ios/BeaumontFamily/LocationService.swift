import Foundation
import CoreLocation
import MapKit
import UIKit

/// Watches location in the background and posts it to Supabase.
///
/// - Geofences around saved zones + significant-location-change keep us
///   waking up cheaply while the app is closed.
/// - While outside every zone we also run low-accuracy continuous updates so
///   the TV map and ETA stay fresh during a journey; they stop on arrival.
/// - Privacy: inside a zone we publish the zone's centre and name (no heading
///   or speed), so the TV shows "School", not exactly where you are in it.
@MainActor
final class LocationService: NSObject, ObservableObject, CLLocationManagerDelegate {
    @Published var authorization: CLAuthorizationStatus = .notDetermined
    @Published var lastZone: String?
    @Published var lastEta: Int?
    @Published var lastUpload: Date?
    @Published var lastError: String?
    @Published var lastLocation: CLLocation?

    var zones: ZoneStore
    var personName: String { UserDefaults.standard.string(forKey: "personName") ?? "" }

    private let manager = CLLocationManager()
    private var lastPushedZone: String??  // double optional: "never pushed" vs "pushed nil"
    private var lastPushAt = Date.distantPast
    private var pushing = false

    init(zones: ZoneStore) {
        self.zones = zones
        super.init()
        manager.delegate = self
        manager.desiredAccuracy = kCLLocationAccuracyHundredMeters
        manager.distanceFilter = 100
        manager.pausesLocationUpdatesAutomatically = true
        manager.activityType = .automotiveNavigation
        authorization = manager.authorizationStatus
    }

    // MARK: - control

    var authorizationDescription: String {
        switch authorization {
        case .notDetermined: return "not asked yet"
        case .restricted: return "restricted"
        case .denied: return "denied (Never)"
        case .authorizedWhenInUse: return "While Using (needs Always)"
        case .authorizedAlways: return "Always"
        @unknown default: return "unknown"
        }
    }

    func requestPermission() {
        switch manager.authorizationStatus {
        case .notDetermined: manager.requestWhenInUseAuthorization()
        case .authorizedWhenInUse: manager.requestAlwaysAuthorization()
        default: break
        }
    }

    func start() {
        guard manager.authorizationStatus == .authorizedAlways ||
              manager.authorizationStatus == .authorizedWhenInUse else { return }
        manager.allowsBackgroundLocationUpdates = manager.authorizationStatus == .authorizedAlways
        manager.showsBackgroundLocationIndicator = false
        manager.startMonitoringSignificantLocationChanges()
        refreshGeofences()
        manager.requestLocation()
    }

    func stop() {
        manager.stopMonitoringSignificantLocationChanges()
        manager.stopUpdatingLocation()
        manager.monitoredRegions.forEach { manager.stopMonitoring(for: $0) }
    }

    func refreshGeofences() {
        manager.monitoredRegions.forEach { manager.stopMonitoring(for: $0) }
        for z in zones.all {
            let r = CLCircularRegion(center: z.coordinate, radius: z.radius, identifier: z.id.uuidString)
            r.notifyOnEntry = true
            r.notifyOnExit = true
            manager.startMonitoring(for: r)
        }
    }

    func pushNow() { manager.requestLocation() }

    // MARK: - CLLocationManagerDelegate

    nonisolated func locationManagerDidChangeAuthorization(_ m: CLLocationManager) {
        Task { @MainActor in
            self.authorization = m.authorizationStatus
            // iOS only offers "Always" in Settings once the app has asked for it,
            // so ask straight after "While Using" is granted.
            if m.authorizationStatus == .authorizedWhenInUse { m.requestAlwaysAuthorization() }
            self.start()
        }
    }

    nonisolated func locationManager(_ m: CLLocationManager, didUpdateLocations locs: [CLLocation]) {
        guard let loc = locs.last else { return }
        Task { @MainActor in await self.handle(loc) }
    }

    nonisolated func locationManager(_ m: CLLocationManager, didEnterRegion r: CLRegion) { Task { @MainActor in self.pushNow() } }
    nonisolated func locationManager(_ m: CLLocationManager, didExitRegion r: CLRegion) { Task { @MainActor in self.pushNow() } }

    nonisolated func locationManager(_ m: CLLocationManager, didFailWithError error: Error) {
        Task { @MainActor in self.lastError = error.localizedDescription }
    }

    // MARK: - handling a fix

    private func zone(for loc: CLLocation) -> Zone? {
        zones.all
            .map { ($0, CLLocation(latitude: $0.lat, longitude: $0.lng).distance(from: loc)) }
            .filter { $0.1 <= $0.0.radius }
            .min { $0.1 < $1.1 }?.0
    }

    private func handle(_ loc: CLLocation) async {
        lastLocation = loc
        let current = zone(for: loc)

        // Continuous updates only while travelling.
        if current == nil { manager.startUpdatingLocation() } else { manager.stopUpdatingLocation() }

        // Throttle, but always push straight away on a zone change.
        let zoneChanged = lastPushedZone == nil || lastPushedZone! != current?.name
        guard zoneChanged || Date().timeIntervalSince(lastPushAt) > 60, !pushing else { return }
        pushing = true
        defer { pushing = false }

        let bg = UIApplication.shared.beginBackgroundTask(withName: "push-location")
        defer { UIApplication.shared.endBackgroundTask(bg) }

        let eta = await etaHomeMinutes(from: loc, in: current)
        let moving = current == nil && loc.speed > 0.5 && loc.course >= 0
        let name = personName
        let payload: (String) -> LocationPayload = { userId in
            LocationPayload(
                userId: userId, person: name,
                lat: current?.lat ?? loc.coordinate.latitude,
                lng: current?.lng ?? loc.coordinate.longitude,
                heading: moving ? loc.course : nil,
                speedMps: moving ? loc.speed : nil,
                zone: current?.name,
                etaHomeMinutes: eta,
                updatedAt: Date())
        }
        do {
            try await SupabaseClient.shared.upload(payload)
            lastPushedZone = .some(current?.name)
            lastPushAt = Date()
            lastUpload = Date()
            lastZone = current?.name
            lastEta = eta
            lastError = nil
        } catch {
            lastError = error.localizedDescription
        }
    }

    private func etaHomeMinutes(from loc: CLLocation, in zone: Zone?) async -> Int? {
        if zone?.name == ZoneStore.homeName { return 0 }
        guard let home = zones.home else { return nil }
        let req = MKDirections.Request()
        req.source = MKMapItem(placemark: MKPlacemark(coordinate: loc.coordinate))
        req.destination = MKMapItem(placemark: MKPlacemark(coordinate: home.coordinate))
        req.transportType = .automobile
        req.departureDate = Date()
        do {
            let r = try await MKDirections(request: req).calculateETA()
            return max(1, Int((r.expectedTravelTime / 60).rounded()))
        } catch {
            return nil
        }
    }
}
