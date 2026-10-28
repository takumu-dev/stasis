;; Exports `user.seventeen` from records.stasis, whose arguments are all scalars but spill into the
;; script's memory, but doesn't export the cabi_realloc through which Stasis would allocate them
;; there.
(module
  (memory (export "memory") 1)
  (func (export "test:records/user@1#seventeen:bca79f5d2ec4e0fe") (param i32) (result i32)
    (local.get 0)))
