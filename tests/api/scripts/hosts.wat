;; The script that the tests of host functions call. Its entry points call hosts.stasis's host
;; functions, as the interface says.
(module
  (import "test:hosts/host@1" "pass_u8:4618eff65f831824" (func $pass_u8 (param i32) (result i32)))
  (import "test:hosts/host@1" "pass_u16:0daec35f42ff3c23" (func $pass_u16 (param i32) (result i32)))
  (import "test:hosts/host@1" "pass_u32:4d8a9a4559ed9204" (func $pass_u32 (param i32) (result i32)))
  (import "test:hosts/host@1" "pass_u64:6075243e99052a22" (func $pass_u64 (param i64) (result i64)))
  (import "test:hosts/host@1" "pass_s8:1a6e70e5cfc850c4" (func $pass_s8 (param i32) (result i32)))
  (import "test:hosts/host@1" "pass_s16:f9b03928024b69a4" (func $pass_s16 (param i32) (result i32)))
  (import "test:hosts/host@1" "pass_s32:7fc9a34e05f273e8" (func $pass_s32 (param i32) (result i32)))
  (import "test:hosts/host@1" "pass_s64:0c19df4286c55dc0" (func $pass_s64 (param i64) (result i64)))
  (import "test:hosts/host@1" "pass_f32:9554a077d12e0b66" (func $pass_f32 (param f32) (result f32)))
  (import "test:hosts/host@1" "pass_f64:3a6940b6ebee1f13" (func $pass_f64 (param f64) (result f64)))
  (import "test:hosts/host@1" "pass_bool:1c8fc351710be1e8"
    (func $pass_bool (param i32) (result i32)))
  (import "test:hosts/host@1" "ping:2e38e77b22c314a4" (func $ping))
  (import "test:hosts/host@1" "sixteen:e0339a28498649d2"
    (func $sixteen
      (param i32 i32 i32 i64 f32 f64 i32 i64 i32 i32 i32 i64 f32 f64 i32 i32) (result i64)))
  (import "test:hosts/host@1" "seventeen:5f56e9a6de0ea0c7"
    (func $seventeen (param i32) (result i64)))
  (import "test:hosts/host@1" "thing.poke:bfc6d0f269b3c1d6" (func $poke (param i32)))
  (import "test:hosts/other@1" "ping:5b69886a8b9e31a8" (func $other_ping (result i32)))
  (memory (export "memory") 1)

  (func (export "test:hosts/caller@1#relay_u8:4618eff65f831824") (param i32) (result i32)
    (call $pass_u8 (local.get 0)))
  (func (export "test:hosts/caller@1#relay_u16:0daec35f42ff3c23") (param i32) (result i32)
    (call $pass_u16 (local.get 0)))
  (func (export "test:hosts/caller@1#relay_u32:4d8a9a4559ed9204") (param i32) (result i32)
    (call $pass_u32 (local.get 0)))
  (func (export "test:hosts/caller@1#relay_u64:6075243e99052a22") (param i64) (result i64)
    (call $pass_u64 (local.get 0)))
  (func (export "test:hosts/caller@1#relay_s8:1a6e70e5cfc850c4") (param i32) (result i32)
    (call $pass_s8 (local.get 0)))
  (func (export "test:hosts/caller@1#relay_s16:f9b03928024b69a4") (param i32) (result i32)
    (call $pass_s16 (local.get 0)))
  (func (export "test:hosts/caller@1#relay_s32:7fc9a34e05f273e8") (param i32) (result i32)
    (call $pass_s32 (local.get 0)))
  (func (export "test:hosts/caller@1#relay_s64:0c19df4286c55dc0") (param i64) (result i64)
    (call $pass_s64 (local.get 0)))
  (func (export "test:hosts/caller@1#relay_f32:9554a077d12e0b66") (param f32) (result f32)
    (call $pass_f32 (local.get 0)))
  (func (export "test:hosts/caller@1#relay_f64:3a6940b6ebee1f13") (param f64) (result f64)
    (call $pass_f64 (local.get 0)))
  (func (export "test:hosts/caller@1#relay_bool:1c8fc351710be1e8") (param i32) (result i32)
    (call $pass_bool (local.get 0)))

  ;; The same calls, with the bits as the script has them on both sides.
  (func (export "test:hosts/caller@1#relay_bits_u8:4d8a9a4559ed9204") (param i32) (result i32)
    (call $pass_u8 (local.get 0)))
  (func (export "test:hosts/caller@1#relay_bits_u16:4d8a9a4559ed9204") (param i32) (result i32)
    (call $pass_u16 (local.get 0)))
  (func (export "test:hosts/caller@1#relay_bits_s8:4d8a9a4559ed9204") (param i32) (result i32)
    (call $pass_s8 (local.get 0)))
  (func (export "test:hosts/caller@1#relay_bits_s16:4d8a9a4559ed9204") (param i32) (result i32)
    (call $pass_s16 (local.get 0)))
  (func (export "test:hosts/caller@1#relay_bits_bool:4d8a9a4559ed9204") (param i32) (result i32)
    (call $pass_bool (local.get 0)))

  ;; The arguments the tests expect: 200, -2, 4000000000, 0x123456789abcdef0, 1.5, -2.25, true, -5,
  ;; 65535, -128, -2147483648, 2^64 - 1, a NaN with the payload 0x412345, -0, false and 7, and for
  ;; `seventeen`, -2^63.
  (func (export "test:hosts/caller@1#call_sixteen:b13376f9693dd6bd") (result i64)
    (call $sixteen
      (i32.const 200) (i32.const -2) (i32.const 4000000000) (i64.const 0x123456789abcdef0)
      (f32.const 1.5) (f64.const -2.25) (i32.const 1) (i64.const -5)
      (i32.const 65535) (i32.const -128) (i32.const 0x80000000) (i64.const -1)
      (f32.const nan:0x412345) (f64.const -0) (i32.const 0) (i32.const 7)))

  ;; The same arguments, and the seventeenth, laid out at 1024 as the canonical ABI lays out a
  ;; tuple of them: each at the next offset its alignment allows. The bool `true` is 0x80, which is
  ;; true as any byte but 0 is.
  (func (export "test:hosts/caller@1#call_seventeen:b13376f9693dd6bd") (result i64)
    (i32.store8 (i32.const 1024) (i32.const 200))
    (i32.store16 (i32.const 1026) (i32.const -2))
    (i32.store (i32.const 1028) (i32.const 4000000000))
    (i64.store (i32.const 1032) (i64.const 0x123456789abcdef0))
    (f32.store (i32.const 1040) (f32.const 1.5))
    (f64.store (i32.const 1048) (f64.const -2.25))
    (i32.store8 (i32.const 1056) (i32.const 0x80))
    (i64.store (i32.const 1064) (i64.const -5))
    (i32.store16 (i32.const 1072) (i32.const 65535))
    (i32.store8 (i32.const 1074) (i32.const -128))
    (i32.store (i32.const 1076) (i32.const 0x80000000))
    (i64.store (i32.const 1080) (i64.const -1))
    (f32.store (i32.const 1088) (f32.const nan:0x412345))
    (f64.store (i32.const 1096) (f64.const -0))
    (i32.store8 (i32.const 1104) (i32.const 0))
    (i32.store8 (i32.const 1105) (i32.const 7))
    (i64.store (i32.const 1112) (i64.const 0x8000000000000000))
    (call $seventeen (i32.const 1024)))
  (func (export "test:hosts/caller@1#call_seventeen_at:5db8d049340bbec7") (param i32) (result i64)
    (call $seventeen (local.get 0)))

  (func (export "test:hosts/caller@1#ping:2e38e77b22c314a4")
    (call $ping)
    (call $ping))
  (func (export "test:hosts/caller@1#ping_other:5b69886a8b9e31a8") (result i32)
    (call $other_ping))
  (func (export "test:hosts/caller@1#poke:2e38e77b22c314a4")
    (call $poke (i32.const 0))))
