;; The script that the tests of snapshots and restores call. Its entry points do as snapshots.stasis
;; says, and its state lies in its memory, which starts with one page and grows to at most four, in
;; its mutable globals, one of each numeric type beside the count of starts, and in its handles. An
;; immutable global sits between them, so that the mutable ones aren't numbered in a row. It
;; exports its memory and its mutable globals, one of them twice, so it's snapshottable.
;;
;; The first bytes of its memory hold:
;;     8   the address cabi_realloc allocates from next, which starts at 1024
;;     16  the address and size of the text `remember` keeps
;;     32  the return area of `recall`
(module
  (import "test:snapshots/world@1" "find:9bd12ac0e1778d83" (func $find (param i32) (result i32)))
  (import "test:snapshots/world@1" "pick_up:48463e64f7740793"
    (func $pick_up (param i32) (result i32)))
  (import "test:snapshots/world@1" "touch:94b4a8302b1e7a7c" (func $touch (param i32)))
  (memory (export "memory") 1 4)
  (data (i32.const 8) "\00\04\00\00")
  (global $starts (export "starts") (mut i32) (i32.const 0))
  (global $fixed i32 (i32.const 7))
  (global $narrow (export "narrow") (mut i32) (i32.const 0))
  (global $wide (export "wide") (mut i64) (i64.const 0))
  (global $single (export "single") (export "single_again") (mut f32) (f32.const 0))
  (global $double (export "double") (mut f64) (f64.const 0))

  (func (export "stasis_initialize")
    (global.set $starts (i32.add (global.get $starts) (i32.const 1))))

  ;; Allocates `size` bytes aligned to `align` from the address at 8, growing memory if they don't
  ;; fit, and returns where they are, or null if memory can't grow.
  (func $cabi_realloc (export "cabi_realloc")
    (param $old i32) (param $old_size i32) (param $align i32) (param $size i32) (result i32)
    (local $at i32)
    (local $end i32)
    (local $memory i32)
    (local.set $at
      (i32.and
        (i32.add (i32.load (i32.const 8)) (i32.sub (local.get $align) (i32.const 1)))
        (i32.sub (i32.const 0) (local.get $align))))
    (local.set $end (i32.add (local.get $at) (local.get $size)))
    (local.set $memory (i32.mul (memory.size) (i32.const 65536)))
    (if (i32.gt_u (local.get $end) (local.get $memory))
      (then
        (if (i32.eq
              (memory.grow
                (i32.shr_u
                  (i32.add (i32.sub (local.get $end) (local.get $memory)) (i32.const 65535))
                  (i32.const 16)))
              (i32.const -1))
          (then (return (i32.const 0))))))
    (i32.store (i32.const 8) (local.get $end))
    (local.get $at))

  (func (export "test:snapshots/keeper@1#starts:5b69886a8b9e31a8") (result i32)
    (global.get $starts))

  (func (export "test:snapshots/keeper@1#set_globals:9d5679af96847f6e")
    (param i32 i64 f32 f64)
    (global.set $narrow (local.get 0))
    (global.set $wide (local.get 1))
    (global.set $single (local.get 2))
    (global.set $double (local.get 3)))
  (func (export "test:snapshots/keeper@1#global_i32:5b69886a8b9e31a8") (result i32)
    (global.get $narrow))
  (func (export "test:snapshots/keeper@1#global_i64:b13376f9693dd6bd") (result i64)
    (global.get $wide))
  (func (export "test:snapshots/keeper@1#global_f32:676becf213550e79") (result f32)
    (global.get $single))
  (func (export "test:snapshots/keeper@1#global_f64:0465451bbeea62d2") (result f64)
    (global.get $double))

  (func (export "test:snapshots/keeper@1#grow:4d8a9a4559ed9204") (param i32) (result i32)
    (memory.grow (local.get 0)))
  (func (export "test:snapshots/keeper@1#pages:5b69886a8b9e31a8") (result i32)
    (memory.size))
  (func (export "test:snapshots/keeper@1#store:bcbfa7c332b66f12") (param i32 i32)
    (i32.store (local.get 0) (local.get 1)))
  (func (export "test:snapshots/keeper@1#load:4d8a9a4559ed9204") (param i32) (result i32)
    (i32.load (local.get 0)))

  ;; The text stays where Stasis put it, in memory cabi_realloc allocated, and is never freed.
  (func (export "test:snapshots/keeper@1#remember:76a67b5b4fa368b5") (param i32 i32)
    (i32.store (i32.const 16) (local.get 0))
    (i32.store (i32.const 20) (local.get 1)))
  (func (export "test:snapshots/keeper@1#recall:ae6637fc59233091") (result i32)
    (i32.store (i32.const 32) (i32.load (i32.const 16)))
    (i32.store (i32.const 36) (i32.load (i32.const 20)))
    (i32.const 32))

  (func (export "test:snapshots/keeper@1#find:4d8a9a4559ed9204") (param i32) (result i32)
    (call $find (local.get 0)))
  (func (export "test:snapshots/keeper@1#pick_up:4d8a9a4559ed9204") (param i32) (result i32)
    (call $pick_up (local.get 0)))
  (func (export "test:snapshots/keeper@1#touch:e492f253b87ebe82") (param i32)
    (call $touch (local.get 0))))
