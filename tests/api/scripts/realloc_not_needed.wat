;; Takes and returns strings through strings.stasis in ways that never make Stasis put a value into
;; its memory, so it needs no cabi_realloc: a host function that takes a string, and an entry point
;; that returns one, as `user.pass_at` and `user.return_at` do in strings.wat.
(module
  (import "test:strings/host@1" "take:76a67b5b4fa368b5" (func $take (param i32 i32)))
  (memory (export "memory") 1)
  (data (i32.const 1024) "\00\08\00\00\05\00\00\00")
  (data (i32.const 2048) "plain")
  (func (export "test:strings/user@1#pass_at:bcbfa7c332b66f12") (param i32 i32)
    (call $take (local.get 0) (local.get 1)))
  (func (export "test:strings/user@1#return_at:b214348321a9caba") (param i32) (result i32)
    (local.get 0)))
