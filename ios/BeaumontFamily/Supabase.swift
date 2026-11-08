import Foundation
import Security

/// Minimal Supabase client: email/password auth + upsert of the user's own
/// `locations` row. Plain REST, so there is no package dependency.
struct AuthSession: Codable {
    var accessToken: String
    var refreshToken: String
    var expiresAt: Date
    var userId: String
}

struct LocationPayload {
    var userId: String
    var person: String
    var lat: Double?
    var lng: Double?
    var heading: Double?
    var speedMps: Double?
    var zone: String?
    var etaHomeMinutes: Int?
    var updatedAt: Date
}

extension LocationPayload: Encodable {
    enum CodingKeys: String, CodingKey {
        case userId = "user_id", person, lat, lng, heading
        case speedMps = "speed_mps", zone
        case etaHomeMinutes = "eta_home_minutes", updatedAt = "updated_at"
    }

    // Explicit nulls: an upsert that omits a key would keep the stale old value.
    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(userId, forKey: .userId)
        try c.encode(person, forKey: .person)
        try c.encode(lat, forKey: .lat)
        try c.encode(lng, forKey: .lng)
        try c.encode(heading, forKey: .heading)
        try c.encode(speedMps, forKey: .speedMps)
        try c.encode(zone, forKey: .zone)
        try c.encode(etaHomeMinutes, forKey: .etaHomeMinutes)
        try c.encode(ISO8601DateFormatter().string(from: updatedAt), forKey: .updatedAt)
    }
}

enum SupabaseError: LocalizedError {
    case notConfigured, notSignedIn, http(Int, String)
    var errorDescription: String? {
        switch self {
        case .notConfigured: return "Supabase URL/key missing - check Secrets.xcconfig."
        case .notSignedIn: return "Not signed in."
        case .http(let code, let body): return "Server said \(code): \(body)"
        }
    }
}

actor SupabaseClient {
    static let shared = SupabaseClient()

    private let baseURL: URL?
    private let anonKey: String
    private var session: AuthSession?

    init() {
        let info = Bundle.main.infoDictionary ?? [:]
        baseURL = (info["SupabaseURL"] as? String).flatMap { URL(string: $0) }
        anonKey = info["SupabaseAnonKey"] as? String ?? ""
        session = Keychain.load()
    }

    var isSignedIn: Bool { session != nil }

    func signIn(email: String, password: String) async throws {
        let body = try JSONSerialization.data(withJSONObject: ["email": email, "password": password])
        try await tokenRequest(query: "grant_type=password", body: body)
    }

    func signOut() {
        session = nil
        Keychain.delete()
    }

    func upload(_ build: (String) -> LocationPayload) async throws {
        let token = try await validToken()
        guard let base = baseURL, let userId = session?.userId else { throw SupabaseError.notSignedIn }
        let payload = build(userId)

        var req = URLRequest(url: base.appendingPathComponent("rest/v1/locations"))
        req.url = URL(string: req.url!.absoluteString + "?on_conflict=user_id")
        req.httpMethod = "POST"
        req.setValue(anonKey, forHTTPHeaderField: "apikey")
        req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.setValue("resolution=merge-duplicates,return=minimal", forHTTPHeaderField: "Prefer")
        req.httpBody = try JSONEncoder().encode(payload)
        _ = try await send(req)
    }

    // MARK: - internals

    private func validToken() async throws -> String {
        guard let s = session else { throw SupabaseError.notSignedIn }
        if s.expiresAt.timeIntervalSinceNow > 60 { return s.accessToken }
        let body = try JSONSerialization.data(withJSONObject: ["refresh_token": s.refreshToken])
        try await tokenRequest(query: "grant_type=refresh_token", body: body)
        guard let fresh = session else { throw SupabaseError.notSignedIn }
        return fresh.accessToken
    }

    private func tokenRequest(query: String, body: Data) async throws {
        guard let base = baseURL, !anonKey.isEmpty else { throw SupabaseError.notConfigured }
        var req = URLRequest(url: URL(string: base.absoluteString + "/auth/v1/token?" + query)!)
        req.httpMethod = "POST"
        req.setValue(anonKey, forHTTPHeaderField: "apikey")
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.httpBody = body
        let data = try await send(req)

        struct Resp: Decodable {
            let access_token: String
            let refresh_token: String
            let expires_in: Double
            struct User: Decodable { let id: String }
            let user: User
        }
        let r = try JSONDecoder().decode(Resp.self, from: data)
        let s = AuthSession(accessToken: r.access_token, refreshToken: r.refresh_token,
                            expiresAt: Date().addingTimeInterval(r.expires_in), userId: r.user.id)
        session = s
        Keychain.save(s)
    }

    private func send(_ req: URLRequest) async throws -> Data {
        let (data, resp) = try await URLSession.shared.data(for: req)
        let code = (resp as? HTTPURLResponse)?.statusCode ?? 0
        guard (200..<300).contains(code) else {
            // A rejected refresh token means we must sign in again.
            if code == 400 || code == 401, req.url?.path.contains("auth/v1/token") == true { signOut() }
            throw SupabaseError.http(code, String(data: data, encoding: .utf8) ?? "")
        }
        return data
    }
}

/// Stores the session in the Keychain (available after first unlock, so
/// background location wake-ups can still upload).
enum Keychain {
    private static let query: [String: Any] = [
        kSecClass as String: kSecClassGenericPassword,
        kSecAttrService as String: "com.beaumont.familytv.session",
    ]

    static func save(_ s: AuthSession) {
        guard let data = try? JSONEncoder().encode(s) else { return }
        delete()
        var q = query
        q[kSecValueData as String] = data
        q[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        SecItemAdd(q as CFDictionary, nil)
    }

    static func load() -> AuthSession? {
        var q = query
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var out: AnyObject?
        guard SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess,
              let data = out as? Data else { return nil }
        return try? JSONDecoder().decode(AuthSession.self, from: data)
    }

    static func delete() { SecItemDelete(query as CFDictionary) }
}
