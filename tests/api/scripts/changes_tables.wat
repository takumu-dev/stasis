;; A script with each instruction that changes a table or drops a segment, which a snapshot can't
;; hold: it holds neither tables nor which segments were dropped. `table.set` is in two functions.
(module
  (memory (export "memory") 1)
  (table 2 funcref)
  (elem $functions func $nothing)
  (data $bytes "bytes")
  (func $nothing)
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0))
  (func $sets
    (table.set (i32.const 0) (ref.null func)))
  (func $grows
    (drop (table.grow (ref.null func) (i32.const 1))))
  (func $fills
    (table.fill (i32.const 0) (ref.null func) (i32.const 1)))
  (func $copies
    (table.copy (i32.const 0) (i32.const 1) (i32.const 1)))
  (func $initialises
    (table.init $functions (i32.const 0) (i32.const 0) (i32.const 1)))
  (func $drops_elements
    (elem.drop $functions))
  (func $drops_data
    (data.drop $bytes))
  (func $sets_again
    (table.set (i32.const 1) (ref.null func))))
