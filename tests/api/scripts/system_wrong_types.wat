;; A script that imports from `wasi_snapshot_preview1` two functions of the system interface as
;; functions of other types, `fd_write` with a parameter too few and `proc_exit` with a result, and
;; one that the system interface lacks. Its import of `sched_yield` is of the system interface's
;; type, and its import of a global breaks only the rule that a script imports nothing else but
;; functions. It's otherwise a script system.stasis loads.
(module
  (import "wasi_snapshot_preview1" "fd_write" (func (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sched_yield" (func (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_frobnicate" (func (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "clock" (global i64))
  (memory (export "memory") 1))
