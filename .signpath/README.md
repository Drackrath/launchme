# Code signing

Release builds are signed through [SignPath Foundation](https://signpath.org/), which signs open-source
projects for free. `.github/workflows/release.yml` sends two files:

1. `launchme.exe`, before the installer and the ZIP are built from it;
2. `launchme-<version>-win64-setup.exe`, the finished installer.

Until it is set up, the workflow skips signing and publishes unsigned files.

## Setup

1. Apply at <https://signpath.org/apply> with this repository. Once accepted, SignPath creates an
   organization and a project for it.
2. In the project, set the slug to `launchme` and add two artifact configurations with these slugs and the
   contents of the files here:
   - `launchme-exe`: [artifact-configurations/launchme-exe.xml](artifact-configurations/launchme-exe.xml)
   - `launchme-setup`: [artifact-configurations/launchme-setup.xml](artifact-configurations/launchme-setup.xml)
3. Signing policies: `release-signing` (the release certificate, used for `v*` tags) and `test-signing`
   (used when the workflow is started by hand).
4. Add GitHub as a trusted build system for the project, and create a CI user with submitter rights on both
   policies.
5. In the repository's settings (Secrets and variables → Actions):
   - secret `SIGNPATH_API_TOKEN`: the CI user's API token;
   - variable `SIGNPATH_ORGANIZATION_ID`: the organization's ID.

## Limits

- Inno Setup's uninstaller (`unins000.exe`) stays unsigned: Inno signs it only while compiling, with a local
  sign tool (`tools/build-installer.ps1 -Sign`), and SignPath signs finished files.
- The DLLs inside `plugin-packages/*.zip` are not signed.
