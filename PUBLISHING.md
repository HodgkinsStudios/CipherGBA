# Repository maintenance and release procedure

**Repository:** [HodgkinsStudios/CipherGBA](https://github.com/HodgkinsStudios/CipherGBA)  
**Default branch:** `main`  
**Latest published release:** [v0.3.0](https://github.com/HodgkinsStudios/CipherGBA/releases/tag/v0.3.0)

## Documentation-only maintenance

Documentation updates may be committed to `main` without changing the emulator version. Do **not** create a new tag, re-upload existing release files, or describe a docs-only commit as a new binary release. Update the [changelog](CHANGELOG.md) under **Unreleased** for editorial changes.

The automated [portability workflow](.github/workflows/portability.yml) can still run after a push to `main`. The current [release workflow](.github/workflows/release.yml) contains an immutable-tag guard: it **refuses to overwrite an existing version**. Documentation commits do not require running the release job manually.

## Future software releases

For an intentional versioned release:

1. Agree on the next version and scope; implement and verify the code changes on a branch.
2. Update version constants, packaging names, release workflow settings, README download references, changelog and [release notes](RELEASE_NOTES.md) together.
3. Merge the verified work and confirm the full [cross-platform build matrix](.github/workflows/portability.yml) passes for the **exact intended main commit**.
4. Verify platform packages, the combined archive, and `SHA256SUMS.txt`.
5. Publish a **new immutable tag**, using release assets from that passing commit only.
6. Preserve older GitHub releases and their original binaries.

Do not use this document as instructions to create, delete, or recreate the existing repository. No BIOS files, copyrighted ROMs, private saves, test outputs, or compiled build intermediates belong in the source repository.

Automated compilation is not a substitute for testing the desktop frontends on actual devices. See [platform acceptance](docs/PLATFORM_ACCEPTANCE.md) and [third-party notices](THIRD_PARTY_NOTICES.md).
