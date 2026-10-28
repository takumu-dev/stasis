;; A script with custom sections: the name section, from its functions' names, and one of its own.
;; The build strips them both from a copy, which is the same script.
(module
  (@custom "notes" "a custom section, which isn't part of the script's identity")
  (memory (export "memory") 1)
  (func $starts (export "test:calls/caller@1#starts:5b69886a8b9e31a8") (result i32)
    (i32.const 0)))
