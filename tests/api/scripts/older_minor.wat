;; A script with only the entry point that calls.stasis requires, as one built against an older
;; minor version of the interface, before its optional entry points were added, would be. It has
;; no stasis_initialize, so starting it runs nothing.
(module
  (memory (export "memory") 1)
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
