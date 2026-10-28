;; A script with active data and element segments that run past the memory and the table it starts
;; with, which instantiating it would trap on. Beside them are segments that just fit, and passive
;; and declared ones, which are never copied when an instance is created. It's otherwise a script
;; calls.stasis loads.
(module
  (memory (export "memory") 1)
  (table 2 funcref)
  (func $nothing)

  (data (i32.const 65532) "fits")
  (data (i32.const 65533) "late")
  (data (i32.const 65536) "")
  ;; An offset of 2^32 - 1, which is unsigned.
  (data (i32.const -1) "x")
  (data "passive")

  (elem (i32.const 0) func $nothing $nothing)
  (elem (i32.const 1) func $nothing $nothing)
  (elem (table 0) (i32.const 2) funcref (ref.func $nothing))
  (elem func $nothing)
  (elem declare func $nothing)

  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
