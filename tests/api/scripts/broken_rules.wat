;; A script that breaks every rule loading checks after its signatures and names, one of each, and
;; lacks the entry point calls.stasis requires, which comes first.
(module
  (import "env" "seed" (global i32))
  (memory (export "memory") 65536)
  (table 1 funcref)
  (func $nothing)
  (start $nothing)
  (export "__wasm_call_ctors" (func $nothing))
  (export "test:calls/caller@1#Echo_u8:4618eff65f831824" (func $nothing))
  (elem (i32.const 1) func $nothing))
