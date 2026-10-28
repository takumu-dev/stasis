;; Imports `host.give_hit` from records.stasis, which returns a record that holds a string, but
;; doesn't export the cabi_realloc through which Stasis would put the string into its memory.
(module
  (import "test:records/host@1" "give_hit:72e479e0a668ef06" (func (param i32)))
  (memory (export "memory") 1))
