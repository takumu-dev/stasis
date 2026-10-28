;; A script built against traps.stasis whose stasis_initialize traps, so that a new instance traps
;; before it's ever ready.
(module
  (memory (export "memory") 1)
  (func $initialize (export "stasis_initialize")
    (unreachable))
  (func $returns (export "test:traps/trapper@1#returns:2e38e77b22c314a4")))
