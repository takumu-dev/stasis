;; A script with problems of both kinds that loading lists: it lacks `starts`, its `echo_u8` has
;; another signature, and one of its exports doesn't parse.
(module
  (memory (export "memory") 1)
  (func (export "test:calls/caller@1#echo_u8:0000000000000000") (param i32) (result i32)
    (local.get 0))
  (func (export "test:calls/caller@1#Tick:37ab64a28242fa4b") (param i64)))
