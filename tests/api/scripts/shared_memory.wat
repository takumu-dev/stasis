;; A script whose memory is shared, which more than one thread may change at once, so a snapshot
;; couldn't hold it. WAMR, as Stasis builds it, has no shared memory, so its loader refuses it.
(module
  (memory (export "memory") 1 1 shared)
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
