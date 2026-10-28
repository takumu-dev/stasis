;; A script with a start section, which every backend runs when it creates an instance, so that it
;; would run again in each restored one. It's otherwise a script calls.stasis loads.
(module
  (memory (export "memory") 1)
  (func $start)
  (start $start)
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
