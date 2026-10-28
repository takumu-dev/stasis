;; A script that exports mutable globals that hold references, which a snapshot can't hold, since a
;; reference means something only to the backend that made it. Its immutable one never changes.
(module
  (memory (export "memory") 1)
  (global (export "handler") (mut funcref) (ref.null func))
  (global (export "host_object") (mut externref) (ref.null extern))
  (global (export "fixed") funcref (ref.null func))
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
