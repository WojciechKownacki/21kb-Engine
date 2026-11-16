# Packages

A package is a folder of ready-made content (assets, scripts, scenes, materials) with a small `package.json`. A project asks
for packages by name and version range; `kb_cli package` finds the versions that fit together, copies them into the project and
records exactly what it installed, so every machine gets the same files.

Packages are source content for building a game. They are not [mods](modding.md), which extend a game that is already released.

## A package

```
acme.crates/
  package.json
  Scripts/Door.lua
  Materials/Wood.kbmat
```

```json
{
  "name": "acme.crates",
  "version": "1.4.0",
  "description": "Crates for the warehouse.",
  "dependencies": { "acme.core": "~2.1" }
}
```

* `name`: lower case letters, digits, `.`, `_` and `-`, at most 48 characters (the same rule as a mod id). It is the folder name
  in the project.
* `version`: `major.minor.patch`.
* `dependencies` (optional): other packages and the versions this one works with.
* Every other file in the folder is content. Links and special files are refused.

### Version ranges

| Written | Means |
| --- | --- |
| `1.2.3` | exactly 1.2.3 |
| `^1.2` | 1.2.0 or newer, below 2.0.0 |
| `~2.1` | 2.1.0 or newer, below 2.2.0 |
| `>=1.0 <3` | both comparisons must hold (separate them with a space or a comma) |
| `*` | any version |

A short version such as `1.2` means `1.2.0`.

## The registry

A registry is an `index.json` and the archives it lists, in a folder or on a web server:

```
Registry/
  index.json
  acme.crates/1.4.0/acme.crates-1.4.0.kbpackage
  acme.core/2.1.7/acme.core-2.1.7.kbpackage
```

```json
{
  "indexVersion": 1,
  "packages": [
    {
      "name": "acme.crates",
      "version": "1.4.0",
      "description": "Crates for the warehouse.",
      "dependencies": { "acme.core": "~2.1" },
      "archive": "acme.crates/1.4.0/acme.crates-1.4.0.kbpackage",
      "sha512": "<128 hexadecimal digits>",
      "size": 18342
    }
  ]
}
```

* `archive` is a plain relative path (beside the index) or an `https://` address. `sha512` and `size` are those of the archive file.
* An archive is a tar stream compressed as one zstd frame, `.kbpackage`. The same folder always gives the same bytes.
* The resolver reads only the index: names, versions and dependencies. An archive is fetched when the package is installed.

`publish` builds the archive of a package folder and adds it to a registry folder (the folder and `index.json` are made when missing):

```
kb_cli package publish --registry Registry Packages/acme.crates
```

A published version is never replaced; raise `version` in `package.json` instead. Put the registry folder on any web server that
serves static files and give its address, `https://example.com/registry/index.json`, as the registry. Plain `http://` is accepted
for this computer only (for testing); anything else must be `https://`.

`add --registry <folder or address>` stores it in the project's `packages.json` (a folder relative to the project when it can be), so
later commands need no option.

Installing from an address:

* The index is read first. A download is limited to the size the index lists (at most 256 MiB).
* The SHA-512 of the archive must equal the one in the index, or nothing is unpacked and the project is not changed.
* Downloaded archives are kept in `Saved/PackageCache` of the project (`--cache <dir>` names another folder), named by their sum, so
  installing the same version again does not download it. A cached file is checked again before it is used.
* An archive is checked completely before the first file is written. It is refused when a path is absolute, climbs out with `..`,
  names a drive, a reserved Windows name or a name ending in a dot or space, repeats another path (also in other letter case), when
  it holds a link or any other entry that is not a plain file or folder, when it has no `package.json`, or unpacks to more than 1 GiB.
  Its `package.json` must also match the entry in the index.

## Commands

All commands take `--project <dir>` (except `publish`) and, with `--json`, return `data` with the result and, for commands that
change files, `changes` with every file created, modified or removed.

```
kb_cli package search --project P [<text>]          what the registry has
kb_cli package info   --project P <name>[@<range>]  versions, dependencies, download size of one package
kb_cli package add    --project P --registry R <name>[@<range>]
kb_cli package update --project P [<name>]
kb_cli package remove --project P <name>
kb_cli package list   --project P                   what is installed and why
kb_cli package verify --project P                   are the installed files as installed
kb_cli package publish --registry <folder> <package folder>
```

* **add** picks the newest version that fits, together with everything it needs. Without a range the project asks for
  `^<version chosen>`. A package that is already installed keeps its version while that still fits.
* **update** moves packages to the newest versions their ranges allow. With a name only that package moves.
* **remove** takes a package the project asked for, and the packages only it needed. A package that others need is refused
  with the names of those packages.
* **verify** compares every installed file with the lock and names each file that is modified, missing or added. It exits with
  1 when something differs.

When the requirements cannot be met, the command says why and changes nothing:

```
error: Cannot install 'acme.b': no version fits all that is asked of it (acme.a 1.4.0 needs acme.b ~2.1; acme.c 1.0.0 needs
acme.b ^3). Available versions: 2.1.7, 3.0.0. Nothing was changed.
```

The resolver picks, for each package, the newest version that satisfies every requirement it has seen (preferring the locked one).
It does not search for another combination when that fails, and it does not read pre-release versions. Packages that need each
other in a circle are refused.

## In the project

| File | Holds |
| --- | --- |
| `packages.json` | the registry and the packages the project asks for, with their ranges. Edit it or use the commands. |
| `packages.lock.json` | each installed package: exact version, dependencies, a SHA-512 checksum of the package and the SHA-512 of every file. Commit it. |
| `Assets/Packages/<name>/` | the installed files. |

Installed files are ordinary project assets under `/Game/Packages/<name>/...`: the editor lists them, scripts and scenes use them by
that path, and cooking treats them like any other asset. Do not edit them; `verify` reports a changed file, and `update` replaces the folder.
Put your own changes in your own assets.

`add` and `update` unpack a package, copy it and compare the copy with the unpacked files before they write the lock. If the files
of a version that is already locked differ from the locked sums, the command stops instead of installing them.

## In the editor

Options > Packages opens the Packages panel: the packages the registry offers and those installed (with the version asked for and
the newest one, and a note such as "Needed by acme.a"), and the buttons Add, Update, Remove, Verify and Registry... (the folder or
address). Every operation runs in the background and ends with a sentence in plain words: what was installed, updated or removed, or
why nothing was changed (for example which two packages need different versions of a third). Verify lists the files that differ.
The panel and `kb_cli package` use the same library, `kb_packages` (`sources/packages`).

## Not yet

* Packages that carry native modules; a package holds content and scripts.
* Signed registries (the checksum comes from the index, so the index must come from a source you trust, over https).
* Searching for another combination of versions when the newest ones conflict.
