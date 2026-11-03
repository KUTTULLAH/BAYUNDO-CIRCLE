# Jirani Circle

A static, responsive chama dashboard prototype.

## Prototype limitations

- Member and contribution records are sample data stored only in browser memory.
- Adding a member or contribution changes the current page only; data is not saved.
- Login, member privacy controls, the database-backed API, reports, and M-Pesa STK Push are not implemented.
- Do not enter real member or payment information.

## Backend foundation

- `backend/src/main.cc` currently implements `GET /api/v1/health` and a MySQL readiness check; member and contribution endpoints are not implemented yet.
- [`backend/schema.sql`](./backend/schema.sql) is a draft MySQL schema for chama members, individually effective-dated monthly targets, invitations, sessions, financial transactions, access requests, and audit events.
- [`backend/openapi.yaml`](./backend/openapi.yaml) is the API contract. Members read their own records; an admin can approve one read-only retrieval of another member's records for a requested date range.
- Member accounts use phone-number sign-in and one-time activation invitations. Members set their own passwords; SMS/WhatsApp delivery still needs a provider.
- The initial design supports one chama membership per account and KES. A secure one-time bootstrap command must create the first chama and administrator before member invitations can be issued. Public admin signup is not part of the design.
- The schema has not been run against MySQL, and the API contract has not been run through an OpenAPI validator.

### Build the API on Windows

Install Visual Studio Build Tools with the C++ workload, CMake, vcpkg, and MySQL 8. Set `VCPKG_ROOT` and the `DB_HOST`, `DB_PORT`, `DB_NAME`, `DB_USER`, and `DB_PASSWORD` environment variables for your database; do not commit database credentials. Then run from the repository root:

```powershell
cmake -S backend -B backend/build `
  -DVCPKG_OVERLAY_PORTS="$PWD/backend/vcpkg-overlay-ports" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build backend/build --config Release
$env:PORT = "8080"
.\backend\build\Release\jirani-circle-api.exe
```

Check `http://localhost:8080/api/v1/health` for `{"status":"ok"}`. The endpoint returns `503` until it can run a test query against the configured database.

The **Build C++ API** GitHub Actions workflow builds the backend with Drogon's MySQL driver and runs a readiness check against a temporary MySQL 8 service when backend files change, on pull requests, or when started manually from the Actions tab.

## Publish with GitHub Pages

1. Push the site files to the repository's `main` branch.
2. In the repository, open **Settings → Pages** and set the build and deployment source to **GitHub Actions**.
3. Open **Actions** and confirm that **Deploy static site to GitHub Pages** succeeds.
4. Open **Settings → Pages** to find the published site URL.

The workflow deploys the contents of the repository root whenever a commit is pushed to `main`. Keep real financial and member data out of this public prototype.
