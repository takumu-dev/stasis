;; A script whose active segments end exactly where its memory and its table do, and so fit, empty
;; ones at the end included. Its `starts` returns the last byte its data put in memory.
(module
  (memory (export "memory") 1)
  (table 3 funcref)
  (func $nothing)

  (data (i32.const 65532) "fits")
  (data (i32.const 65536) "")

  (elem (i32.const 0) func $nothing $nothing)
  (elem (table 0) (i32.const 2) funcref (ref.func $nothing))
  (elem (i32.const 3) func)

  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.load8_u (i32.const 65535))))
