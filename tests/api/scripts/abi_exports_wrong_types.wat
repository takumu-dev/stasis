;; Needs no cabi_realloc, since nothing it uses from strings.stasis makes Stasis put a value into its
;; memory, but exports one of another type than the canonical ABI's, and two cabi_post_* functions
;; of another type than their entry points' results make them: a function where `user.fetch`
;; returns an i32, the address of its return area, and a global where `user.pages` returns one.
(module
  (import "test:strings/host@1" "take:76a67b5b4fa368b5" (func (param i32 i32)))
  (memory (export "memory") 1)
  (global (export "cabi_post_test:strings/user@1#pages:5b69886a8b9e31a8") i32 (i32.const 0))
  (func (export "cabi_realloc") (param i32) (result i32)
    (local.get 0))
  (func (export "test:strings/user@1#fetch:ae6637fc59233091") (result i32)
    (i32.const 64))
  (func (export "cabi_post_test:strings/user@1#fetch:ae6637fc59233091"))
  (func (export "test:strings/user@1#pages:5b69886a8b9e31a8") (result i32)
    (memory.size)))
