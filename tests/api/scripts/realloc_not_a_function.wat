;; Exports `user.echo` from strings.stasis, which takes a string, and a global as its cabi_realloc.
(module
  (memory (export "memory") 1)
  (global (export "cabi_realloc") i32 (i32.const 0))
  (func (export "test:strings/user@1#echo:150b24eac1c4a9c1") (param i32 i32) (result i32)
    (i32.const 64)))
