;; A script that imports a global, a table and a memory, from a group of calls.stasis, so that each
;; import's module is one a script may import from. It's otherwise a script calls.stasis loads.
(module
  (import "test:calls/world@1" "count" (global i32))
  (import "test:calls/world@1" "things" (table 1 funcref))
  (import "test:calls/world@1" "memory" (memory 1))
  (export "memory" (memory 0))
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
