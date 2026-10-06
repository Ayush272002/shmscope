---
title: SHMSCOPE
section: 1
header: User Commands
footer: shmscope
---

# NAME

shmscope - live terminal viewer for POSIX shared memory

# SYNOPSIS

**shmscope** [*name*] [**-l** *file*] [**\-\-hz** *rate*]

**shmscope** **-v** | **\-\-version**

# DESCRIPTION

**shmscope** maps a POSIX shared memory object read only and shows its bytes
in the terminal, refreshing while the writer runs. Changed bytes are
highlighted.

With a layout file the bytes are decoded into named fields shown beside the
hex. Without a *name*, **shmscope** opens a launcher listing recently opened
segments.

# OPTIONS

*name*
:   The shared memory object to open, for example */shmscope-demo*.

**-l**, **\-\-layout** *file*
:   Load a layout file (*.ksy*, *.yaml* or *.json*) and overlay it on the
    bytes. The file must exist.

**\-\-hz** *rate*
:   Refresh rate in updates per second, from 1 to 60. The default is 15.

**-v**, **\-\-version**
:   Print the version and exit.

**-h**, **\-\-help**
:   Print a usage summary and exit.

# KEYS

**Up**, **Down**, **Left**, **Right**
:   Move the cursor.

**PgUp**, **PgDn**
:   Move one page.

**Home**, **End**
:   Go to the first or last row.

**Space**
:   Freeze or resume live updates.

**f**
:   Follow the writer.

**i**
:   Switch between the fields pane and the inspector. Needs a layout.

**/**
:   Open the command bar. **Tab** completes commands and file paths.

**q**, **Esc**
:   Go back, or quit from the launcher.

The mouse wheel scrolls whichever pane it is over. Hold **Option** to select
text.

# COMMANDS

Commands are typed after **/**. Only the commands that apply right now are
offered.

**/jump** *offset*
:   Scroll to a hex offset.

**/field** *path*
:   Track a field as it moves, for example *latest.sequence*. Needs a layout.

**/untrack**
:   Stop tracking the field.

**/layout** *file*|**off**|**reload**
:   Load, remove or reload a layout.

**/fields**, **/inspector**
:   Show the layout's fields, or every type decoded at the cursor.

**/live**, **/hex**
:   Show only the rows that are changing, or the whole mapping again.

**/follow**, **/unfollow**
:   Keep the view on the writer, or stop.

**/freeze**, **/unfreeze**
:   Pause or resume live updates.

**/top**, **/bottom**
:   Go to the first or last row.

**/reopen**
:   Open whatever the name points at now.

**/close**
:   Go back to the launcher.

# LAYOUTS

Layouts are Kaitai Struct *.ksy* files in YAML or JSON. **shmscope** reads a
subset: little endian, integers, floats, **str**, **contents**, nested types,
**instances** with **pos**, **repeat: expr** and **switch-on**.

Display formats are a **shmscope** extension, declared under
**-shmscope-formats** and attached with **-shmscope-format**. Kinds are
**decimal**, **hex**, **scaled**, **enum** and **timestamp**.

# FILES

*$XDG_STATE_HOME/shmscope/recent*
:   Recently opened segments, shown in the launcher. Falls back to
    *~/.local/state/shmscope/recent* when **XDG_STATE_HOME** is unset.

# ENVIRONMENT

**XDG_STATE_HOME**
:   Where the recent list is kept.

**HOME**
:   Used for the fallback state directory and to expand *~* in paths.

# NOTES

The mapping is read only and **shmscope** never writes to it.

Reads are not synchronised with the writer, so a value can tear mid write.

If the writer removes or recreates the segment, the title says so and
**/reopen** picks up the new one.

# EXAMPLES

Open a segment with a layout:

```sh
shmscope --layout examples/ring.ksy /shmscope-demo
```

Refresh at 60 Hz:

```sh
shmscope --hz 60 /shmscope-demo
```

Open the launcher:

```sh
shmscope
```

# SEE ALSO

**shm_open**(3), **mmap**(2)

Project site: <https://github.com/Ayush272002/shmscope>
