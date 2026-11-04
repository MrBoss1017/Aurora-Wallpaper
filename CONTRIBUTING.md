# Contributing

Please open an issue before a large feature or architecture change. Keep changes focused and describe the behavior before and after the change.

Build instructions are in [docs/BUILD.md](docs/BUILD.md). Run the portable policy tests for policy or geometry changes. Native changes also need the Windows build and relevant cases from [docs/VALIDATION.md](docs/VALIDATION.md).

Do not commit vendor binaries, wallpaper media you do not have permission to share, credentials, crash dumps, or personal logs. Submitted original code uses this repository's MIT license.

Release packaging runs in GitHub Actions. Version changes must remain consistent across CMake, application metadata, and installer defaults. Use preview releases until the documented desktop/GPU validation is complete.
