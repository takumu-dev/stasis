;; A script with two memories, where a snapshot holds exactly one. WAMR, as Stasis builds it, has
;; no multiple memories, so its loader refuses it.
(module
  (memory (export "memory") 1)
  (memory (export "other") 1)
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
