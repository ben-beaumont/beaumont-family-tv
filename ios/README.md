# Family TV - iPhone app

SwiftUI app (iOS 17+) that posts the user's zone, position and ETA home to the
Supabase `locations` table, which the ESP32 and TV dashboard read.

## One-time setup
1. Supabase: run `../supabase/schema.sql`, create a user per family member
   (Authentication > Users), and turn off public sign-ups.
2. **Build in the cloud (no Mac needed) with Codemagic** (`../codemagic.yaml`):
   - Sign up at codemagic.io with GitHub and add this repo (push it first).
   - In App Store Connect create the app (bundle ID `com.beaumont.familytv`),
     then Users and Access > Integrations > create an API key.
   - Codemagic > Team settings > Integrations > App Store Connect: add that key,
     named `Codemagic`.
   - Codemagic > Environment variables: group `family_tv` (all Secure) with
     `SUPABASE_URL`, `SUPABASE_ANON_KEY` (anon, not service-role), `HOME_LAT`,
     `HOME_LNG`, `DEVELOPMENT_TEAM` (10-char team ID).
   - Start the `family-tv-ios` workflow. Codemagic generates the project,
     signs it, and uploads to TestFlight.
   - (Alternative with a Mac: copy `Secrets.example.xcconfig` to
     `Secrets.xcconfig`, `brew install xcodegen && xcodegen`, open in Xcode.)
3. Install the TestFlight app on each iPhone and accept the invite.
4. In the app: enter name, sign in, allow location -> then "Always" in Settings.
5. Stand at school/work etc. and tap "Save here" to add zones.
6. On the ESP32 set `SUPABASE_PROJECT_URL` and the **service-role** key in `config.h`.

## Behaviour
- Geofences + significant-change wake the app in the background; while outside
  every zone it also takes low-accuracy updates so ETA stays current.
- In a zone the TV gets the zone name and its centre (no live dot/heading).
- ETA home is computed on the phone with MapKit (driving).
