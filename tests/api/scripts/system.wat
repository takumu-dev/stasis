;; A script that imports every function of the system interface, each of the type WASI preview 1
;; gives it, and provides an entry point of system.stasis for each, which calls it with the
;; arguments it's given and returns what it returns. It also provides the entry points that load and
;; store in its memory, which is 16 pages, enough for more iovecs than their sizes can add up to in
;; a u32, and never grows.
(module
  (import "wasi_snapshot_preview1" "args_get" (func $args_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "args_sizes_get"
    (func $args_sizes_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "clock_res_get"
    (func $clock_res_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "clock_time_get"
    (func $clock_time_get (param i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "environ_get" (func $environ_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "environ_sizes_get"
    (func $environ_sizes_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_advise"
    (func $fd_advise (param i32 i64 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_allocate"
    (func $fd_allocate (param i32 i64 i64) (result i32)))
  (import "wasi_snapshot_preview1" "fd_close" (func $fd_close (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_datasync" (func $fd_datasync (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_fdstat_get"
    (func $fd_fdstat_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_fdstat_set_flags"
    (func $fd_fdstat_set_flags (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_fdstat_set_rights"
    (func $fd_fdstat_set_rights (param i32 i64 i64) (result i32)))
  (import "wasi_snapshot_preview1" "fd_filestat_get"
    (func $fd_filestat_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_filestat_set_size"
    (func $fd_filestat_set_size (param i32 i64) (result i32)))
  (import "wasi_snapshot_preview1" "fd_filestat_set_times"
    (func $fd_filestat_set_times (param i32 i64 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_pread"
    (func $fd_pread (param i32 i32 i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_prestat_dir_name"
    (func $fd_prestat_dir_name (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_prestat_get"
    (func $fd_prestat_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_pwrite"
    (func $fd_pwrite (param i32 i32 i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_read" (func $fd_read (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_readdir"
    (func $fd_readdir (param i32 i32 i32 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_renumber" (func $fd_renumber (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_seek" (func $fd_seek (param i32 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_sync" (func $fd_sync (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_tell" (func $fd_tell (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write" (func $fd_write (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_create_directory"
    (func $path_create_directory (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_filestat_get"
    (func $path_filestat_get (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_filestat_set_times"
    (func $path_filestat_set_times (param i32 i32 i32 i32 i64 i64 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_link"
    (func $path_link (param i32 i32 i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_open"
    (func $path_open (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_readlink"
    (func $path_readlink (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_remove_directory"
    (func $path_remove_directory (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_rename"
    (func $path_rename (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_symlink"
    (func $path_symlink (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "path_unlink_file"
    (func $path_unlink_file (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "poll_oneoff"
    (func $poll_oneoff (param i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "proc_exit" (func $proc_exit (param i32)))
  (import "wasi_snapshot_preview1" "proc_raise" (func $proc_raise (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "random_get" (func $random_get (param i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sched_yield" (func $sched_yield (result i32)))
  (import "wasi_snapshot_preview1" "sock_accept"
    (func $sock_accept (param i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_recv"
    (func $sock_recv (param i32 i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_send"
    (func $sock_send (param i32 i32 i32 i32 i32) (result i32)))
  (import "wasi_snapshot_preview1" "sock_shutdown"
    (func $sock_shutdown (param i32 i32) (result i32)))
  (memory (export "memory") 16)

  (func (export "test:system/user@1#args_get:d6b542a74ea081eb") (param i32 i32) (result i32)
    (call $args_get (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#args_sizes_get:d6b542a74ea081eb") (param i32 i32) (result i32)
    (call $args_sizes_get (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#clock_res_get:d6b542a74ea081eb") (param i32 i32) (result i32)
    (call $clock_res_get (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#clock_time_get:f2abe89ead9ae3c4")
    (param i32 i64 i32) (result i32)
    (call $clock_time_get (local.get 0) (local.get 1) (local.get 2)))
  (func (export "test:system/user@1#environ_get:d6b542a74ea081eb") (param i32 i32) (result i32)
    (call $environ_get (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#environ_sizes_get:d6b542a74ea081eb")
    (param i32 i32) (result i32)
    (call $environ_sizes_get (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#fd_advise:b89fd75fc81d286b")
    (param i32 i64 i64 i32) (result i32)
    (call $fd_advise (local.get 0) (local.get 1) (local.get 2) (local.get 3)))
  (func (export "test:system/user@1#fd_allocate:f19b2e1444ec4ae1") (param i32 i64 i64) (result i32)
    (call $fd_allocate (local.get 0) (local.get 1) (local.get 2)))
  (func (export "test:system/user@1#fd_close:4d8a9a4559ed9204") (param i32) (result i32)
    (call $fd_close (local.get 0)))
  (func (export "test:system/user@1#fd_datasync:4d8a9a4559ed9204") (param i32) (result i32)
    (call $fd_datasync (local.get 0)))
  (func (export "test:system/user@1#fd_fdstat_get:d6b542a74ea081eb") (param i32 i32) (result i32)
    (call $fd_fdstat_get (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#fd_fdstat_set_flags:d6b542a74ea081eb")
    (param i32 i32) (result i32)
    (call $fd_fdstat_set_flags (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#fd_fdstat_set_rights:f19b2e1444ec4ae1")
    (param i32 i64 i64) (result i32)
    (call $fd_fdstat_set_rights (local.get 0) (local.get 1) (local.get 2)))
  (func (export "test:system/user@1#fd_filestat_get:d6b542a74ea081eb") (param i32 i32) (result i32)
    (call $fd_filestat_get (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#fd_filestat_set_size:8005089bad94ff2a")
    (param i32 i64) (result i32)
    (call $fd_filestat_set_size (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#fd_filestat_set_times:b89fd75fc81d286b")
    (param i32 i64 i64 i32) (result i32)
    (call $fd_filestat_set_times (local.get 0) (local.get 1) (local.get 2) (local.get 3)))
  (func (export "test:system/user@1#fd_pread:38eb6a494ddfb3a2")
    (param i32 i32 i32 i64 i32) (result i32)
    (call $fd_pread (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4)))
  (func (export "test:system/user@1#fd_prestat_dir_name:c111a918e7a83a5e")
    (param i32 i32 i32) (result i32)
    (call $fd_prestat_dir_name (local.get 0) (local.get 1) (local.get 2)))
  (func (export "test:system/user@1#fd_prestat_get:d6b542a74ea081eb") (param i32 i32) (result i32)
    (call $fd_prestat_get (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#fd_pwrite:38eb6a494ddfb3a2")
    (param i32 i32 i32 i64 i32) (result i32)
    (call $fd_pwrite (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4)))
  (func (export "test:system/user@1#fd_read:cf013bfdc376205e") (param i32 i32 i32 i32) (result i32)
    (call $fd_read (local.get 0) (local.get 1) (local.get 2) (local.get 3)))
  (func (export "test:system/user@1#fd_readdir:38eb6a494ddfb3a2")
    (param i32 i32 i32 i64 i32) (result i32)
    (call $fd_readdir (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4)))
  (func (export "test:system/user@1#fd_renumber:d6b542a74ea081eb") (param i32 i32) (result i32)
    (call $fd_renumber (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#fd_seek:8186c89a71fbf6da") (param i32 i64 i32 i32) (result i32)
    (call $fd_seek (local.get 0) (local.get 1) (local.get 2) (local.get 3)))
  (func (export "test:system/user@1#fd_sync:4d8a9a4559ed9204") (param i32) (result i32)
    (call $fd_sync (local.get 0)))
  (func (export "test:system/user@1#fd_tell:d6b542a74ea081eb") (param i32 i32) (result i32)
    (call $fd_tell (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#fd_write:cf013bfdc376205e") (param i32 i32 i32 i32) (result i32)
    (call $fd_write (local.get 0) (local.get 1) (local.get 2) (local.get 3)))
  (func (export "test:system/user@1#path_create_directory:c111a918e7a83a5e")
    (param i32 i32 i32) (result i32)
    (call $path_create_directory (local.get 0) (local.get 1) (local.get 2)))
  (func (export "test:system/user@1#path_filestat_get:79b027efcf930697")
    (param i32 i32 i32 i32 i32) (result i32)
    (call $path_filestat_get (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4)))
  (func (export "test:system/user@1#path_filestat_set_times:0eaf97ac3dacf779")
    (param i32 i32 i32 i32 i64 i64 i32) (result i32)
    (call $path_filestat_set_times
      (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4) (local.get 5)
      (local.get 6)))
  (func (export "test:system/user@1#path_link:01f02ee6df4df096")
    (param i32 i32 i32 i32 i32 i32 i32) (result i32)
    (call $path_link
      (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4) (local.get 5)
      (local.get 6)))
  (func (export "test:system/user@1#path_open:280f63cd3a19a2b7")
    (param i32 i32 i32 i32 i32 i64 i64 i32 i32) (result i32)
    (call $path_open
      (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4) (local.get 5)
      (local.get 6) (local.get 7) (local.get 8)))
  (func (export "test:system/user@1#path_readlink:882dc2367e0cca7a")
    (param i32 i32 i32 i32 i32 i32) (result i32)
    (call $path_readlink
      (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4) (local.get 5)))
  (func (export "test:system/user@1#path_remove_directory:c111a918e7a83a5e")
    (param i32 i32 i32) (result i32)
    (call $path_remove_directory (local.get 0) (local.get 1) (local.get 2)))
  (func (export "test:system/user@1#path_rename:882dc2367e0cca7a")
    (param i32 i32 i32 i32 i32 i32) (result i32)
    (call $path_rename
      (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4) (local.get 5)))
  (func (export "test:system/user@1#path_symlink:79b027efcf930697")
    (param i32 i32 i32 i32 i32) (result i32)
    (call $path_symlink (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4)))
  (func (export "test:system/user@1#path_unlink_file:c111a918e7a83a5e")
    (param i32 i32 i32) (result i32)
    (call $path_unlink_file (local.get 0) (local.get 1) (local.get 2)))
  (func (export "test:system/user@1#poll_oneoff:cf013bfdc376205e")
    (param i32 i32 i32 i32) (result i32)
    (call $poll_oneoff (local.get 0) (local.get 1) (local.get 2) (local.get 3)))
  (func (export "test:system/user@1#proc_exit:e492f253b87ebe82") (param i32)
    (call $proc_exit (local.get 0)))
  (func (export "test:system/user@1#proc_raise:4d8a9a4559ed9204") (param i32) (result i32)
    (call $proc_raise (local.get 0)))
  (func (export "test:system/user@1#random_get:d6b542a74ea081eb") (param i32 i32) (result i32)
    (call $random_get (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#sched_yield:5b69886a8b9e31a8") (result i32)
    (call $sched_yield))
  (func (export "test:system/user@1#sock_accept:c111a918e7a83a5e") (param i32 i32 i32) (result i32)
    (call $sock_accept (local.get 0) (local.get 1) (local.get 2)))
  (func (export "test:system/user@1#sock_recv:882dc2367e0cca7a")
    (param i32 i32 i32 i32 i32 i32) (result i32)
    (call $sock_recv
      (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4) (local.get 5)))
  (func (export "test:system/user@1#sock_send:79b027efcf930697")
    (param i32 i32 i32 i32 i32) (result i32)
    (call $sock_send (local.get 0) (local.get 1) (local.get 2) (local.get 3) (local.get 4)))
  (func (export "test:system/user@1#sock_shutdown:d6b542a74ea081eb") (param i32 i32) (result i32)
    (call $sock_shutdown (local.get 0) (local.get 1)))

  (func (export "test:system/user@1#load_u8:65d04745e3f2b9ee") (param i32) (result i32)
    (i32.load8_u (local.get 0)))
  (func (export "test:system/user@1#load_u32:4d8a9a4559ed9204") (param i32) (result i32)
    (i32.load (local.get 0)))
  (func (export "test:system/user@1#load_u64:5db8d049340bbec7") (param i32) (result i64)
    (i64.load (local.get 0)))
  (func (export "test:system/user@1#store_u8:64136b00cfcc0e7f") (param i32 i32)
    (i32.store8 (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#store_u32:bcbfa7c332b66f12") (param i32 i32)
    (i32.store (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#store_u64:ccd850fbaf1e8298") (param i32 i64)
    (i64.store (local.get 0) (local.get 1)))
  (func (export "test:system/user@1#store_iovecs:e6ffd2ba6eb8b67d")
    (param $address i32) (param $count i32) (param $bytes i32) (param $size i32)
    (block $done
      (loop $next
        (br_if $done (i32.eqz (local.get $count)))
        (i32.store (local.get $address) (local.get $bytes))
        (i32.store offset=4 (local.get $address) (local.get $size))
        (local.set $address (i32.add (local.get $address) (i32.const 8)))
        (local.set $count (i32.sub (local.get $count) (i32.const 1)))
        (br $next)))))
