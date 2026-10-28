;; A script that counts the bytes of its memory that aren't zero, fills every byte, and grows its
;; memory, as memory.stasis says. Its memory starts with one page, and it has no data, so every byte
;; of its memory should start as zero.
(module
  (memory (export "memory") 1)
  (func (export "test:memory/caller@1#nonzero_bytes:5b69886a8b9e31a8") (result i32)
    (local $at i32) (local $end i32) (local $count i32)
    (local.set $end (i32.mul (memory.size) (i32.const 65536)))
    (block $done
      (loop $next
        (br_if $done (i32.ge_u (local.get $at) (local.get $end)))
        (if (i32.load8_u (local.get $at))
          (then (local.set $count (i32.add (local.get $count) (i32.const 1)))))
        (local.set $at (i32.add (local.get $at) (i32.const 1)))
        (br $next)))
    (local.get $count))
  (func (export "test:memory/caller@1#fill:2e38e77b22c314a4")
    (memory.fill (i32.const 0) (i32.const 0xff) (i32.mul (memory.size) (i32.const 65536))))
  (func (export "test:memory/caller@1#grow:4d8a9a4559ed9204") (param i32) (result i32)
    (memory.grow (local.get 0))))
