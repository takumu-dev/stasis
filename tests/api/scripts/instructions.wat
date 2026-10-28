;; A script whose code has every form of immediate an instruction may take, and instructions from
;; each end of every range of opcodes that take none, as WAMR accepts them, but none of the
;; instructions that change a table or drop a segment. Many immediates are bytes that are those
;; instructions' opcodes elsewhere: 0x26 is `table.set`, and 0xfc the prefix of the rest. It's
;; snapshottable, unless reading its code goes wrong.
(module
  (type $pair (func (param i32) (result i32 i32)))
  (type $unary (func (param i32) (result i32)))
  (memory (export "memory") 1)
  (table 2 funcref)
  (table $second 1 funcref)
  (global $counter (export "counter") (mut i32) (i32.const 38))
  (global $limit i64 (i64.const 0xfc26))
  (data $bytes "bytes")
  (elem declare func $identity)

  (func $identity (type $unary)
    (local.get 0))

  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0))

  (func $everything (param $x i32) (result i32)
    (local $y i32) (local $z i64) (local $w f32) (local f64 f64)
    nop
    (block $out
      (loop $again
        (br_if $out (local.get $x))
        (br_table $out $again $out $again $out (local.get $x))))
    (if (local.get $x)
      (then unreachable)
      (else nop))
    (if (result i32) (local.get $x)
      (then (i32.const 1))
      (else (br 0 (i32.const 2))))
    drop
    local.get $x
    block (type $pair) (param i32) (result i32 i32)
      i32.const 38
    end
    drop
    drop
    (drop (block (result i64) (i64.const 0x7fffffffffffffff)))
    (drop (call $identity (i32.const 38)))
    (drop (call_indirect (type $unary) (i32.const 252) (i32.const 0)))
    (drop (call_indirect $second (type $unary) (i32.const -1) (i32.const 0)))
    (drop (select (i32.const 1) (i32.const 2) (local.get $x)))
    (drop (select (result f32) (f32.const nan:0x262626) (f32.const 0) (local.get $x)))
    (local.set $y (local.get $x))
    (drop (local.tee $y (i32.const -38)))
    (local.set $z (global.get $limit))
    (local.set $w (f32.const -0x1.fcp+38))
    (local.set 4 (f64.const nan:0xfc26fc26fc26))
    (global.set $counter (global.get $counter))
    (drop (table.get (i32.const 0)))
    (drop (table.get $second (i32.const 0)))
    (drop (table.size))
    (drop (table.size $second))
    (drop (ref.is_null (ref.null extern)))
    (drop (ref.func $identity))
    (drop (ref.null func))
    (drop (i32.load offset=38 align=1 (i32.const 0)))
    (drop (i64.load32_u offset=252 (i32.const 0)))
    (i32.store8 offset=0xfc26 (i32.const 0) (i32.const 38))
    (i64.store32 align=4 (i32.const 0) (i64.const 252))
    (drop (memory.size))
    (drop (memory.grow (i32.const 0)))
    (memory.init $bytes (i32.const 0) (i32.const 0) (i32.const 1))
    (memory.copy (i32.const 0) (i32.const 1) (i32.const 1))
    (memory.fill (i32.const 0) (i32.const 38) (i32.const 1))
    (drop (i32.eqz (i32.const 0)))
    (drop (i64.extend32_s (local.get $z)))
    (drop (i32.trunc_sat_f32_s (local.get $w)))
    (drop (i64.trunc_sat_f64_u (local.get 4)))
    (return (local.get $x))))
