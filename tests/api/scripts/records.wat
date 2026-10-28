;; The script that the tests of records call. Its entry points do as records.stasis says.
;;
;; The first bytes of its memory hold what the tests read and set:
;;     0   cabi_realloc's mode
;;     4   the number of times cabi_realloc allocates before its mode applies
;;     8   the address cabi_realloc allocates from next, which starts at 8192
;;     12  the number of times cabi_realloc has been called
;;     16  the alignment and size cabi_realloc was asked for in each of its first 16 calls, 8 bytes
;;         a call
;;     144 the number of times a cabi_post_* function has been called
;;     256 the return area of `echo_hit`
;;     320 the return area of `echo_outer`
;;     384 the return area of `echo_pair`
(module
  (import "test:records/host@1" "take_hit:7abc176cf089458f"
    (func $take_hit (param i32 i32 i32 f32 f32)))
  (import "test:records/host@1" "take_hits:d70d8319bc5de9a9" (func $take_hits (param i32)))
  (import "test:records/host@1" "give_hit:72e479e0a668ef06" (func $give_hit (param i32)))
  (import "test:records/host@1" "give_outer:0d6b48927a4088cf" (func $give_outer (param i32)))
  (import "test:records/host@1" "give_single:3f8f04caefb336a1" (func $give_single (result f64)))
  (memory (export "memory") 1)
  (data (i32.const 8) "\00\20\00\00")

  ;; Records its call, and then, unless it's to allocate a few more times first, by the mode at 0:
  ;;     0  allocates `size` bytes aligned to `align` from the address at 8, growing memory if they
  ;;        don't fit, and returns where they are, or null if memory can't grow
  ;;     1  returns null
  ;;     2  returns 8193, which is misaligned for any alignment but 1
  ;;     3  returns an address that's as aligned as asked, but too near the end of memory, by the
;;        alignment, for the bytes to fit
;;     4  grows memory by 16 pages, and returns the first of them
  (func $cabi_realloc (export "cabi_realloc")
    (param $old i32) (param $old_size i32) (param $align i32) (param $size i32) (result i32)
    (local $calls i32)
    (local $mode i32)
    (local $at i32)
    (local $end i32)
    (local $memory i32)
    (local.set $calls (i32.load (i32.const 12)))
    (if (i32.lt_u (local.get $calls) (i32.const 16))
      (then
        (i32.store offset=16 (i32.mul (local.get $calls) (i32.const 8)) (local.get $align))
        (i32.store offset=20 (i32.mul (local.get $calls) (i32.const 8)) (local.get $size))))
    (i32.store (i32.const 12) (i32.add (local.get $calls) (i32.const 1)))
    (local.set $mode (i32.load (i32.const 0)))
    (if (i32.gt_u (i32.load (i32.const 4)) (i32.const 0))
      (then
        (i32.store (i32.const 4) (i32.sub (i32.load (i32.const 4)) (i32.const 1)))
        (local.set $mode (i32.const 0))))
    (if (i32.eq (local.get $mode) (i32.const 1))
      (then (return (i32.const 0))))
    (if (i32.eq (local.get $mode) (i32.const 2))
      (then (return (i32.const 8193))))
    (if (i32.eq (local.get $mode) (i32.const 3))
      (then
        (return
          (i32.add
            (i32.sub (i32.mul (memory.size) (i32.const 65536)) (local.get $size))
            (local.get $align)))))
    (if (i32.eq (local.get $mode) (i32.const 4))
      (then (return (i32.mul (memory.grow (i32.const 16)) (i32.const 65536)))))
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

  (func $pass_hit_at (export "test:records/user@1#pass_hit_at:a16593f96dc516b3")
    (param i32 i32 i32 f32 f32)
    (call $take_hit (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4)))

  ;; Records a call of a cabi_post_* function.
  (func $post
    (i32.store (i32.const 144) (i32.add (i32.load (i32.const 144)) (i32.const 1))))

  ;; Overwrites the string at `at` in memory, its address and size, with asterisks, so that a host
  ;; that hadn't copied it would read them.
  (func $spoil (param $at i32)
    (memory.fill
      (i32.load (local.get $at))
      (i32.const 0x2a)
      (i32.load offset=4 (local.get $at))))

  (func $pass_hit (export "test:records/user@1#pass_hit:7abc176cf089458f")
    (param i32 i32 i32 f32 f32)
    (call $take_hit (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4)))

  ;; Lays out a hit at `at`, as the canonical ABI does: the weapon's address and size, the damage,
  ;; and the `vec2`, 4 bytes each.
  (func $store_hit (param $at i32) (param i32 i32 i32 f32 f32)
    (i32.store (local.get $at) (local.get 1))
    (i32.store offset=4 (local.get $at) (local.get 2))
    (i32.store offset=8 (local.get $at) (local.get 3))
    (f32.store offset=12 (local.get $at) (local.get 4))
    (f32.store offset=16 (local.get $at) (local.get 5)))

  (func $echo_hit (export "test:records/user@1#echo_hit:268e2eed9a9941d6")
    (param i32 i32 i32 f32 f32) (result i32)
    (call $store_hit (i32.const 256)
      (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4))
    (i32.const 256))
  (func (export "cabi_post_test:records/user@1#echo_hit:268e2eed9a9941d6") (param i32)
    (call $spoil (local.get 0))
    (call $post))

  ;; Lays out an `outer`: `tag` at 0, `inner` at 8, with `a` at 8, `b` at 16, `c` at 24, `d` at
  ;; 26, `e` at 28 and `f` at 32, and `last` at 40.
  (func $echo_outer (export "test:records/user@1#echo_outer:ca2012d2782e70cd")
    (param i32 i32 i64 i32 i32 f32 i32 i32) (result i32)
    (i32.store8 (i32.const 320) (local.get 0))
    (i32.store8 (i32.const 328) (local.get 1))
    (i64.store (i32.const 336) (local.get 2))
    (i32.store16 (i32.const 344) (local.get 3))
    (i32.store8 (i32.const 346) (local.get 4))
    (f32.store (i32.const 348) (local.get 5))
    (i32.store8 (i32.const 352) (local.get 6))
    (i32.store16 (i32.const 360) (local.get 7))
    (i32.const 320))
  (func (export "cabi_post_test:records/user@1#echo_outer:ca2012d2782e70cd") (param i32)
    (call $post))

  ;; Lays out a pair: its left hit at 0, and its right hit at 20.
  (func $echo_pair (export "test:records/user@1#echo_pair:255c9801d43234dc")
    (param i32 i32 i32 f32 f32 i32 i32 i32 f32 f32) (result i32)
    (call $store_hit (i32.const 384)
      (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4))
    (call $store_hit (i32.const 404)
      (local.get 5) (local.get 6) (local.get 7) (local.get 8) (local.get 9))
    (i32.const 384))
  (func (export "cabi_post_test:records/user@1#echo_pair:255c9801d43234dc") (param i32)
    (call $spoil (local.get 0))
    (call $spoil (i32.add (local.get 0) (i32.const 20)))
    (call $post))

  (func $echo_single (export "test:records/user@1#echo_single:74a938f7e001ae12")
    (param f64) (result f64)
    (local.get 0))
  (func (export "cabi_post_test:records/user@1#echo_single:74a938f7e001ae12") (param f64)
    (call $post))
  (func $fetch_single (export "test:records/user@1#fetch_single:3f8f04caefb336a1") (result f64)
    (call $give_single))
  (func $pass_hits (export "test:records/user@1#pass_hits:d70d8319bc5de9a9") (param i32)
    (call $take_hits (local.get 0)))
  (func $pass_hits_at (export "test:records/user@1#pass_hits_at:e492f253b87ebe82") (param i32)
    (call $take_hits (local.get 0)))
  (func $sixteen (export "test:records/user@1#sixteen:4348a794850b66d9")
    (param i32 i32 i32 f32 f32 i32 i32 i32 f32 f32 i32 i32 i32 f32 f32 i32) (result i32)
    (local.get 15))
  (func $seventeen (export "test:records/user@1#seventeen:bca79f5d2ec4e0fe")
    (param i32) (result i32)
    (local.get 0))

  (func $return_outer_at (export "test:records/user@1#return_outer_at:ccc6a20a7f4bcfaf")
    (param i32) (result i32)
    (local.get 0))
  (func $return_pair_at (export "test:records/user@1#return_pair_at:a69852e5844113fe")
    (param i32) (result i32)
    (local.get 0))

  (func $posts (export "test:records/user@1#posts:5b69886a8b9e31a8") (result i32)
    (i32.load (i32.const 144)))

  (func $fetch_hit_into (export "test:records/user@1#fetch_hit_into:e492f253b87ebe82") (param i32)
    (call $give_hit (local.get 0)))
  (func $fetch_outer_into (export "test:records/user@1#fetch_outer_into:e492f253b87ebe82")
    (param i32)
    (call $give_outer (local.get 0)))

  (func $store (export "test:records/user@1#store:bcbfa7c332b66f12") (param i32 i32)
    (i32.store (local.get 0) (local.get 1)))
  (func $store_byte (export "test:records/user@1#store_byte:64136b00cfcc0e7f") (param i32 i32)
    (i32.store8 (local.get 0) (local.get 1)))
  (func $load (export "test:records/user@1#load:4d8a9a4559ed9204") (param i32) (result i32)
    (i32.load (local.get 0)))
  (func $load_byte (export "test:records/user@1#load_byte:65d04745e3f2b9ee") (param i32) (result i32)
    (i32.load8_u (local.get 0)))

  (func $set_realloc (export "test:records/user@1#set_realloc:bcbfa7c332b66f12") (param i32 i32)
    (i32.store (i32.const 0) (local.get 0))
    (i32.store (i32.const 4) (local.get 1)))
  (func $reallocs (export "test:records/user@1#reallocs:5b69886a8b9e31a8") (result i32)
    (i32.load (i32.const 12)))
  (func $realloc_alignment (export "test:records/user@1#realloc_alignment:4d8a9a4559ed9204")
    (param i32) (result i32)
    (i32.load offset=16 (i32.mul (local.get 0) (i32.const 8))))
  (func $realloc_size (export "test:records/user@1#realloc_size:4d8a9a4559ed9204")
    (param i32) (result i32)
    (i32.load offset=20 (i32.mul (local.get 0) (i32.const 8)))))
