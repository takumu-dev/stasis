;; Passes records to the host, spilled or not, and takes a record without strings from the host and
;; from an entry point's arguments, and returns one, none of which needs cabi_realloc, which it
;; doesn't export.
;;
;; Its memory holds four hits at 1024, laid out as `host.take_hits` takes them, each with the weapon
;; "plain", at 2048, and a damage of 7.
(module
  (import "test:records/host@1" "take_hits:d70d8319bc5de9a9" (func $take_hits (param i32)))
  (import "test:records/host@1" "give_outer:0d6b48927a4088cf" (func $give_outer (param i32)))
  (memory (export "memory") 1)
  (data (i32.const 1024) "\00\08\00\00\05\00\00\00\07\00\00\00")
  (data (i32.const 1044) "\00\08\00\00\05\00\00\00\07\00\00\00")
  (data (i32.const 1064) "\00\08\00\00\05\00\00\00\07\00\00\00")
  (data (i32.const 1084) "\00\08\00\00\05\00\00\00\07\00\00\00")
  (data (i32.const 2048) "plain")
  (func (export "test:records/user@1#pass_hits_at:e492f253b87ebe82") (param i32)
    (call $take_hits (local.get 0)))
  (func (export "test:records/user@1#fetch_outer_into:e492f253b87ebe82") (param i32)
    (call $give_outer (local.get 0)))
  (func (export "test:records/user@1#echo_outer:ca2012d2782e70cd")
    (param i32 i32 i64 i32 i32 f32 i32 i32) (result i32)
    (i32.store8 (i32.const 320) (local.get 0))
    (i32.const 320))
  (func (export "test:records/user@1#load_byte:65d04745e3f2b9ee") (param i32) (result i32)
    (i32.load8_u (local.get 0))))
