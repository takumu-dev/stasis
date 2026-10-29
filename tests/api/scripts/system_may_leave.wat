;; A script whose `cabi_realloc` and `cabi_post_` call functions of the system interface, which a
;; script may not do while Stasis is inside either: its `cabi_realloc` calls `sched_yield`, and the
;; `cabi_post_` of `give` calls `proc_exit`. It provides `take` and `give`, as system.stasis says.
(module
  (import "wasi_snapshot_preview1" "proc_exit" (func $proc_exit (param i32)))
  (import "wasi_snapshot_preview1" "sched_yield" (func $sched_yield (result i32)))
  (memory (export "memory") 1)
  (func (export "cabi_realloc") (param i32 i32 i32 i32) (result i32)
    (drop (call $sched_yield))
    (i32.const 1024))
  (func (export "test:system/user@1#take:76a67b5b4fa368b5") (param i32 i32))
  ;; Returns the 2 bytes at 16, through its return area at 8.
  (func (export "test:system/user@1#give:ae6637fc59233091") (result i32)
    (i32.store (i32.const 8) (i32.const 16))
    (i32.store (i32.const 12) (i32.const 2))
    (i32.const 8))
  (func (export "cabi_post_test:system/user@1#give:ae6637fc59233091") (param i32)
    (call $proc_exit (i32.const 0)))
  (data (i32.const 16) "ok"))
