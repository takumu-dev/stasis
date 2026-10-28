;; Imports `host.give` from strings.stasis, which returns a string, but doesn't export the
;; cabi_realloc through which Stasis would put it into the script's memory.
(module
  (import "test:strings/host@1" "give:ae6637fc59233091" (func (param i32)))
  (memory (export "memory") 1))
