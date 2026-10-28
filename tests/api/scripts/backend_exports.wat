;; A script that exports each function a backend runs by itself when it creates an instance, as
;; WAMR does whichever of them takes and returns nothing. It's otherwise a script calls.stasis
;; loads.
(module
  (memory (export "memory") 1)
  (func $nothing)
  (export "_initialize" (func $nothing))
  (export "__wasm_call_ctors" (func $nothing))
  (export "__post_instantiate" (func $nothing))
  (func (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
