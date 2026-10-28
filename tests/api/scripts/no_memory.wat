;; A script with no memory, which loads and runs, but whose state a snapshot can't hold, since a
;; snapshot holds exactly one memory.
(module
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
