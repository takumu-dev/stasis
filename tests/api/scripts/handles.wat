;; The script that the tests of handles call. Its entry points do as handles.stasis says, passing
;; handles to the host as the numbers they're given.
;;
;; The first bytes of its memory hold:
;;     8   the address cabi_realloc allocates from next, which starts at 8192
;;     256 the return area of `echo_hit`
;;     320 the return area that `fetch_hit` passes `combat.last_hit`, and returns
(module
  (import "test:handles/world@1" "entity.poke:94b4a8302b1e7a7c" (func $poke (param i32)))
  (import "test:handles/world@1" "item.weight:67d6bb51405d0400"
    (func $weight (param i32) (result i32)))
  (import "test:handles/world@1" "find:9bd12ac0e1778d83" (func $find (param i32) (result i32)))
  (import "test:handles/world@1" "pick_up:48463e64f7740793"
    (func $pick_up (param i32) (result i32)))
  (import "test:handles/world@1" "touch:94b4a8302b1e7a7c" (func $touch (param i32)))
  (import "test:handles/world@1" "meet:32f50a3ded3cb8a2" (func $meet (param i32 i32)))
  (import "test:handles/world@1" "kill:94b4a8302b1e7a7c" (func $kill (param i32)))
  (import "test:handles/combat@1" "last_hit:ba0bc7a05a42f758" (func $last_hit (param i32 i32)))
  (import "test:handles/combat@1" "alarm:8d910d2e940c01a7"
    (func $alarm (param i32 i32 i32 i32 f32 f32) (result i32)))
  (import "test:handles/combat@1" "take_hits:a6097731f66a805f" (func $take_hits (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 8) "\00\20\00\00")

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

  (func (export "test:handles/npc@1#find:4d8a9a4559ed9204") (param i32) (result i32)
    (call $find (local.get 0)))
  (func (export "test:handles/npc@1#pick_up:4d8a9a4559ed9204") (param i32) (result i32)
    (call $pick_up (local.get 0)))
  (func (export "test:handles/npc@1#touch:e492f253b87ebe82") (param i32)
    (call $touch (local.get 0)))
  (func (export "test:handles/npc@1#meet:bcbfa7c332b66f12") (param i32 i32)
    (call $meet (local.get 0) (local.get 1)))
  (func (export "test:handles/npc@1#kill:e492f253b87ebe82") (param i32)
    (call $kill (local.get 0))
    (call $touch (local.get 0)))
  (func (export "test:handles/npc@1#poke:e492f253b87ebe82") (param i32)
    (call $poke (local.get 0)))
  (func (export "test:handles/npc@1#weigh:4d8a9a4559ed9204") (param i32) (result i32)
    (call $weight (local.get 0)))

  (func (export "test:handles/npc@1#greet:7eb3592ec030eb54") (param i32) (result i32)
    (local.get 0))
  (func (export "test:handles/npc@1#choose:9bd12ac0e1778d83") (param i32) (result i32)
    (local.get 0))

  ;; Lays out a hit at 256, as the canonical ABI does: its handle, the weapon's address and size,
  ;; the damage, and the `vec2`, 4 bytes each.
  (func (export "test:handles/npc@1#echo_hit:49bac6475a7d53eb")
    (param i32 i32 i32 i32 f32 f32) (result i32)
    (i32.store (i32.const 256) (local.get 0))
    (i32.store (i32.const 260) (local.get 1))
    (i32.store (i32.const 264) (local.get 2))
    (i32.store (i32.const 268) (local.get 3))
    (f32.store (i32.const 272) (local.get 4))
    (f32.store (i32.const 276) (local.get 5))
    (i32.const 256))
  (func (export "test:handles/npc@1#relay_hit:8d910d2e940c01a7")
    (param i32 i32 i32 i32 f32 f32) (result i32)
    (call $alarm
      (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4) (local.get 5)))
  ;; The hit's weapon is empty, and its damage and place are 0.
  (func (export "test:handles/npc@1#alarm_by:4d8a9a4559ed9204") (param i32) (result i32)
    (call $alarm
      (local.get 0) (i32.const 0) (i32.const 0) (i32.const 0) (f32.const 0) (f32.const 0)))
  (func (export "test:handles/npc@1#fetch_hit:07c6891388f24ae2") (param i32) (result i32)
    (call $last_hit (local.get 0) (i32.const 320))
    (i32.const 320))
  (func (export "test:handles/npc@1#pass_hits:a6097731f66a805f") (param i32)
    (call $take_hits (local.get 0)))
  (func (export "test:handles/npc@1#return_hit_at:07c6891388f24ae2") (param i32) (result i32)
    (local.get 0))

  (func (export "test:handles/npc@1#load:4d8a9a4559ed9204") (param i32) (result i32)
    (i32.load (local.get 0)))
  (func (export "test:handles/npc@1#store:bcbfa7c332b66f12") (param i32 i32)
    (i32.store (local.get 0) (local.get 1))))
