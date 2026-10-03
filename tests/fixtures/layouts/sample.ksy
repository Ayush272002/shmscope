meta:
  id: sample_ring
  title: sample ring buffer
  endian: le
  encoding: ASCII
-shmscope-formats:
  price: {kind: scaled, digits: 4}
  side: {kind: enum, values: {0: buy, 1: sell, -1: none}}
  stamp: {kind: timestamp, unit: ns}
seq:
  - id: header
    type: header
  - id: writer_sequence
    type: u8
    -shmscope-format: hex
instances:
  records:
    pos: header.records_at
    size: header.record_size
    type:
      switch-on: header.kind
      cases:
        1: trade
        2: quote
        _: raw
    repeat: expr
    repeat-expr: header.count
types:
  header:
    seq:
      - {contents: "SAMPLE01"}
      - {id: kind, type: u4}
      - {id: record_size, type: u4}
      - {id: count, type: u8}
      - {id: records_at, type: u8}
      - {id: name, type: str, size: 16}
      - size: 24
  trade:
    seq:
      - {id: sent_at, type: u8, -shmscope-format: stamp}
      - {id: price, type: s8, -shmscope-format: price}
      - {id: side, type: s1, -shmscope-format: side}
      - {id: qty, type: u4le}
  quote:
    seq:
      - {id: bid, type: s8, -shmscope-format: price}
      - {id: ask, type: s8, -shmscope-format: price}
  raw:
    seq:
      - {id: bytes, size: 16}
