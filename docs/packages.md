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

A registry is a folder of packages, one folder per name and version:

```
Registry/
  acme.crates/1.3.0/package.json ...
  acme.crates/1.4.0/package.json ...
  acme.core/2.1.7/package.json ...
```

`add --registry <folder>` stores the folder in the project's `packages.json` (relative to the project when it can be), so later
commands need no option. Only a local folder is supported; a registry on a web server is a next step.

## Commands

All commands take `--project <dir>` and, with `--json`, return `data` with the result and, for commands that change files,
`changes` with every file created, modified or removed.

```
kb_cli package search --project P [<text>]          what the registry has
kb_cli package info   --project P <name>[@<range>]  versions, dependencies, files of one package
kb_cli package add    --project P --registry R <name>[@<range>]
kb_cli package update --project P [<name>]
kb_cli package remove --project P <name>
kb_cli package list   --project P                   what is installed and why
kb_cli package verify --project P                   are the installed files as installed
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

`add` and `update` copy a package and compare the copy with the registry files before they write the lock. If the registry's files
for a version that is already locked differ from the locked sums, the command stops instead of installing them.

## Not yet

* A registry served over HTTP, which needs an index file and a package archive format.
* A package panel in the editor.
* Packages that carry native modules; a package holds content and scripts.
