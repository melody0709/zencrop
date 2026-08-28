# ZenCrop MSI upgrade contract

This is the permanent contract for the `stable|x64|perMachine` ZenCrop MSI
product line. It exists so a future architectural rewrite can still be shipped
as a safe Windows Installer major upgrade.

## Immutable identity

`ProductIdentity.wxi` is the only source for these values. They were generated
once before the first MSI release and must never be regenerated:

| Value | Constant |
| --- | --- |
| UpgradeCode | `A7A61016-64CE-4391-AE58-B5397CE3046D` |
| ProductCode UUID v5 namespace | `4F345A15-C37F-407D-9E3B-DF7BEF7E23B3` |
| Component UUID v5 namespace | `C8957C35-9C53-4EE7-B0CB-BBED61C90CAF` |

For every public `major.minor.patch`, `ProductCode` is UUID v5 of
`stable|x64|perMachine|major.minor.patch` in the ProductCode namespace. A
runtime Component GUID is UUID v5 of its canonical slash-separated relative
payload path in the Component namespace. New path means new Component; the old
registered MSI removes a deleted or moved Component during the major upgrade.

`PackageCode` is deliberately not stored here. WiX creates a new PackageCode
for each actual MSI file.

## Release and upgrade rules

- Every public three-part version is a full MSI major upgrade.
- `MajorUpgrade` remains `afterInstallInitialize` with
  `AllowSameVersionUpgrades="no"`.
- A public version's MSI, Portable `.7z`, signatures, and checksums are immutable. A
  different payload requires a three-part version increase; do not enable
  same-version upgrade or choose a random ProductCode to evade this rule.
- A same-version package command is a read-only confirmation: it checks the
  stored SHA-256 sidecar, re-extracts the Portable `.7z`, and revalidates the MSI
  database plus administrative payload. The MSI also stores a deterministic
  digest of its WiX authoring, generators, and generated inputs, so an
  installer-semantic change fails closed rather than reusing an old MSI. It
  never overwrites a release asset.
  Signed confirmation reuses the already verified signed payload because a new
  RFC 3161 timestamp would correctly produce different bytes.
- The ProductCode, feature tree, and installed file layout may change. The
  UpgradeCode and both UUID namespaces may not change while this is the same
  non-side-by-side stable/x64/per-machine product line.
- A new channel, side-by-side product, install context, or CPU architecture is
  a product decision that needs a separate identity contract before release.

## Installation UI and selected root

- A first installation defaults to `ProgramFiles64Folder\ZenCrop`. Its native
  MSI UI offers one-click `Install` at that resolved root and `Advanced...` for
  the standard folder picker; it does not show a fabricated license screen.
- The selected root is saved as the 64-bit per-machine value
  `HKLM\Software\ZenCrop\InstallFolder` by its own stable registry Component.
  A later MSI restores `INSTALLFOLDER` through AppSearch before
  `RemoveExistingProducts` runs, then writes the value again. This preserves a
  user-selected root across normal `afterInstallInitialize` major upgrades.
- Keep this flow declarative: no type-51 property setting, custom action, or
  wildcard cleanup may participate in install-root selection or migration.
  An uninstall removes ZenCrop's own record; it never recursively deletes an
  arbitrary selected root or its unknown files.
- The first upgrade from a historical MSI that predates this record naturally
  uses the Program Files default. Once a release contains the record, formal
  upgrade testing must cover a non-default selected root as well as the
  Program Files default.

## File ownership and cleanup

- Every canonical runtime file is in exactly one non-permanent, non-shared MSI
  Component. WiX generates `MsiFileHash` for unversioned files; JavaScript and
  CSS never use the PE checksum attribute.
- Files owned by a prior MSI are removed by that prior MSI during
  `RemoveExistingProducts`.
- `LegacyCleanup.wxs` may contain only exact known historical orphan paths,
  with their source version range and reason documented here. It may not use
  wildcards, recursive directory removal, or an unbounded Program Files delete.
- When a future architecture change moves or deletes a runtime path, retain the
  old MSI in release storage and let its registered Component remove the old
  path during the major upgrade. Add `LegacyCleanup.wxs` only for a proven
  orphan that no historical MSI owns, and record its exact path, affected
  version range, and evidence in this document.
- Unknown files in the installation directory are preserved. In particular, an
  unknown WebView asset remains visible to `WebAssetGuard`, which fails closed
  and reports it rather than having MSI guess whether it is safe to remove.
- `%LOCALAPPDATA%\ZenCrop` is never MSI-owned. Upgrade, repair, and uninstall
  preserve it. Any data-schema migration is an idempotent application concern,
  not an MSI custom action.

## Validation and release gates

`build.bat --package-msi` performs WiX validation, database contract checks,
and `msiexec /a` administrative extraction followed by an exact file/hash
comparison to the canonical payload. It does not install the product.

`build.bat --package` performs the same checks for both package types using
one canonical payload. The Portable artifact is a solid LZMA2 `.7z`; its
technical entry list is safety-checked, 7-Zip integrity-tested, safely
extracted, and compared file-by-file with the canonical payload. In an unsigned
engineering environment, the outputs are explicitly suffixed `-unsigned`; they
are not a substitute for a signed release upgrade chain.

Before a formal release, an isolated Windows VM must retain signed MSIs and
checksums and run clean install, forced asset repair, N-1 to current,
oldest-supported to current, applicable architecture-boundary upgrade,
all-users Start Menu shortcut target/removal, downgrade rejection, FilesInUse,
obsolete-file removal, known-orphan cleanup,
LocalAppData preservation, rollback, and uninstall tests. The double-gated
`scripts/test_msi_lifecycle.ps1` is the harness entry point; it is never run by
the normal build or package commands. It requires explicit historical and
current MSI paths, elevation, `-ConfirmSystemMutation`, and
`ZENCROP_ALLOW_MSI_LIFECYCLE_TEST=1`; use `-RequireTrustedSignatures` for a
formal signed-release VM run. FilesInUse and transaction rollback remain
explicit operator/VM test cases because the package intentionally has no test
custom action that could manufacture those failures.

Use `-CustomInstallRoot <fresh-absolute-path>` only with a historical MSI that
already implements the install-root persistence contract. The harness then
installs that historical MSI at the supplied path and verifies that the current
MSI both remains there and re-records the same 64-bit registry value.
