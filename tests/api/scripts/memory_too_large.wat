;; A script whose memory starts with 65,536 pages, which WebAssembly allows but an instance of a
;; script can't have. It's otherwise a script calls.stasis loads.
(module
  (memory (export "memory") 65536)
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
