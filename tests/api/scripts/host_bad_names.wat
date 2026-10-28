;; A script whose imports from a group of hosts.stasis have names that don't parse as a host
;; function's or a method's. The global's name doesn't parse either, but a script imports only
;; functions, which is all that loading says of it.
(module
  (import "test:hosts/host@1" "Ping:2e38e77b22c314a4" (func))
  (import "test:hosts/host@1" "ping" (func))
  (import "test:hosts/host@1" "ping:2E38E77B22C314A4" (func))
  (import "test:hosts/host@1" "thing.poke.again:bfc6d0f269b3c1d6" (func (param i32)))
  (import "test:hosts/host@1" ".poke:bfc6d0f269b3c1d6" (func (param i32)))
  (import "test:hosts/host@1" "count" (global i32))
  (memory (export "memory") 1))
