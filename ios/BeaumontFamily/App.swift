import SwiftUI
import CoreLocation

@main
struct BeaumontFamilyApp: App {
    @StateObject private var zones: ZoneStore
    @StateObject private var location: LocationService

    init() {
        let z = ZoneStore()
        _zones = StateObject(wrappedValue: z)
        _location = StateObject(wrappedValue: LocationService(zones: z))
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(zones)
                .environmentObject(location)
                .onAppear { location.start() }
        }
    }
}

struct ContentView: View {
    @EnvironmentObject var zones: ZoneStore
    @EnvironmentObject var location: LocationService

    @State private var signedIn = false
    @State private var email = ""
    @State private var password = ""
    @AppStorage("personName") private var personName = ""
    @State private var newZoneName = ""
    @State private var message: String?

    var body: some View {
        NavigationStack {
            Form {
                if !signedIn { signInSection } else { statusSection; permissionSection; zonesSection }
            }
            .navigationTitle("Family TV")
            .task { signedIn = await SupabaseClient.shared.isSignedIn }
        }
    }

    private var signInSection: some View {
        Section("Sign in") {
            TextField("Your name (shown on the TV)", text: $personName)
            TextField("Email", text: $email)
                .textContentType(.username).keyboardType(.emailAddress).textInputAutocapitalization(.never)
            SecureField("Password", text: $password).textContentType(.password)
            Button("Sign in") {
                Task {
                    do {
                        try await SupabaseClient.shared.signIn(email: email, password: password)
                        password = ""
                        signedIn = true
                        location.requestPermission()
                        location.start()
                        location.pushNow()
                        message = nil
                    } catch { message = error.localizedDescription }
                }
            }.disabled(email.isEmpty || password.isEmpty || personName.isEmpty)
            if let message { Text(message).foregroundStyle(.red).font(.footnote) }
        }
    }

    private var statusSection: some View {
        Section("Status") {
            if personName.isEmpty {
                TextField("Your name (shown on the TV)", text: $personName)
                    .onSubmit { location.pushNow() }
                Text("Enter your name to start sharing.").foregroundStyle(.red).font(.footnote)
            } else {
                LabeledContent("Sharing as", value: personName)
            }
            LabeledContent("Zone", value: location.lastZone ?? "On the way / unknown")
            if let eta = location.lastEta { LabeledContent("ETA home", value: eta == 0 ? "Home" : "\(eta) min") }
            if let t = location.lastUpload { LabeledContent("Last update", value: t.formatted(date: .omitted, time: .shortened)) }
            if let e = location.lastError { Text(e).foregroundStyle(.red).font(.footnote) }
            Button("Send now") { location.pushNow() }
            Button("Sign out", role: .destructive) {
                Task {
                    await SupabaseClient.shared.signOut()
                    location.stop()
                    signedIn = false
                }
            }
        }
    }

    @ViewBuilder private var permissionSection: some View {
        if location.authorization != .authorizedAlways {
            Section("Location permission") {
                Text("Family TV needs \"Always\" location to update while the app is closed.")
                    .font(.footnote)
                Text("Current permission: \(location.authorizationDescription)").font(.footnote)
                if location.authorization == .notDetermined || location.authorization == .authorizedWhenInUse {
                    Button("Allow location") { location.requestPermission() }
                }
                Button("Open Settings") {
                    if let url = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(url) }
                }
            }
        }
    }

    private var zonesSection: some View {
        Section {
            if let home = zones.home {
                Label("\(home.name) (from config)", systemImage: "house")
            } else {
                Text("No home set - fill in HOME_LAT/HOME_LNG in Secrets.xcconfig.").font(.footnote)
            }
            ForEach(zones.custom) { z in Label(z.name, systemImage: "mappin.and.ellipse") }
                .onDelete { zones.remove(at: $0); location.refreshGeofences() }
            HStack {
                TextField("Name this place (e.g. School)", text: $newZoneName)
                Button("Save here") {
                    if let c = location.lastLocation?.coordinate {
                        zones.add(name: newZoneName, at: c)
                        newZoneName = ""
                        location.refreshGeofences()
                        location.pushNow()
                    } else { message = "No location fix yet." }
                }.disabled(newZoneName.isEmpty)
            }
        } header: { Text("Zones") } footer: {
            Text("Stand at the place and tap Save here. Inside a zone the TV shows its name, not your exact position.")
        }
    }
}
