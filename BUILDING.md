# Building Gw2dmk

There are four ways to build `gw2dmk`:

   * [The usual way](#building-gw2dmk-natively), natively, which creates binaries for the system you're running on,
   * [Cross-building](#cross-building-gw2dmk),
   * [Cross-building using OCI containers](#cross-building-gw2dmk-using-oci-containers) (docker or podman), or
   * [Cross-building with GitHub Actions](#cross-building-with-github-actions)


## Building Gw2dmk Natively

To build `gw2dmk` for the system you're running on, you may need
to install additional packages before you build.

### Installing packages

First, you'll need to install packages for C development.

On Fedora and RHEL-like distros, run:

```
$ sudo dnf install -y @c-development
```

On Debian, Ubuntu, and related distros, run:

```
$ sudo apt-get update
$ sudo apt-get install -y build-essential
```

To clone this repo, run:

```
$ git clone git@github.com:qbarnes/gw2dmk.git
```

### Building natively

To build, change directory into the cloned repo and run:
```
$ make
```

The binaries will be under the `build` directory.


## Cross-Building Gw2dmk

Cross-building creates `gw2dmk` binaries for these five platforms:

   * Linux (x86_64, 32-bit and 64-bit ARM),
   * 32-bit and 64-bit Microsoft Windows

Cross-building is only supported while running on x86_64 Linux.

**Note:** Full cross-building of all five platforms simultaneously cannot
be supported at this time.  Each platform tested is missing critical
pieces.  For example, on Fedora, you can build for MS Windows, but not
for ARM.  On Ubuntu, you can build for ARM, but not MS Windows.  If
you'd like to build for all, please follow the
[OCI directions](#cross-building-gw2dmk-using-oci-containers).

### Installing software

To cross-build, you'll need to install some additional software first.

On Fedora and RHEL-like distros, run:

```
$ sudo dnf install -y @c-development mingw{32,64}-gcc mingw{32,64}-libgnurx{,-static}
```

On Debian, Ubuntu, and related distros, run:

```
$ sudo apt-get update
$ sudo apt-get install -y build-essential mingw-w64 gcc-aarch64-linux-gnu gcc-arm-linux-gnueabi
```

To clone this repo, run:

```
$ git clone git@github.com:qbarnes/gw2dmk.git
```

### Cross-building

To cross-build for all platforms, change directory into the cloned
repo and run:

```
$ make -f Makefile.cross
```

Binaries will be found under the `build.*` directories.

Unfortunately, Fedora and related distros don't fully support
cross-building for ARM.  Packages for ARM cross-compilers are
available, but without the necessary cross-environment.
To exclude the ARM platforms when cross-building, run:

```
$ make -f Makefile.cross BUILDS="LINUX_X86_64 MSWIN32 MSWIN64"
```

Unfortunately, Debian, Ubuntu, and related distros are missing
dependent libraries for cross-building for Microsoft Windows.
To exclude the MSW platforms when cross-building, run:

```
$ make -f Makefile.cross BUILDS="LINUX_X86_64 LINUX_ARMV7L LINUX_AARCH64"
```


## Cross-Building Gw2dmk using OCI Containers

Cross-building `gw2dmk` with containers builds binaries for all five
platforms like cross-building above, but does not require installing
any additional software packages on your system beyond `git`,
`make`, and OCI tools (`docker` or `podman`).  On Fedora, building
with containers also allows ARM cross-builds, and on Debian/Ubuntu,
building with containers also allows MS Windows cross-builds.

Cross-building with containers is only supported when running on
x86_64 Linux.

### Installing software

On Fedora and RHEL-like distros, run:

```
$ sudo dnf install -y git make podman
```

On Debian, Ubuntu, and related distros, run:

```
$ sudo apt-get update
$ sudo apt-get install -y git make docker.io
```

To clone this repo, run:

```
$ git clone git@github.com:qbarnes/gw2dmk.git
```

### Using containers for building

To cross-build for all platforms using containers, change directory
into the cloned repo and run:

```
$ make -f Makefile.oci
```

Note that the first time building will take time to download
and locally cache the containers.  Any following builds will
be considerably faster.

## Cross-Building with GitHub Actions

Building with GitHub Actions requires no need to install any
software on your personal computer, or for that matter, even have a
computer of your own!

If you're not already an existing contributor to `gw2dmk`, the
first step to building `gw2dmk` with GitHub Actions is to fork the
repository and then follow the directions below with your own fork.

To access your own fork using the example links below, use the
references below, but replace the string "qbarnes" with your own
GitHub account name.

### Building artifacts

To build artifacts, go to the "Build gw2dmk" workflow page under
GitHub Actions.  A example link would be
[https://github.com/qbarnes/gw2dmk/actions/workflows/gw2dmk-build.yml](https://github.com/qbarnes/gw2dmk/actions/workflows/gw2dmk-build.yml),
but substitute your own GitHub account name to use your fork.

On the "Build gw2dmk" workflows page, you'll find a button menu
"Run workflow".  Select it.  If desired, change the branch, then
select the "Run workflow" button at the bottom of the menu.  You may
need to reload the web page to see the scheduled workflow appear.

While the workflow is running (or after the build finishes), select
its "Build gw2dmk" link to see the status of the run.  When the run
completes successfully, at the bottom of the page five artifacts
will be added.  You can download any or all of these.  They'll each
contain a tarball of files for that platform's build.

### Building releases

To build a release, first ensure the `VERSION` macro in `product.mk`
contains the unique release identifier you want for this release.

Now run the the "Release gw2dmk" GitHub Action workflow.  An
example link would be
[https://github.com/qbarnes/gw2dmk/actions/workflows/gw2dmk-release.yml](https://github.com/qbarnes/gw2dmk/actions/workflows/gw2dmk-release.yml),
but substitute your own fork.

On the "Release gw2dmk" workflows page, you'll find a button menu
"Run workflow".  Select it.  If desired, change the branch, then
select the "Run workflow" button at the bottom of the menu.  You may
need to reload the web page to see the scheduled workflow appear.

While the workflow is running (or after the build finishes), select
its "Release gw2dmk" link.

A common reason for the release workflow to fail is not having
a unique tag.  Either change the `VERSION` macro or remove the
existing tag that conflicts.

To clean up from a failed release workflow run, be sure to delete
the release, if made, and then delete its tag.

When the run completes successfully, you'll find a new tag with the
string from the `VERSION` macro prefixed with a "v" under the repo's
"Tags".  Example link:
[https://github.com/qbarnes/gw2dmk/tags](https://github.com/qbarnes/gw2dmk/tags).
Also, under the newly created release area for the tag, you'll find
its five release tarballs and source code.  Example link:
[https://github.com/qbarnes/gw2dmk/releases/tag/v0.0.0](https://github.com/qbarnes/gw2dmk/releases/tag/v0.0.0).

Be sure to edit and update the description of the new release!
