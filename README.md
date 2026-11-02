# Jirani Circle

A static, responsive chama dashboard prototype.

## Prototype limitations

- Member and contribution records are sample data stored only in browser memory.
- Adding a member or contribution changes the current page only; data is not saved.
- Login, member privacy controls, an API, a database, reports, and M-Pesa STK Push are not implemented.
- Do not enter real member or payment information.

## Publish with GitHub Pages

1. Add `index.html`, this `README.md`, and `.github/workflows/deploy-pages.yml` to the root of the `main` branch.
2. In the repository, open **Settings → Pages** and set the build and deployment source to **GitHub Actions**.
3. Open **Actions** and confirm that **Deploy static site to GitHub Pages** succeeds.
4. Open **Settings → Pages** to find the published site URL.

The workflow deploys the contents of the repository root whenever a commit is pushed to `main`. Keep real financial and member data out of this public prototype.
