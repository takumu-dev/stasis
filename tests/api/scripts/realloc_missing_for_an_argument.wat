;; Exports `user.echo` from strings.stasis, which takes a string, but doesn't export the
;; cabi_realloc through which Stasis would put it into the script's memory.
(module
  (memory (export "memory") 1)
  (func (export "test:strings/user@1#echo:150b24eac1c4a9c1") (param i32 i32) (result i32)
    (i32.const 64)))
