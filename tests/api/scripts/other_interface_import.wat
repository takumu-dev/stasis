;; A script whose entry point is calls.stasis's, but which imports from a group of another
;; interface.
(module
  (import "other:calls/world@1" "find:0123456789abcdef" (func))
  (memory (export "memory") 1)
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
