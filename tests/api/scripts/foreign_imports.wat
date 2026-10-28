;; A script that imports from modules a script may not import from: `env`, and WASI's older
;; `wasi_unstable`. Its import from `wasi_snapshot_preview1` is one a script may make. It's
;; otherwise a script calls.stasis loads.
(module
  (import "env" "log" (func (param i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func (param i32)))
  (import "wasi_unstable" "proc_exit" (func (param i32)))
  (memory (export "memory") 1)
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
