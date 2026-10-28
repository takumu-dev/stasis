;; A script that imports host functions under their names and hashes, but as functions of other
;; types than their signatures make them.
(module
  (import "test:hosts/host@1" "pass_u8:4618eff65f831824" (func (param i64) (result i32)))
  (import "test:hosts/host@1" "ping:2e38e77b22c314a4" (func (result i32)))
  ;; Seventeen arguments spill into memory, so the import takes only their address.
  (import "test:hosts/host@1" "seventeen:5f56e9a6de0ea0c7"
    (func (param i32 i32 i32 i64 f32 f64 i32 i64 i32 i32 i32 i64 f32 f64 i32 i32 i64)
      (result i64)))
  (memory (export "memory") 1))
