;; The script that the tests of trap reports call. Each entry point but `returns` traps in its own
;; way, as traps.stasis says, and every function has a name in the name section, which the frames
;; of a report carry.
(module
  (type $void (func))
  (memory (export "memory") 1)

  ;; Functions 0 and 1. The frames of `nested` show where each was: $traps at the instruction that
  ;; trapped, and $nested at its call. Each comes after the four bytes of an `i32.const 1000` and a
  ;; `drop`, its function's first two instructions.
  (func $traps
    (drop (i32.const 1000))
    (unreachable))
  (func $nested (export "test:traps/trapper@1#nested:2e38e77b22c314a4")
    (drop (i32.const 1000))
    (call $traps))

  ;; Element 0 holds a function of another type than calls through the table expect, and element 1
  ;; holds none.
  (table 2 funcref)
  (elem (i32.const 0) $takes_an_i32)
  (func $takes_an_i32 (param i32))

  (func $returns (export "test:traps/trapper@1#returns:2e38e77b22c314a4"))

  (func $unreachable (export "test:traps/trapper@1#unreachable:2e38e77b22c314a4")
    (unreachable))
  (func $out_of_bounds (export "test:traps/trapper@1#out_of_bounds:2e38e77b22c314a4")
    (drop (i32.load (i32.const 65536))))
  (func $undefined_element (export "test:traps/trapper@1#undefined_element:2e38e77b22c314a4")
    (call_indirect (type $void) (i32.const 2)))
  (func $uninitialized_element
    (export "test:traps/trapper@1#uninitialized_element:2e38e77b22c314a4")
    (call_indirect (type $void) (i32.const 1)))
  (func $type_mismatch (export "test:traps/trapper@1#type_mismatch:2e38e77b22c314a4")
    (call_indirect (type $void) (i32.const 0)))
  (func $divide_by_zero (export "test:traps/trapper@1#divide_by_zero:2e38e77b22c314a4")
    (drop (i32.div_u (i32.const 1) (i32.const 0))))
  (func $division_overflow (export "test:traps/trapper@1#division_overflow:2e38e77b22c314a4")
    (drop (i32.div_s (i32.const 0x80000000) (i32.const -1))))
  (func $conversion_overflow (export "test:traps/trapper@1#conversion_overflow:2e38e77b22c314a4")
    (drop (i32.trunc_f32_s (f32.const 3e9))))
  (func $invalid_conversion (export "test:traps/trapper@1#invalid_conversion:2e38e77b22c314a4")
    (drop (i32.trunc_f32_s (f32.const nan))))
  (func $table_out_of_bounds (export "test:traps/trapper@1#table_out_of_bounds:2e38e77b22c314a4")
    (drop (table.get 0 (i32.const 2))))
  (func $recurse (export "test:traps/trapper@1#recurse:2e38e77b22c314a4")
    (call $recurse)))
