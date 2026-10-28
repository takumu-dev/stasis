;; The script that the tests of strings call. Its entry points do as strings.stasis says, and its
;; cabi_realloc and cabi_post_* functions do as the modes the tests set say.
;;
;; The first bytes of its memory hold what the tests read and set:
;;     0   cabi_realloc's mode
;;     4   the cabi_post_* functions' mode
;;     8   the address cabi_realloc allocates from next, which starts at 8192
;;     12  the number of times cabi_realloc has been called
;;     16  cabi_realloc's four arguments in its last call
;;     32  the number of times a cabi_post_* function has been called
;;     36  the argument of the last call of one that takes one
;;     64  the return area of the entry points that return a string
;;     72  the return area the script passes `host.give` and `host.swap`
;;     1024  the strings `pass_nine` passes, "1" to "999999999"
;;     2048  the arguments `pass_nine` spills
(module
  (import "test:strings/host@1" "take:76a67b5b4fa368b5" (func $take (param i32 i32)))
  (import "test:strings/host@1" "take_mixed:387089b764ab8883"
    (func $take_mixed (param i32 i32 i32 i64 i32 i32 f32)))
  (import "test:strings/host@1" "take_nine:4c03c2c2b2194da1" (func $take_nine (param i32)))
  (import "test:strings/host@1" "give:ae6637fc59233091" (func $give (param i32)))
  (import "test:strings/host@1" "swap:150b24eac1c4a9c1" (func $swap (param i32 i32 i32)))
  (memory (export "memory") 1)
  (data (i32.const 8) "\00\20\00\00")
  (data (i32.const 1024) "122333444455555666666777777788888888999999999")

  ;; Allocates `size` bytes aligned to `align` from the address at 8, growing memory if they don't
  ;; fit, and returns where they are, or null if memory can't grow.
  (func $allocate (param $align i32) (param $size i32) (result i32)
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

  ;; Records its call, and then by the mode at 0:
  ;;     0  allocates
  ;;     1  returns null
  ;;     2  returns an address one byte too near the end of memory for the bytes to fit
  ;;     3  traps
  ;;     4  calls `host.take`, which it may not, and then allocates
  ;;     5  grows memory by 16 pages, and returns the first of them
  (func $cabi_realloc (export "cabi_realloc")
    (param $old i32) (param $old_size i32) (param $align i32) (param $size i32) (result i32)
    (local $mode i32)
    (i32.store (i32.const 12) (i32.add (i32.load (i32.const 12)) (i32.const 1)))
    (i32.store (i32.const 16) (local.get $old))
    (i32.store (i32.const 20) (local.get $old_size))
    (i32.store (i32.const 24) (local.get $align))
    (i32.store (i32.const 28) (local.get $size))
    (local.set $mode (i32.load (i32.const 0)))
    (if (i32.eq (local.get $mode) (i32.const 1))
      (then (return (i32.const 0))))
    (if (i32.eq (local.get $mode) (i32.const 2))
      (then
        (return
          (i32.add
            (i32.sub (i32.mul (memory.size) (i32.const 65536)) (local.get $size))
            (i32.const 1)))))
    (if (i32.eq (local.get $mode) (i32.const 3))
      (then (unreachable)))
    (if (i32.eq (local.get $mode) (i32.const 4))
      (then (call $take (i32.const 0) (i32.const 0))))
    (if (i32.eq (local.get $mode) (i32.const 5))
      (then (return (i32.mul (memory.grow (i32.const 16)) (i32.const 65536)))))
    (call $allocate (local.get $align) (local.get $size)))

  ;; Records a call of a cabi_post_* function, and then by the mode at 4:
  ;;     0  does nothing more
  ;;     1  calls `host.take`, which it may not
  ;;     2  traps
  ;;     3  grows memory by 16 pages
  (func $post
    (local $mode i32)
    (i32.store (i32.const 32) (i32.add (i32.load (i32.const 32)) (i32.const 1)))
    (local.set $mode (i32.load (i32.const 4)))
    (if (i32.eq (local.get $mode) (i32.const 1))
      (then (call $take (i32.const 0) (i32.const 0))))
    (if (i32.eq (local.get $mode) (i32.const 2))
      (then (unreachable)))
    (if (i32.eq (local.get $mode) (i32.const 3))
      (then (drop (memory.grow (i32.const 16))))))

  ;; Records the argument of a cabi_post_* function, and its call.
  (func $post_argument (param $argument i32)
    (i32.store (i32.const 36) (local.get $argument))
    (call $post))

  ;; Frees a string result in the return area at `area`, which it overwrites with asterisks first,
  ;; so that a host that hadn't copied it would read them.
  (func $post_string (param $area i32)
    (memory.fill
      (i32.load (local.get $area))
      (i32.const 0x2a)
      (i32.load offset=4 (local.get $area)))
    (call $post_argument (local.get $area)))

  (func $pass (export "test:strings/user@1#pass:76a67b5b4fa368b5") (param i32 i32)
    (call $take (local.get 0) (local.get 1)))
  (func (export "cabi_post_test:strings/user@1#pass:76a67b5b4fa368b5")
    (call $post))

  (func $pass_mixed (export "test:strings/user@1#pass_mixed:387089b764ab8883")
    (param i32 i32 i32 i64 i32 i32 f32)
    (call $take_mixed
      (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4) (local.get 5)
      (local.get 6)))

  (func $fetch (export "test:strings/user@1#fetch:ae6637fc59233091") (result i32)
    (call $give (i32.const 72))
    (i32.const 72))
  (func (export "cabi_post_test:strings/user@1#fetch:ae6637fc59233091") (param i32)
    (call $post_string (local.get 0)))

  (func $relay (export "test:strings/user@1#relay:150b24eac1c4a9c1") (param i32 i32) (result i32)
    (call $swap (local.get 0) (local.get 1) (i32.const 72))
    (i32.const 72))
  (func (export "cabi_post_test:strings/user@1#relay:150b24eac1c4a9c1") (param i32)
    (call $post_string (local.get 0)))

  ;; The strings at 1024, each after the one before, and 7, laid out at 2048 as the canonical ABI
  ;; lays out a tuple of them: each string as its address and length, 4 bytes each.
  (func $pass_nine (export "test:strings/user@1#pass_nine:2e38e77b22c314a4")
    (local $string i32)
    (local $address i32)
    (local.set $address (i32.const 1024))
    (loop $strings
      (i32.store (i32.add (i32.const 2048) (i32.mul (local.get $string) (i32.const 8)))
        (local.get $address))
      (i32.store offset=4 (i32.add (i32.const 2048) (i32.mul (local.get $string) (i32.const 8)))
        (i32.add (local.get $string) (i32.const 1)))
      (local.set $string (i32.add (local.get $string) (i32.const 1)))
      (local.set $address (i32.add (local.get $address) (local.get $string)))
      (br_if $strings (i32.lt_u (local.get $string) (i32.const 9))))
    (i32.store8 (i32.const 2120) (i32.const 7))
    (call $take_nine (i32.const 2048)))

  (func $echo (export "test:strings/user@1#echo:150b24eac1c4a9c1") (param i32 i32) (result i32)
    (i32.store (i32.const 64) (local.get 0))
    (i32.store (i32.const 68) (local.get 1))
    (i32.const 64))
  (func (export "cabi_post_test:strings/user@1#echo:150b24eac1c4a9c1") (param i32)
    (call $post_string (local.get 0)))

  (func $measure (export "test:strings/user@1#measure:11d92d220d222e92") (param i32 i32) (result i32)
    (local.get 1))
  (func (export "cabi_post_test:strings/user@1#measure:11d92d220d222e92") (param i32)
    (call $post_argument (local.get 0)))

  (func $pass_at (export "test:strings/user@1#pass_at:bcbfa7c332b66f12") (param i32 i32)
    (call $take (local.get 0) (local.get 1)))
  (func $pass_mixed_at (export "test:strings/user@1#pass_mixed_at:e6ffd2ba6eb8b67d")
    (param i32 i32 i32 i32)
    (call $take_mixed
      (i32.const 9) (local.get 0) (local.get 1) (i64.const 10) (local.get 2) (local.get 3)
      (f32.const 11)))
  (func $pass_nine_at (export "test:strings/user@1#pass_nine_at:e492f253b87ebe82") (param i32)
    (call $take_nine (local.get 0)))
  (func $fetch_into (export "test:strings/user@1#fetch_into:e492f253b87ebe82") (param i32)
    (call $give (local.get 0)))
  (func $return_at (export "test:strings/user@1#return_at:b214348321a9caba") (param i32) (result i32)
    (local.get 0))

  (func $store (export "test:strings/user@1#store:bcbfa7c332b66f12") (param i32 i32)
    (i32.store (local.get 0) (local.get 1)))
  (func $store_byte (export "test:strings/user@1#store_byte:64136b00cfcc0e7f") (param i32 i32)
    (i32.store8 (local.get 0) (local.get 1)))
  (func $pages (export "test:strings/user@1#pages:5b69886a8b9e31a8") (result i32)
    (memory.size))

  (func $set_realloc (export "test:strings/user@1#set_realloc:e492f253b87ebe82") (param i32)
    (i32.store (i32.const 0) (local.get 0)))
  (func $set_post (export "test:strings/user@1#set_post:e492f253b87ebe82") (param i32)
    (i32.store (i32.const 4) (local.get 0)))
  (func $reallocs (export "test:strings/user@1#reallocs:5b69886a8b9e31a8") (result i32)
    (i32.load (i32.const 12)))
  (func $realloc_argument (export "test:strings/user@1#realloc_argument:4d8a9a4559ed9204")
    (param i32) (result i32)
    (i32.load offset=16 (i32.mul (local.get 0) (i32.const 4))))
  (func $posts (export "test:strings/user@1#posts:5b69886a8b9e31a8") (result i32)
    (i32.load (i32.const 32)))
  (func $post_argument_of (export "test:strings/user@1#post_argument:5b69886a8b9e31a8")
    (result i32)
    (i32.load (i32.const 36))))
