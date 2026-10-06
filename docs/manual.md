# shmscope

Live terminal viewer for POSIX shared memory.
{ .man-lead }

## Synopsis

**shmscope** [*name*] [**-l** *file*] [**--hz** *rate*]

**shmscope** **-v** | **--version**

## Description

**shmscope** maps a POSIX shared memory object read only and shows its bytes
in the terminal, refreshing while the writer runs. Changed bytes are
highlighted.

With a layout file the bytes are decoded into named fields shown beside the
hex. Without a *name*, **shmscope** opens a launcher listing recently opened
segments.

## Options

*name*
:   The shared memory object to open, for example */shmscope-demo*.

**-l**, **--layout** *file*
:   Load a layout file (*.ksy*, *.yaml* or *.json*) and overlay it on the
    bytes. The file must exist.

**--hz** *rate*
:   Refresh rate in updates per second, from 1 to 60. The default is 15.

**-v**, **--version**
:   Print the version and exit.

**-h**, **--help**
:   Print a usage summary and exit.

## Keys

<kbd>Up</kbd>, <kbd>Down</kbd>, <kbd>Left</kbd>, <kbd>Right</kbd>
:   Move the cursor.

<kbd>PgUp</kbd>, <kbd>PgDn</kbd>
:   Move one page.

<kbd>Home</kbd>, <kbd>End</kbd>
:   Go to the first or last row.

<kbd>Space</kbd>
:   Freeze or resume live updates.

<kbd>f</kbd>
:   Follow the writer.

<kbd>i</kbd>
:   Switch between the fields pane and the inspector. Needs a layout.

<kbd>/</kbd>
:   Open the command bar. <kbd>Tab</kbd> completes commands and file paths.

<kbd>q</kbd>, <kbd>Esc</kbd>
:   Go back, or quit from the launcher.

The mouse wheel scrolls whichever pane it is over. Hold <kbd>Option</kbd> to
select text.

## Commands

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

## Layouts

Layouts are Kaitai Struct *.ksy* files in YAML or JSON. shmscope reads a
subset: little endian, integers, floats, `str`, `contents`, nested types,
`instances` with `pos`, `repeat: expr` and `switch-on`.

Display formats are a shmscope extension, declared under `-shmscope-formats`
and attached with `-shmscope-format`. Kinds are `decimal`, `hex`, `scaled`,
`enum` and `timestamp`.

The [layout guide](layouts.md) covers each feature with examples.

## Files

*$XDG_STATE_HOME/shmscope/recent*
:   Recently opened segments, shown in the launcher. Falls back to
    *~/.local/state/shmscope/recent* when **XDG_STATE_HOME** is unset.

## Environment

**XDG_STATE_HOME**
:   Where the recent list is kept.

**HOME**
:   Used for the fallback state directory and to expand *~* in paths.

## Notes

!!! warning ""
    The mapping is read only and **shmscope** never writes to it.

    Reads are not synchronised with the writer, so a value can tear mid write.

    If the writer removes or recreates the segment, the title says so and
    **/reopen** picks up the new one.

## Examples

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

## See also

[`shm_open(3)`](https://man7.org/linux/man-pages/man3/shm_open.3.html),
[`mmap(2)`](https://man7.org/linux/man-pages/man2/mmap.2.html)
