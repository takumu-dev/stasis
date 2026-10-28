;; A script built against an interface where `starts` returned a u64: its name has that
;; signature's hash, and its export returns an i64 as that signature makes it.
(module
  (memory (export "memory") 1)
  (func (export "test:calls/caller@1#starts:b13376f9693dd6bd") (result i64)
    (i64.const 0)))
