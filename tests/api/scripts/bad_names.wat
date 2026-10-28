;; A script whose names break every rule loading checks them by, after the interface and its line.
;; Its `starts` is right.
(module
  (import "test:calls/world" "find:0123456789abcdef" (func))
  (memory (export "memory") 1)
  (func $starts (result i32)
    (i32.const 0))
  (func $echo (param i32) (result i32)
    (local.get 0))
  (func $tick (param i64))
  (export "test:calls/caller@1#starts:5b69886a8b9e31a8" (func $starts))
  ;; The full version, as wit-bindgen writes names, rather than the line.
  (export "test:calls/caller@1.2.0#tick:37ab64a28242fa4b" (func $tick))
  ;; No hash.
  (export "test:calls/caller@1#tick" (func $tick))
  (export "foo:bar" (func $starts))
  ;; Another kind of script.
  (export "test:calls/boss@1#tick:37ab64a28242fa4b" (func $tick))
  ;; One entry point exported twice.
  (export "test:calls/caller@1#echo_u8:4618eff65f831824" (func $echo))
  (export "test:calls/caller@1#echo_u8:1a6e70e5cfc850c4" (func $echo)))
