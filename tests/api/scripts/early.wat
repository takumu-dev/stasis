;; A script built against early.stasis at 0.3.0, whose line is 0.3, as the host's 0.3.1 is.
(module
  (memory (export "memory") 1)
  (func (export "test:early/caller@0.3#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 3)))
