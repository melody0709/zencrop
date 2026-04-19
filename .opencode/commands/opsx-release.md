# opsx-release

Automates the release process for ZenCrop: committing changes, tagging, and creating a GitHub release with the latest executable and changelog.

**Usage**: `/opsx-release`

## Steps

1. **Identify Latest Version**: Read `main.cpp` to find `#define ZENCROP_VERSION L"x.y.z"`. The tag will be `vx.y.z`.
2. **Extract Release Notes**: Read `doc/CHANGELOG.md` and extract the section for the latest version (stop before the previous version's `## V...` header). Save it to `release-notes.md`.
3. **Ensure Build Exists**: Verify `build\ZenCrop_vx.y.z.exe` exists. If not, run `.\build.bat`.
4. **Git Commit & Push**:
   - `git add .`
   - `git commit -m "chore: release vx.y.z"`
   - `git push`
5. **Git Tag**:
   - `git tag vx.y.z`
   - `git push origin vx.y.z`
6. **Create GitHub Release**:
   - Use the `gh` CLI to create the release:
     `gh release create vx.y.z build\ZenCrop_vx.y.z.exe --title "ZenCrop vx.y.z" --notes-file release-notes.md`
7. **Cleanup**: Delete the temporary `release-notes.md` file.