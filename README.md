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
- The initial design supports one chama membership per account and KES. Public admin signup is not part of the design.
- The API's `POST /api/v1/admin/bootstrap` endpoint creates the first chama and administrator once. It is disabled unless `BOOTSTRAP_TOKEN` is configured with at least 32 characters. Generate a random token, set it as a secret in the API host, and send it in the `X-Bootstrap-Token` header over HTTPS with the chama name, admin name, E.164 phone, and a 12-128 character password. After receiving `201 Created`, remove `BOOTSTRAP_TOKEN` from the host and restart the API. Requests after an administrator exists return `409`; a database lock prevents concurrent bootstrap attempts. Never commit or share the bootstrap token.
- The standalone `backend/build/jirani-circle-bootstrap-admin` command remains available for trusted terminal use. It prompts for the same initial admin details and uses the API's `DB_HOST`, `DB_PORT`, `DB_NAME`, `DB_USER`, and `DB_PASSWORD` settings.
- CI applies the schema to MySQL 8 and verifies the API's database readiness. The API contract has not yet been run through an OpenAPI validator.

### Build the API on Windows

Install Visual Studio Build Tools with the C++ workload, CMake, vcpkg, and MySQL 8. Set `VCPKG_ROOT` and the `DB_HOST`, `DB_PORT`, `DB_NAME`, `DB_USER`, and `DB_PASSWORD` environment variables for your database; do not commit database credentials. Then run from the repository root:

```powershell
cmake -S backend -B backend/build `
  -DVCPKG_OVERLAY_PORTS="$PWD/backend/vcpkg-overlay-ports" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build backend/build --config Release
$env:PORT = "8080"
$env:BOOTSTRAP_TOKEN = "<set a random secret of at least 32 characters>"
.\backend\build\Release\jirani-circle-api.exe
```

Check `http://localhost:8080/api/v1/health` for `{"status":"ok"}`. After bootstrapping over HTTPS, remove `BOOTSTRAP_TOKEN` and restart the API. The health endpoint returns `503` until it can run a test query against the configured database.

The **Build C++ API** GitHub Actions workflow builds the backend with Drogon's MySQL driver, applies the draft schema, verifies database readiness, tests the protected one-time API bootstrap, and checks immediate member activation, invitation acceptance, login, logout, duplicate-phone rejection, and local-only CORS against a temporary MySQL 8 service when backend or dashboard files change, on pull requests, or when started manually from the Actions tab.

## Publish with GitHub Pages

1. Push the site files to the repository's `main` branch.
2. In the repository, open **Settings → Pages** and set the build and deployment source to **GitHub Actions**.
3. Open **Actions** and confirm that **Deploy static site to GitHub Pages** succeeds.
4. Open **Settings → Pages** to find the published site URL.

The workflow deploys the contents of the repository root whenever a commit is pushed to `main`. Keep real financial and member data out of this public prototype.
