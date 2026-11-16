# Publish CipherGBA v0.1 to a new GitHub repository

The source archive is prepared for a **new** `HodgkinsStudios/CipherGBA` repository. No GitHub actions were performed during this archive preparation. There are no compiled emulator binaries, BIOS files, game ROMs, test outputs or private saves in the source ZIP.

## 1. Create the new repository

After deleting the original repository on GitHub, extract the v0.1 source ZIP into an empty local directory and open a shell there. With Git and the authenticated GitHub CLI (`gh`) installed:

```sh
git init -b main
git add .
git commit -m "CipherGBA v0.1 initial source release"
gh repo create HodgkinsStudios/CipherGBA --public --source=. --remote=origin --push
```

Alternatively, create an empty repository at GitHub and use `git remote add origin https://github.com/HodgkinsStudios/CipherGBA.git` followed by `git push -u origin main`.

## 2. Validate all platform packages

The `.github/workflows/linux.yml` and `.github/workflows/portability.yml` workflows run on pushes to `main`. Confirm that the **full portability workflow, including `release-assets`, passes on the exact intended commit**. The workflow builds/stages Windows x64, macOS Intel/Apple Silicon, Linux X11, AppImage and Flatpak outputs, and verifies the combined packages and SHA-256 manifests. Build success is not proof of manual gameplay validation on each OS; see [platform acceptance](docs/PLATFORM_ACCEPTANCE.md).

## 3. Publish the v0.1.0 release

Once the full portability workflow completes successfully for `main`, in GitHub Actions manually run **Publish CipherGBA v0.1** (`release.yml`). The workflow refuses to publish if it cannot find a successful multi-platform build for the current main commit, and refuses to overwrite an existing `v0.1.0` tag. It downloads exactly that CI run's assembled assets, verifies checksums, and publishes them under the release tag `v0.1.0` using [RELEASE_NOTES.md](RELEASE_NOTES.md).

For the GitHub CLI, after the CI run is green:

```sh
gh workflow run release.yml --ref main
```

The first push should not contain binaries or third-party games/firmware. On future changes, update the documented version, workflows, package names and release notes together before tagging another release.
