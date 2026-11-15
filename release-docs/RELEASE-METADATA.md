# Release records

The portable builder generates records from the actual staging directory after source assembly and host payload verification. It does not start the application or run tests. The records do not claim that a release was uploaded, accepted, or independently audited.

| File | Meaning |
| --- | --- |
| `source-records.json` | Explicit original code/document mappings, copied byte fingerprints and generated outputs selected by the builder. |
| `file-license-index.json` | One entry per package file, using explicit source mappings or the inventory's exact-path/longest-path-prefix rules. A prefix may identify a directory or a filename family. Rights scopes and preserved notices apply. Unmapped third-party files are `NOASSERTION`, never automatically GPL. |
| `provenance.json` | Full-project source revision when explicitly supplied, native host artifact identity, retained input fingerprints and unresolved rights coverage. Host CI and full-project commits are recorded separately. |
| `validation-range.json` | The work actually performed and work still unverified. `testsRun` and `applicationLaunched` are `false`; runtime and device acceptance remain outstanding. |
| `package-manifest.json` | SHA-256 and byte count for actual files, excluding this manifest and `SHA256SUMS` to avoid recursive hashes. |
| `SHA256SUMS` | SHA-256 for every regular package file except itself, including `package-manifest.json`. Paths are relative to the package root. |

Original code, build scripts, configuration and original program assets use GPL-3.0-only within the contributors' rights. Original explanatory wording uses CC BY-SA 4.0. Facts and measurements carry no new copyright claim. Third-party components and license texts keep their own terms. These records do not resolve license compatibility or apply a project license to other people's work.

Only entries with explicit `additionalPermissionPaths` record the Windows host's narrow permission in `LICENSE-WEBVIEW2-EXCEPTION`. This does not extend the permission to other project files or change Microsoft component terms. The GPL text is retained unchanged.

For an existing, verified host artifact and full source commit, the maintainer can use:

```powershell
.\Build-ProperNotesPortable.ps1 -HostArtifactPath '<extracted-host-artifact>' -SourceRepositoryUrl 'https://github.com/unknownnumbers/ProperNotes-for-Windows' -SourceCommit '<full-source-commit>'
```

`New-ProperNotesReleaseMetadata.ps1` is included as source. To refresh records for an existing package, run its copy with `-PackageDirectory '<package-directory>'`, retain that package's `source-records.json`, and supply the same explicit source revision and host artifact manifest when available. A refresh does not verify that compilation occurred, and the default `HostBuildMode` is `unspecified`. Changing any package byte requires regenerating the records and external archive checksums. No local absolute build paths are recorded.

To create a fresh, isolated build input, use `Export-ProperNotesSource.ps1 -OutputDirectory '<new-source-directory>' -NodePath '<approved-node-executable>' -NodeLicensePath '<matching-official-node-license>'`. The destination must not exist. The exporter copies explicit original source/document paths, the reviewed 201 vendor files, native host sources, public cloud templates and the license directory, while replacing the old Node notice with the explicitly supplied matching notice. It checks the supplied Node files and vendor files against the inventory fingerprints. It creates `.gitattributes` and `.github/workflows/windows-portable.yml` from the public templates. It does not copy the old runtime tree, runtime data, internal work notes, tests, research or build output, and it does not invoke any program. A failed export retains its partial new directory for inspection; choose another new destination after correcting the input.

A separate complete corresponding source archive and its external checksum must accompany a distributed binary. Package-local records do not assert that this archive exists or has been published. The maintainer must check the archive against the exact binary inputs and retain third-party notices and source availability requirements. The source tree includes the portable builder, metadata generator and cloud host recipe. Existing build prerequisites remain necessary; nothing here installs them.

The launcher's `propernotes-portable.json` keeps its existing startup protocol and its earlier source/host inventory. The separate final package manifest covers the later release records. Drawboard Mouse and Touch / Pen switching and Tool Lock remain unverified. Compatibility on target Windows hardware, including Surface and external pen tablets, has not been accepted. Handwriting responsiveness and PDF reading/saving performance have not been measured or accepted. These limits are recorded in `validation-range.json`.
