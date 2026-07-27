## General

Utilities for reading and writing floppy disks using a
[Greaseweazle](https://github.com/keirf/greaseweazle).

Based on the `cw2dmk` Catweasel utilities originally created by
[Tim Mann](https://github.com/TimothyPMann/).

Upstream: https://github.com/qbarnes/gw2dmk

See [BUILDING.md](BUILDING.md) for how to build or
[Releases](https://github.com/qbarnes/gw2dmk/releases)
for released binaries.

## macOS Note

Binaries extracted from a tarball via the Finder will be tagged as
quarantined by macOS and will refuse to run.  To clear the quarantine
attribute after extracting, run:

```
$ xattr -d com.apple.quarantine gw2dmk dmk2gw gwhist
```

If you extract binaries via `tar` at the shell, they will be unaffected
and won't be tagged as quarantined.
