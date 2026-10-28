#include "support.h"

#include <stasis.h>

#include "harness.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
