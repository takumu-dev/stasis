;; A script built for another interface, `other:calls`, with the same kind of script and entry
;; point as calls.stasis.
(module
  (memory (export "memory") 1)
  (func (export "other:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
