;; A script whose memory isn't exported, so a snapshot couldn't read or write it.
(module
  (memory 1)
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
