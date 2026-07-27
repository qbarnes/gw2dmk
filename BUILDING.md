# Building Gw2dmk

There are four ways to build `gw2dmk`:

   * [The usual way](#natively-building-gw2dmk), natively, which creates binaries for the system you're running on,
   * [Cross-building](#cross-building-gw2dmk),
   * [Cross-building using OCI containers](#cross-building-gw2dmk-using-oci-containers) (docker or podman), or
   * [Building with GitHub Actions](#building-with-github-actions)


## Natively Building Gw2dmk

To build `gw2dmk` for the system you're running on, you may need
to install additional packages before you build.

### Installing packages

#### Linux

You'll need to install packages for C development and generating PDFs.

On Fedora and RHEL-like distros, run:

```
$ sudo dnf install -y @c-development groff-perl perl-IO-Compress
```

On Debian, Ubuntu, and related distros, run:

```
$ sudo apt-get update
$ sudo apt-get install -y build-essential groff-base groff
```

#### macOS

On macOS, install the Xcode Command Line Tools for the compiler, then
the GNU tools the makefiles use.

With [Homebrew](https://brew.sh):

```
$ xcode-select --install
$ brew install coreutils gnu-tar groff
```

or with [MacPorts](https://www.macports.org):

```
$ xcode-select --install
$ sudo port install coreutils gnutar groff
```

Xcode's own `make` is enough.  There is no need to install a later
version of GNU make from Homebrew or MacPorts unless you want.

Then follow the directions for Homebrew or MacPorts for the adding
the appropriate directory path to your shell's `PATH` environment
variable.

### Building Gw2dmk natively

To clone this repo, run:

```
$ git clone git@github.com:qbarnes/gw2dmk.git
```

To build, change directory into the cloned repo and run:
```
$ make
```

The binaries and formatted man pages will be under the `build`
directory.

To build just the binaries without the formatted man pages, run:
```
$ make bins
```

The complementary command `make mans` will build just the formatted
man pages.

## Cross-building Gw2dmk

Cross-building creates `gw2dmk` binaries for these seven platforms:

   * Linux (x86_64, 32-bit and 64-bit ARM),
   * 32-bit and 64-bit Microsoft Windows
   * macOS (arm64 and x86_64)

Cross-building is **only supported while running on x86_64 Linux.**

The two macOS platforms are not in the default `BUILDS` list and need a
toolchain that cannot be distributed; see
[Cross-building for macOS](#cross-building-for-macos) below.
**Everything else in this section covers the other five platforms.**

**Note:** Full cross-building of all five platforms without
using OCI containers is spotty.  Some distros only support some
cross-build environments or may only support them on a later version.
Hence, your milage may vary. If you'd like to build for all, please
follow the
[OCI directions](#cross-building-gw2dmk-using-oci-containers).

### Installing software

To cross-build, you'll need to install some additional software first.

#### Fedora

On Fedora and RHEL-like distros, run:

```
$ sudo dnf install -y @c-development mingw{32,64}-gcc mingw{32,64}-libgnurx{,-static}
```

To cross-build for ARM 32- or 64-bit, it's rather complicated
and is not covered here.  I recommend using the
[OCI container approach](cross-building-gw2dmk-using-oci-containers).

#### Debian

On Debian, Ubuntu, and related distros, run:

```
$ sudo apt-get update
$ sudo apt-get install -y git curl build-essential groff-base groff \
    bsdmainutils mingw-w64 gcc-aarch64-linux-gnu gcc-arm-linux-gnueabi
$ curl -fSsLO https://github.com/andrewwutw/build-djgpp/releases/download/v3.1/djgpp-linux64-gcc1020.tar.bz2
$ tar -C ~ -xf djgpp-linux64-gcc1020.tar.bz2  # Add ~/djgpp/bin to your PATH
```

### Cross-building

To clone this repo, run:

```
$ git clone git@github.com:qbarnes/gw2dmk.git
```

To cross-build for all non-macOS platforms, change directory
into the cloned repo and run:

```
$ make -f Makefile.cross
```

Binaries will be found under the `build.*` directories.

On Fedora, to exclude the missing ARM platforms when cross-building,
run:

```
$ make -f Makefile.cross BUILDS="LINUX_X86_64 MSWIN32 MSWIN64"
```


## Cross-Building Gw2dmk using OCI Containers

Cross-building `gw2dmk` with containers builds binaries for all
platforms like cross-building above, but does not require installing
any additional software packages on your system beyond `git`,
`make`, and OCI tools (`docker` or `podman`).

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

### Using containers for building

To clone this repo, run:

```
$ git clone git@github.com:qbarnes/gw2dmk.git
```

To cross-build for all non-macOS platforms using containers, change
directory into the cloned repo and run:

```
$ make -f Makefile.oci pull  # Download latest container versions (optional)
$ make -f Makefile.oci
```

Note that the first time building will take time to download
and locally cache the containers.  Any following builds will be
considerably faster.

### Cross-building for macOS

The macOS cross-build uses [osxcross](https://github.com/tpoechtrager/osxcross),
whose toolchain image is built from the same
[containers-for-cross-compiling](https://github.com/qbarnes/containers-for-cross-compiling)
repo as the other five.  It differs in one respect: it embeds Apple's
macOS SDK, and Apple's license restricts use of the SDK to
Apple-branded hardware and forbids redistributing it.  Its package is
therefore `Private`, so it is not available to an unauthenticated
`make -f Makefile.oci pull`, and it is built there by its own manually
triggered workflow rather than alongside the other five.

Once the image exists, log in and build:

```
$ podman login ghcr.io       # or: docker login ghcr.io
$ make -f Makefile.oci pull  # Download latest container versions (optional)
$ make -f Makefile.oci BUILDS="DARWIN_ARM64 DARWIN_X86_64"
```

Binaries are under `build.darwin.arm64` and `build.darwin.x86_64`.


## Building with GitHub Actions

Building with GitHub Actions requires no need to install any
software on your computer locally, or for that matter, even have a
computer of your own!

If you're not already an existing contributor to `gw2dmk`, the
first step to building `gw2dmk` with GitHub Actions is to fork the
repository and then follow the directions below with your own fork.

To access your own fork using the example links below, replace the
string "qbarnes" with your own GitHub account name.

### Building artifacts

To build artifacts, go to the "Build gw2dmk" workflow page under
GitHub Actions.  A example link would be
https://github.com/qbarnes/gw2dmk/actions/workflows/gw2dmk-build.yml,
but substitute your own GitHub account name to use your fork.


On the "Build gw2dmk" workflows page, you'll find a button menu
"Run workflow".  Select it.  If desired, change the branch, then
select the "Run workflow" button at the bottom of the menu.  You may
need to reload the web page to see the scheduled workflow appear.

While the workflow is running (or after the build finishes), select
its "Build gw2dmk" link to see the status of the run.  When the run
completes successfully, at the bottom of the page seven artifacts
will be added.  You can download any or all of these.  They'll each
contain a tarball of files for that platform's build.

### Building releases

To build a release, first ensure the `VERSION` macro in `product.mk`
contains the unique release identifier you want for this release.

Now run the the "Release gw2dmk" GitHub Action workflow.  An
example link would be
https://github.com/qbarnes/gw2dmk/actions/workflows/gw2dmk-release.yml,
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
"Tags".  Example link: https://github.com/qbarnes/gw2dmk/tags.

Also, under the newly created release area for the tag, you'll find
its seven release tarballs and source code.  Example link:
https://github.com/qbarnes/gw2dmk/releases/tag/v0.5.0.

Be sure to edit and update the description of the new release!

### Note for macOS when building with Actions

To build for macOS with a forked repo, you'll also need to fork
https://github.com/qbarnes/containers-for-cross-compiling as well
and build its containers. Then go to this URL after changing
"qbarnes" to the org that has your forked copy:
https://github.com/users/qbarnes/packages/container/containers-for-cross-compiling%2Fubuntu-22.04-crossbuild-macos/settings.  Under "Manage Actions
access", add your forked `gw2dmk` repo with a `Read` role.
