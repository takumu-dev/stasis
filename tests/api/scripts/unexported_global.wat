;; A script that defines a mutable global without exporting it, so a snapshot couldn't hold its
;; value. Its other globals are exported, or immutable. `starts` returns the unexported global's
;; value, and `tick` sets it.
(module
  (memory (export "memory") 1)
  (global (export "exported") (mut i32) (i32.const 0))
  (global $hidden (mut i64) (i64.const 7))
  (global i32 (i32.const 0))
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.wrap_i64 (global.get $hidden)))
  (func (export "test:calls/caller@1#tick:37ab64a28242fa4b") (param i64)
    (global.set $hidden (local.get 0))))
