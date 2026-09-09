# Publishing a release

The **Publish Full Release** GitHub Actions workflow builds and publishes one release for all currently supported targets:

- Archicad 28 for macOS 26 on Apple Silicon;
- Archicad 29 for macOS 26 on Apple Silicon;
- Archicad 28 for Windows 11 x64;
- Archicad 29 for Windows 11 x64.

It downloads the matching official Graphisoft API DevKits, compiles all four add-ons from the same commit, signs and notarizes the macOS bundles and DMG, creates both platform packages and their SHA-256 files, and attaches all four files to a GitHub release.

## Required repository secrets

Configure these under **Settings > Secrets and variables > Actions** before running the workflow:

- `MACOS_CERTIFICATE_BASE64`: the Developer ID Application certificate and private key exported as a password-protected `.p12`, then base64-encoded;
- `MACOS_CERTIFICATE_PASSWORD`: the password used when exporting the `.p12`;
- `APPLE_ID`: the Apple ID used for notarization;
- `APPLE_TEAM_ID`: the ten-character Apple Developer Team ID;
- `APPLE_APP_PASSWORD`: the app-specific password used by `notarytool`.

The certificate can be encoded locally without uploading it anywhere else:

```sh
base64 -i DeveloperIDApplication.p12 | pbcopy
```

Paste the copied value directly into the `MACOS_CERTIFICATE_BASE64` GitHub Actions secret. Never commit the `.p12`, its password or any Apple credentials to the repository.

## One-button publication

1. Open **Actions > Publish Full Release**.
2. Choose **Run workflow**.
3. Enter the version without a leading `v`, for example `0.3.0-alpha`.
4. Leave **pre-release** enabled for alpha builds.
5. Run the workflow.

The workflow refuses invalid version strings and refuses to overwrite an existing release. A failure in any build, signing, notarization or packaging step prevents publication.

The Windows APX files are currently not Authenticode-signed. They are compiled independently with the Archicad 28 and 29 Windows DevKits and packaged into one ZIP.
