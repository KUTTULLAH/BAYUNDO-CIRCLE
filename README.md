# Jirani Circle

A responsive chama dashboard with browser-only and local-backend modes.

## Prototype limitations

- The member and contribution lists start empty. In browser-only mode, admins can add active members and contributions; these are saved in this browser's local storage, not synced, backed up, or protected as production data.
- The dashboard can optionally connect to a locally running API for admin sign-in and member records. The browser keeps the session token in memory only. Backend contribution history and payment entry are not connected.
- New members become active in the roster immediately, but their account remains invited until they accept the one-time token and set a password. Share the token through a trusted channel.
- Do not enter sensitive real member or payment information until the service has been deployed and hardened for production.

## Backend foundation

- `backend/src/main.cc` implements `GET /api/v1/health` and registers login, logout, invitation acceptance, and admin member listing/creation in `backend/src/api_routes.cc`. The API listens on loopback only. Other endpoints in the API contract, including password reset, are not implemented yet.
- [`backend/schema.sql`](./backend/schema.sql) is a draft MySQL schema for chama members, individually effective-dated monthly targets, invitations, sessions, financial transactions, access requests, and audit events.
- [`backend/openapi.yaml`](./backend/openapi.yaml) is the API contract. Members read their own records; an admin can authorize one read-only retrieval of another member's records for a requested date range.
- Member accounts use phone-number sign-in and one-time activation invitations. Admin-created member records are active immediately; the invited account cannot sign in until the member redeems the token and sets a password. The API returns the token once to the admin for trusted handoff, including when provisioning another admin.
- The dashboard's backend connection accepts only `localhost` or `127.0.0.1` API URLs. The API allows browser origins served from those local hosts, not arbitrary websites; serve the dashboard at a localhost URL (not directly from `file://` or from published GitHub Pages) to connect it. Browser-only member and contribution data remain separate from backend records.
- The initial design supports one chama membership per account and KES. A secure one-time bootstrap command must create the first chama and administrator before member invitations can be issued. Public admin signup is not part of the design.
- `backend/build/jirani-circle-bootstrap-admin` creates the initial chama and administrator only when no administrator exists. It prompts for the chama name, admin name, E.164 phone, and a hidden password; it stores an Argon2id password hash and writes an audit event. Run it once from a trusted terminal after applying the schema. It uses the same `DB_HOST`, `DB_PORT`, `DB_NAME`, `DB_USER`, and `DB_PASSWORD` environment variables as the API.
- CI applies the schema to MySQL 8 and verifies the API's database readiness. The API contract has not yet been run through an OpenAPI validator.

### Build the API on Windows

Install Visual Studio Build Tools with the C++ workload, CMake, vcpkg, and MySQL 8. Set `VCPKG_ROOT` and the `DB_HOST`, `DB_PORT`, `DB_NAME`, `DB_USER`, and `DB_PASSWORD` environment variables for your database; do not commit database credentials. Then run from the repository root:

```powershell
cmake -S backend -B backend/build `
  -DVCPKG_OVERLAY_PORTS="$PWD/backend/vcpkg-overlay-ports" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build backend/build --config Release
.\backend\build\Release\jirani-circle-bootstrap-admin.exe
$env:PORT = "8080"
.\backend\build\Release\jirani-circle-api.exe
```

Check `http://localhost:8080/api/v1/health` for `{"status":"ok"}`. The endpoint returns `503` until it can run a test query against the configured database.

The **Build C++ API** GitHub Actions workflow builds the backend with Drogon's MySQL driver, applies the draft schema, verifies database readiness, and tests immediate member activation, one-time invitation acceptance, login, logout, duplicate-phone rejection, and local-only CORS against a temporary MySQL 8 service when backend or dashboard files change, on pull requests, or when started manually from the Actions tab.

## Publish with GitHub Pages

1. Push the site files to the repository's `main` branch.
2. In the repository, open **Settings → Pages** and set the build and deployment source to **GitHub Actions**.
3. Open **Actions** and confirm that **Deploy static site to GitHub Pages** succeeds.
4. Open **Settings → Pages** to find the published site URL.

The workflow deploys the contents of the repository root whenever a commit is pushed to `main`. Keep real financial and member data out of this public prototype.
