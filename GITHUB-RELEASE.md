# Publish to GitHub

The package is prepared locally; nothing has been uploaded or published.

1. Extract the `-source.zip`. Its `Voltra-Remote/` folder is the repository root. Upload its contents or initialize Git there. Include hidden `.github/` and `.gitignore` files. Do not upload local backups or recordings.
2. Push to your new GitHub repository. The included Actions workflow runs host tests. Firmware build instructions target Apple Silicon macOS; a hosted firmware build has not been validated.
3. Create tag `r17` and a GitHub **pre-release**, titled **Voltra Remote r17 — voice control and smoother screen updates**.
4. Paste `RELEASE_NOTES.md` into the release description.
5. Attach the installer ZIP, source ZIP and their matching `.sha256` files. ZIPs belong in Release assets, not normal Git history.

All local build/test evidence is in `release-evidence/`; private development journals, recordings, machine paths and trainer identities are excluded. Existing third-party notices and licenses are retained. The supplied startup artwork is still present; the project/release name is Voltra Remote.
