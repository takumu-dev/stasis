;; A script that a snapshot can't hold for several reasons at once: its memory isn't exported, it
;; grows a table, and it has a mutable global that it doesn't export and that holds a reference.
(module
  (memory 1)
  (table 1 funcref)
  (global (mut externref) (ref.null extern))
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (table.grow (ref.null func) (i32.const 1))))
