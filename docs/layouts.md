---
description: >-
  Describe shared memory with Kaitai Struct .ksy layouts in shmscope:
  supported types, instances, arrays, and display formats for fixed-point
  prices, enums and timestamps.
---

# Layouts

A layout tells shmscope what the bytes mean. With one loaded, the side pane
shows named, decoded fields instead of raw hex, and `/field` can follow a value
as it moves.

Layouts are [Kaitai Struct](https://kaitai.io) `.ksy` files, written in YAML or
JSON. Load one at start with `--layout`, or later with `/layout <file>`.

## Supported subset

shmscope supports part of Kaitai Struct:

| Feature | Notes |
|---|---|
| Endianness | little endian only |
| Integers | `u1`, `u2`, `u4`, `u8`, `s1`, `s2`, `s4`, `s8` |
| Floats | `f4`, `f8` |
| Strings | `str` with `size` (`strz` is not supported yet) |
| Magic bytes | `contents` |
| Nested types | `types:` |
| Instances | `instances` with `pos` |
| Arrays | `repeat: expr` |
| Variants | `switch-on` |

## Display formats

Display formats are a shmscope extension. Declare them once under
`-shmscope-formats` and attach one to a field with `-shmscope-format`:

```yaml
-shmscope-formats:
  price: {kind: scaled, digits: 4}
  side: {kind: enum, values: {1: buy, -1: sell}}

seq:
  - {id: price, type: s8, -shmscope-format: price}
  - {id: side, type: s1, -shmscope-format: side}
```

| Kind | Shows |
|---|---|
| `decimal` | the integer in base 10 |
| `hex` | the integer in base 16 |
| `scaled` | a fixed-point value, `digits` places after the point |
| `enum` | a name from `values` |
| `timestamp` | a wall-clock time, `unit` is `s`, `ms`, `us` or `ns` (default) |

## Example: a ring buffer

The repo ships `examples/ring.ksy`, which describes the demo writer's ring
buffer. `latest` is an instance computed from the header, so
`/field latest.body` always lands on the newest record:

```yaml
instances:
  latest:
    pos: >-
      header.records_offset
      + (hot.sequence - 1) % header.capacity * header.record_size
    size: header.record_size
    type: record
  records:
    pos: header.records_offset
    size: header.record_size
    type: record
    repeat: expr
    repeat-expr: header.capacity
```

The same layout is also in the repo as `ring.yaml` and `ring.json`.

!!! tip "Editing a layout"
    Keep shmscope open while you edit the file and run `/layout reload` to pick
    up the changes without restarting.
