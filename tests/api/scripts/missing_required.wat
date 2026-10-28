;; A script that provides an optional entry point of calls.stasis, but not `starts`, which it
;; requires.
(module
  (memory (export "memory") 1)
  (func (export "test:calls/caller@1#echo_u8:4618eff65f831824") (param i32) (result i32)
    (local.get 0)))
