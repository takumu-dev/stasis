;; The smallest WAT module: one function that adds two numbers, and a memory.
(module
  (memory (export "memory") 1)
  (func (export "add") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.add))
