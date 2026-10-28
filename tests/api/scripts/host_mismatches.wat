;; A script whose imports from hosts.stasis's groups name host functions that the interface lacks,
;; or declares with another signature, in every way an import can.
(module
  ;; A host function, a group, a method and a kind of host object that the interface lacks.
  (import "test:hosts/host@1" "missing:2e38e77b22c314a4" (func))
  (import "test:hosts/nowhere@1" "ping:2e38e77b22c314a4" (func))
  (import "test:hosts/host@1" "thing.prod:bfc6d0f269b3c1d6" (func (param i32)))
  (import "test:hosts/host@1" "gadget.poke:bfc6d0f269b3c1d6" (func (param i32)))
  ;; A host function and a method with another hash, and `other.ping` with the hash of `host.ping`.
  (import "test:hosts/host@1" "pass_u8:0000000000000000" (func (param i32) (result i32)))
  (import "test:hosts/host@1" "thing.poke:0000000000000000" (func (param i32)))
  (import "test:hosts/other@1" "ping:2e38e77b22c314a4" (func))
  (memory (export "memory") 1))
