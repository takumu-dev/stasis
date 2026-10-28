;; A script built against early.stasis at 0.2, which under 0.x is another line than 0.3.
(module
  (memory (export "memory") 1)
  (func (export "test:early/caller@0.2#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 2)))
