#include "support.h"

#include <stasis.h>

#include "harness.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <process.h>
#include <windows.h>
#else
#include <pthread.h>
#endif

struct module_bytes read_module(const char *name) {
    char path[1024];
    const int length = snprintf(path, sizeof path, "%s/%s.wasm", STASIS_SCRIPTS_DIR, name);
    ASSERT_TRUE(length > 0 && (size_t)length < sizeof path);
    FILE *file = fopen(path, "rb");
    ASSERT_TRUE(file != NULL);
    ASSERT_INT_EQ(fseek(file, 0, SEEK_END), 0);
    const long size = ftell(file);
    ASSERT_TRUE(size > 0);
    ASSERT_INT_EQ(fseek(file, 0, SEEK_SET), 0);
    struct module_bytes module;
    module.size = (size_t)size;
    module.bytes = malloc(module.size);
    ASSERT_TRUE(module.bytes != NULL);
    ASSERT_SIZE_EQ(fread(module.bytes, 1, module.size, file), module.size);
    ASSERT_INT_EQ(fclose(file), 0);
    return module;
}

void free_module(struct module_bytes module) { free(module.bytes); }

stasis_library *create_library(void) {
    stasis_library *library = NULL;
    ASSERT_INT_EQ(stasis_library_create(NULL, &library), STASIS_OK);
    ASSERT_TRUE(library != NULL);
    return library;
}

stasis_script *load_script(stasis_library *library, const stasis_interface *interface_data,
                           const char *name) {
    const struct module_bytes module = read_module(name);
    char message[1024] = "not written";
    stasis_script *script = NULL;
    ASSERT_INT_EQ(stasis_script_load(library, interface_data, module.bytes, module.size, message,
                                     sizeof message, &script),
                  STASIS_OK);
    ASSERT_STR_EQ(message, "");
    ASSERT_TRUE(script != NULL);
    free_module(module);
    return script;
}

stasis_instance *start_instance(stasis_script *script) {
    stasis_instance *instance = NULL;
    ASSERT_INT_EQ(stasis_instance_create(script, NULL, &instance), STASIS_OK);
    ASSERT_TRUE(instance != NULL);
    ASSERT_INT_EQ(stasis_instance_start(instance), STASIS_OK);
    return instance;
}

void destroy_all(stasis_instance *instance, stasis_script *script, stasis_library *library) {
    ASSERT_INT_EQ(stasis_instance_destroy(instance), STASIS_OK);
    ASSERT_INT_EQ(stasis_script_destroy(script), STASIS_OK);
    ASSERT_INT_EQ(stasis_library_destroy(library), STASIS_OK);
}

/* A thread's body and its argument. */
struct thread_body {
    void (*body)(void *argument);
    void *argument;
};

#ifdef _WIN32

static unsigned __stdcall run_body(void *body) {
    const struct thread_body *run = body;
    run->body(run->argument);
    return 0;
}

void run_on_thread(void (*body)(void *argument), void *argument, size_t stack_size) {
    struct thread_body run = {body, argument};
    ASSERT_TRUE(stack_size <= UINT32_MAX);
    const uintptr_t thread = _beginthreadex(NULL, (unsigned)stack_size, run_body, &run,
                                            STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    ASSERT_TRUE(thread != 0);
    ASSERT_UINT_EQ(WaitForSingleObject((HANDLE)thread, INFINITE), WAIT_OBJECT_0);
    ASSERT_TRUE(CloseHandle((HANDLE)thread));
}

uintptr_t stack_end(void) {
    ULONG_PTR low = 0;
    ULONG_PTR high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    return low;
}

#else

static void *run_body(void *body) {
    const struct thread_body *run = body;
    run->body(run->argument);
    return NULL;
}

void run_on_thread(void (*body)(void *argument), void *argument, size_t stack_size) {
    struct thread_body run = {body, argument};
    /* NOLINTNEXTLINE(misc-include-cleaner): pthread.h declares it, in a header of glibc's */
    pthread_attr_t attributes;
    ASSERT_INT_EQ(pthread_attr_init(&attributes), 0);
    ASSERT_INT_EQ(pthread_attr_setstacksize(&attributes, stack_size), 0);
    ASSERT_INT_EQ(pthread_attr_setguardsize(&attributes, 0), 0);
    /* NOLINTNEXTLINE(misc-include-cleaner): pthread.h declares it, in a header of glibc's */
    pthread_t thread;
    ASSERT_INT_EQ(pthread_create(&thread, &attributes, run_body, &run), 0);
    ASSERT_INT_EQ(pthread_join(thread, NULL), 0);
    ASSERT_INT_EQ(pthread_attr_destroy(&attributes), 0);
}

uintptr_t stack_end(void) {
    /* NOLINTNEXTLINE(misc-include-cleaner): pthread.h declares it, in a header of glibc's */
    pthread_attr_t attributes;
    ASSERT_INT_EQ(pthread_getattr_np(pthread_self(), &attributes), 0);
    void *end = NULL;
    size_t size = 0;
    ASSERT_INT_EQ(pthread_attr_getstack(&attributes, &end, &size), 0);
    ASSERT_INT_EQ(pthread_attr_destroy(&attributes), 0);
    return (uintptr_t)end;
}

#endif
