# IRIX Software Packaging — Practical Reference

A guide for programmers who know IRIX and MIPSpro but have not packaged
software for IRIX before. Covers both the command-line (`gendist`) and
GUI (`swpkg`) workflows.

Primary sources: *Software Packager User's Guide* (007-2503-003) and the
`gendist(1M)` reference page.

---

## 1. Core Concepts

### What a tardist is

A tardist (`.tardist`) is simply a `tar` archive containing the output of
`gendist`. When a user runs `inst -f foo.tardist`, the `inst` program
unpacks it and presents a standard Software Manager interface.

### Three-level product hierarchy

Every IRIX package has three levels. The naming convention is strict:

```
product         e.g.  myapp
  image         e.g.  myapp.sw        (software)
                      myapp.man       (manual pages)
    subsystem   e.g.  myapp.sw.base   (required files)
                      myapp.sw.optional
```

SGI convention (strongly recommended):
- There must be a `sw` image containing at least a `base` subsystem.
- Documentation goes in a `man` image with `manpages` and `relnotes` subsystems.
- Image names are `product.image`; subsystem names are `product.image.subsystem`.

### Two input files

`gendist` requires exactly two files:

| File | Extension | Purpose |
|---|---|---|
| Spec file | `.spec` | Declares product/image/subsystem hierarchy and metadata |
| IDB file | `.idb` | Lists every file with its mode, owner, destination, and attributes |

`gendist` reads both, assembles the software images, and writes the
distribution to an output directory. You then `tar` that directory into
a `.tardist`.

---

## 2. The Spec File

The spec file describes the package structure. All keywords are lowercase.
Use tabs for indentation (the spec parser accepts spaces too, but tabs are
conventional).

### Correct keyword: `subsys`, not `subsystem`

The inner block keyword is **`subsys`** / **`endsubsys`** — NOT `subsystem`.
This is a common mistake. `gendist` gives "unrecognized image component" if
you use `subsystem`.

### Minimal spec file

```
product myapp
	id "My Application 1.0"
	image sw
		id "My Application Software"
		version 1
		order 0
		subsys base
			id "My Application"
			default
			exp "myapp.sw.base"
		endsubsys
	endimage
endproduct
```

### Spec file attributes

| Attribute | Level | Notes |
|---|---|---|
| `id "string"` | product, image, subsys | Description shown in Software Manager |
| `version N` | image | Integer; increment with each release |
| `order N` | image | Install order (lower = earlier); default 9999 |
| `default` | subsys | Mark for default installation by inst |
| `required` | subsys | Disallow removal (use sparingly) |
| `exp "expression"` | subsys | Which IDB tags belong to this subsystem |
| `prereq (subsys-range)` | subsys | Prerequisite subsystems |
| `replaces subsys-range` | subsys | Subsystems this one supersedes |
| `obsoletes subsys` | subsys | Shorthand for `replaces subsys 0 maxint` |

### The `exp` expression

`exp` selects which files from the IDB belong to this subsystem. The value
is a boolean expression over IDB tag names.

Simplest case — tag matches subsystem name exactly:
```
exp "myapp.sw.base"
```

Files not in any man directory:
```
exp "!(dstpath=~'*/man/*'||dstpath=~'*/catman/*')"
```

---

## 3. The IDB File

The IDB (Installation Database) has one line per file. The sort order must
be by fields 4–5 (source path and destination path). Always sort before
passing to `gendist`:

```sh
sort +4 -6 myapp.idb > myapp-sorted.idb
```

### IDB line format

```
type mode owner group source destination tag [attributes]
```

| Field | Description |
|---|---|
| type | `f` = file, `d` = directory, `l` = symlink |
| mode | Octal permissions (e.g. `0755`) |
| owner | File owner (e.g. `root`) |
| group | File group (e.g. `sys`) |
| source | Path relative to the source tree root |
| destination | Install path (no leading `/`) |
| tag | Which subsystem (e.g. `myapp.sw.base`) |
| attributes | Optional: `nostrip`, `config(...)`, `exitop(...)`, etc. |

### Examples

```
# Directory
d 0755 root sys - usr/bin myapp.sw.base

# Executable binary
f 0755 root sys myapp usr/bin/myapp myapp.sw.base nostrip

# Config file that survives upgrades
f 0644 root sys defaults/myapp.conf etc/myapp/myapp.conf myapp.sw.base config(suggest)

# Init script with post-install and removal hooks
f 0755 root sys scripts/myapp.init etc/init.d/myapp myapp.sw.base exitop("...") removeop("...")
```

### `config` attribute

| Value | Behavior on upgrade when user has modified the file |
|---|---|
| `config(update)` | Save old as `file.O`, install new version |
| `config(suggest)` | Install new version as `file.N`; leave user's version untouched |
| `config(noupdate)` | Do not install new version at all |

### `nostrip` attribute

By default `gendist` strips executables. Add `nostrip` to preserve symbols.
If the host's `strip` is not the native IRIX strip (e.g. SGUG-RSE strip is
in PATH), pass `-nostrip` to `gendist` instead to avoid errors.

### Ops attributes (`exitop`, `removeop`, `preop`, `postop`)

Shell commands to run at install/remove time. Enclose in double quotes;
separate multiple commands with `;`.

| Attribute | When it runs |
|---|---|
| `preop("cmd")` | Just before the file is installed |
| `postop("cmd")` | Just after the file is installed |
| `exitop("cmd")` | After the user quits Software Manager (post-install) |
| `removeop("cmd")` | After the subsystem is removed (not on upgrade) |

Available environment variables in ops:
- `$rbase` — root installation directory (normally `/`)
- `$dist` — distribution directory

Example exitop for a daemon that registers with chkconfig:
```
exitop("/sbin/chkconfig -f myapp off; if [ ! -L /etc/rc2.d/S75myapp ]; then ln -s ../init.d/myapp /etc/rc2.d/S75myapp; fi")
```

**Do not use `exitop` to create directories** — `inst` cannot account for
their disk space. Create directories with `d` lines in the IDB instead.

---

## 4. Command-Line Workflow (`gendist`)

```sh
# 1. Build your software and place files in a staging tree
make install DESTDIR=/tmp/myapp-stage

# 2. Write myapp.spec and myapp.idb by hand (or generate with swpkg)

# 3. Sort the IDB (required by gendist)
sort +4 -6 myapp.idb > /tmp/myapp-sorted.idb

# 4. Run gendist
gendist \
    -spec myapp.spec \
    -idb /tmp/myapp-sorted.idb \
    -root /tmp/myapp-stage \
    -dist /tmp/myapp-dist \
    -nostrip

# 5. Bundle into a tardist
( cd /tmp/myapp-dist && tar cf - . ) > myapp-1.0-irix65.tardist

# 6. Test install
inst -f myapp-1.0-irix65.tardist
```

### Key `gendist` flags

| Flag | Purpose |
|---|---|
| `-spec file` | Spec file path |
| `-idb file` | IDB file path (must be sorted) |
| `-root dir` | Source tree root (IDB source paths are relative to this) |
| `-dist dir` | Output directory |
| `-nostrip` | Do not strip executables |
| `-nocompress` | Do not compress images |
| `-verbose` | Show progress |
| `-ignoreempty` | Skip empty subsystems without error |
| `-genspec` | Auto-generate a spec from the IDB (useful as a starting point) |

### Auto-generating a spec

`gendist -genspec` reads the IDB and creates a starting spec. Useful to
see the correct format, but the generated tags and expressions need manual
tuning for a real product:

```sh
mkdir -p /tmp/genspec-test/etc
cp myapp-sorted.idb /tmp/genspec-test/etc/idb
gendist -genspec -rbase /tmp/genspec-test myapp.sw
cat /tmp/genspec-test/etc/spec
```

---

## 5. GUI Workflow (`swpkg`)

`swpkg` is a Motif application installed with the `dev.sw.swpkg` product.
It provides a graphical front end that writes the spec and IDB files and
then calls `gendist` for you.

### Starting swpkg

```sh
swpkg &
```

The window has five worksheet tabs across the top:

1. **Create Product Hierarchy** — define the product/image/subsystem tree
2. **Tag Files** — add files and assign them to subsystems
3. **Edit Permissions & Destinations** — set modes, owners, install paths
4. **Add Attributes** — add exitop, config, nostrip, etc.
5. **Build Product** — run gendist and produce the distribution

Work through the tabs left to right for a new package.

### Tab 1 — Create Product Hierarchy

- Edit the Product Name field (short name, no leading digit).
- Edit the Product Description (shown in Software Manager listings; start
  with the product's marketing name).
- Add images with the Add button (select the product node first).
- Add subsystems to each image (select the image node first).
- Name images and subsystems following SGI convention (`sw`, `man`, `base`,
  `manpages`, `relnotes`).
- Set `version` and `order` numbers in the Image Specification sheet.
- Check `default` for subsystems that install by default.
- Save the spec file: File → Save → Spec.

### Tab 2 — Tag Files

- Use the File Browser to navigate to your source tree.
- Select files and directories, then click the Add arrow to add them to
  the IDB.
- Select the correct subsystem tag in the Tags Browser before adding files
  (or add first and reassign).
- Each file in the IDB list shows its current tag.
- Save the IDB file: File → Save → IDB.

### Tab 3 — Edit Permissions & Destinations

- Set the Source Tree Root (the root that gets stripped from source paths).
- For each file, set: Mode (octal), Owner, Group, Destination Directory,
  Destination Filename.
- Click Assign after each change.
- Save the IDB: File → Save → IDB.

### Tab 4 — Add Attributes

- Select a file in the IDB Viewer.
- Check the attribute checkbox (`exitop`, `config`, `nostrip`, etc.).
- Type the attribute value in the text field below the checkbox.
- Click Assign.
- Repeat for each file that needs attributes.
- Save the IDB: File → Save → IDB.

### Tab 5 — Build Product

- Set the Distribution Directory (where output goes; default `/usr/dist`).
- Click **Test Build** to do a dry run — errors appear in the message area.
- Fix any errors, then click **Build All**.
- Output files appear in the distribution directory.
- Bundle into a tardist manually:
  ```sh
  ( cd /usr/dist && tar cf - . ) > myapp-1.0-irix65.tardist
  ```

---

## 6. Testing Your Package

```sh
# List what inst would install (interactive)
inst -f myapp-1.0-irix65.tardist

# At the Inst> prompt:
list               # show subsystems
go                 # install
quit               # exit

# After installing, verify files:
versions           # list installed products
showfiles myapp    # list files installed by myapp
```

To uninstall:
```sh
versions remove myapp
```

---

## 7. Common Mistakes

| Mistake | Symptom | Fix |
|---|---|---|
| Using `subsystem` in spec | "unrecognized image component" at line N | Change to `subsys` / `endsubsys` |
| Unsorted IDB | gendist parse errors | `sort +4 -6 myapp.idb > sorted.idb` |
| SGUG-RSE `strip` in PATH | `strip: invalid option -- u` warnings | Pass `-nostrip` to gendist |
| `exitop` used to create directories | Directory missing from disk space accounting | Use `d` lines in IDB for directories |
| `config(suggest)` file not preserved | User's modified file overwritten | Ensure `config(suggest)` is on the IDB line |
| Source path not relative to `-root` | "N of M entries not found under Source Root" | Strip the source root prefix from IDB source paths |
