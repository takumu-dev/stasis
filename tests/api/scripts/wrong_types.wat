;; A script whose entry points' names and hashes are right, but whose exports aren't what Stasis
;; calls: `starts` takes a parameter, `tick` is a global, and `stasis_initialize` takes a
;; parameter.
(module
  (memory (export "memory") 1)
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (param i32) (result i32)
    (local.get 0))
  (global (export "test:calls/caller@1#tick:37ab64a28242fa4b") i64 (i64.const 0))
  (func (export "stasis_initialize") (param i32)))
