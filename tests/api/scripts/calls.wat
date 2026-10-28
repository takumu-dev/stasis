;; The script that most tests call. It provides every entry point of calls.stasis, each doing what
;; the interface says, and stasis_initialize counts how often it runs. It exports its memory and its
;; mutable globals, so it's snapshottable.
(module
  (memory (export "memory") 1)
  (global $starts (export "starts") (mut i32) (i32.const 0))
  (global $frame (export "frame") (mut i64) (i64.const 0))

  (func (export "stasis_initialize")
    (global.set $starts (i32.add (global.get $starts) (i32.const 1))))
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (global.get $starts))

  (func (export "test:calls/caller@1#echo_u8:4618eff65f831824") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#echo_u16:0daec35f42ff3c23") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#echo_u32:4d8a9a4559ed9204") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#echo_u64:6075243e99052a22") (param i64) (result i64)
    (local.get 0))
  (func (export "test:calls/caller@1#echo_s8:1a6e70e5cfc850c4") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#echo_s16:f9b03928024b69a4") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#echo_s32:7fc9a34e05f273e8") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#echo_s64:0c19df4286c55dc0") (param i64) (result i64)
    (local.get 0))
  (func (export "test:calls/caller@1#echo_f32:9554a077d12e0b66") (param f32) (result f32)
    (local.get 0))
  (func (export "test:calls/caller@1#echo_f64:3a6940b6ebee1f13") (param f64) (result f64)
    (local.get 0))
  (func (export "test:calls/caller@1#echo_bool:1c8fc351710be1e8") (param i32) (result i32)
    (local.get 0))

  (func (export "test:calls/caller@1#extend_u8:7780f4b139e1ba66") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#extend_u16:89a6f34493499acc") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#extend_s8:a895cd9836ac8a7b") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#extend_s16:ffca41fa5f2bd10e") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#extend_bool:f6f8b80f669c0be2") (param i32) (result i32)
    (local.get 0))

  (func (export "test:calls/caller@1#wrap_u8:65d04745e3f2b9ee") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#wrap_u16:f107eabe88d9b8fa") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#wrap_s8:026da3344ec2850a") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#wrap_s16:363aacea49172156") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#wrap_bool:a235a756b92d6e65") (param i32) (result i32)
    (local.get 0))

  ;; a + 3b + 5c + 7d + 11 bits(e) + 13 bits(f) + 17g + 19h, modulo 2^64, with b sign-extended as
  ;; it arrives.
  (func (export "test:calls/caller@1#mix:83b76a24161e68fc")
    (param $a i32) (param $b i32) (param $c i32) (param $d i64)
    (param $e f32) (param $f f64) (param $g i32) (param $h i64)
    (result i64)
    (i64.add
      (i64.add
        (i64.add (i64.extend_i32_u (local.get $a))
                 (i64.mul (i64.extend_i32_s (local.get $b)) (i64.const 3)))
        (i64.add (i64.mul (i64.extend_i32_u (local.get $c)) (i64.const 5))
                 (i64.mul (local.get $d) (i64.const 7))))
      (i64.add
        (i64.add (i64.mul (i64.extend_i32_u (i32.reinterpret_f32 (local.get $e))) (i64.const 11))
                 (i64.mul (i64.reinterpret_f64 (local.get $f)) (i64.const 13)))
        (i64.add (i64.mul (i64.extend_i32_u (local.get $g)) (i64.const 17))
                 (i64.mul (local.get $h) (i64.const 19))))))

  (func (export "test:calls/caller@1#tick:37ab64a28242fa4b") (param i64)
    (global.set $frame (local.get 0)))
  (func (export "test:calls/caller@1#last_frame:b13376f9693dd6bd") (result i64)
    (global.get $frame))

  (func (export "test:calls/caller@1#fail:2e38e77b22c314a4")
    (unreachable))

  ;; Its records, string and handle flatten to seven parameters, and its record result comes back
  ;; through a return area.
  (func (export "test:calls/caller@1#place:582f491785a0720b")
    (param f32 f32 f32 f32 i32 i32 i32) (result i32)
    (unreachable)))
