;; A script built against a newer minor version of calls.stasis, which added the optional entry
;; point `added_later`. Stasis ignores it, as it ignores the exports it gives no meaning to.
(module
  (memory (export "memory") 1)
  (global (export "counter") (mut i32) (i32.const 0))
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0))
  (func (export "test:calls/caller@1#added_later:0123456789abcdef") (param i32)
    (unreachable))
  (func (export "helper") (result i32)
    (i32.const 1)))
