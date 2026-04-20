// tree.c — Tree object serialization and construction
//
// PROVIDED functions: get_file_mode, tree_parse, tree_serialize
// TODO functions:     tree_from_index

#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Forward declaration (implemented in object.c)
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─── Mode Constants ─────────────────────────────────────────────────────────

#define MODE_FILE 0100644
#define MODE_EXEC 0100755
#define MODE_DIR  0040000

// ─── PROVIDED ───────────────────────────────────────────────────────────────

uint32_t get_file_mode(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (S_ISDIR(st.st_mode)) return MODE_DIR;
    if (st.st_mode & S_IXUSR) return MODE_EXEC;
    return MODE_FILE;
}

int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    const uint8_t *end = ptr + len;

    while (ptr < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *entry = &tree_out->entries[tree_out->count];

        const uint8_t *space = memchr(ptr, ' ', (size_t)(end - ptr));
        if (!space) return -1;

        char mode_str[16] = {0};
        size_t mode_len = (size_t)(space - ptr);
        if (mode_len >= sizeof(mode_str)) return -1;
        memcpy(mode_str, ptr, mode_len);
        entry->mode = (uint32_t)strtol(mode_str, NULL, 8);
        ptr = space + 1;

        const uint8_t *nul = memchr(ptr, '\0', (size_t)(end - ptr));
        if (!nul) return -1;

        size_t name_len = (size_t)(nul - ptr);
        if (name_len >= sizeof(entry->name)) return -1;
        memcpy(entry->name, ptr, name_len);
        entry->name[name_len] = '\0';
        ptr = nul + 1;

        if (ptr + HASH_SIZE > end) return -1;
        memcpy(entry->hash.hash, ptr, HASH_SIZE);
        ptr += HASH_SIZE;

        tree_out->count++;
    }
    return 0;
}

static int compare_tree_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry *)a)->name, ((const TreeEntry *)b)->name);
}

int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    size_t max_size = (size_t)tree->count * 296;
    uint8_t *buf = malloc(max_size);
    if (!buf) return -1;

    Tree sorted = *tree;
    qsort(sorted.entries, (size_t)sorted.count, sizeof(TreeEntry), compare_tree_entries);

    size_t offset = 0;
    for (int i = 0; i < sorted.count; i++) {
        const TreeEntry *e = &sorted.entries[i];
        int w = sprintf((char *)buf + offset, "%o %s", e->mode, e->name);
        offset += (size_t)w + 1; // +1 for null terminator from sprintf
        memcpy(buf + offset, e->hash.hash, HASH_SIZE);
        offset += HASH_SIZE;
    }

    *data_out = buf;
    *len_out  = offset;
    return 0;
}

// ─── IMPLEMENTATION ──────────────────────────────────────────────────────────

// Recursive helper: given `count` index entries whose paths are already
// relative to the current level, build a tree object and write it to the store.
static int write_tree_level(IndexEntry *entries, int count, ObjectID *id_out) {
    Tree tree;
    tree.count = 0;

    int i = 0;
    while (i < count) {
        const char *path  = entries[i].path;
        const char *slash = strchr(path, '/');

        if (!slash) {
            // ── Leaf file at this level ──
            if (tree.count >= MAX_TREE_ENTRIES) return -1;
            TreeEntry *te = &tree.entries[tree.count++];
            te->mode = entries[i].mode;
            te->hash = entries[i].hash;
            strncpy(te->name, path, sizeof(te->name) - 1);
            te->name[sizeof(te->name) - 1] = '\0';
            i++;
        } else {
            // ── Sub-directory ──
            size_t dir_len = (size_t)(slash - path);
            char dir_name[256];
            if (dir_len >= sizeof(dir_name)) return -1;
            memcpy(dir_name, path, dir_len);
            dir_name[dir_len] = '\0';

            // Collect all entries belonging to this sub-directory
            int j = i;
            while (j < count &&
                   strncmp(entries[j].path, dir_name, dir_len) == 0 &&
                   entries[j].path[dir_len] == '/')
                j++;

            int sub_count = j - i;
            IndexEntry *sub = malloc((size_t)sub_count * sizeof(IndexEntry));
            if (!sub) return -1;

            for (int k = 0; k < sub_count; k++) {
                sub[k] = entries[i + k];
                // Strip "dir_name/" prefix from the path
                size_t rest = strlen(entries[i + k].path) - dir_len - 1;
                memmove(sub[k].path, entries[i + k].path + dir_len + 1, rest + 1);
            }

            ObjectID sub_id;
            int rc = write_tree_level(sub, sub_count, &sub_id);
            free(sub);
            if (rc != 0) return -1;

            if (tree.count >= MAX_TREE_ENTRIES) return -1;
            TreeEntry *te = &tree.entries[tree.count++];
            te->mode = MODE_DIR;
            te->hash = sub_id;
            strncpy(te->name, dir_name, sizeof(te->name) - 1);
            te->name[sizeof(te->name) - 1] = '\0';

            i = j;
        }
    }

    void *data;
    size_t data_len;
    if (tree_serialize(&tree, &data, &data_len) != 0) return -1;

    int rc = object_write(OBJ_TREE, data, data_len, id_out);
    free(data);
    return rc;
}

// Build a tree hierarchy from the current index and write all tree objects.
int tree_from_index(ObjectID *id_out) {
    Index index;
    if (index_load(&index) != 0) return -1;

    if (index.count == 0) {
        Tree empty; empty.count = 0;
        void *data; size_t data_len;
        if (tree_serialize(&empty, &data, &data_len) != 0) return -1;
        int rc = object_write(OBJ_TREE, data, data_len, id_out);
        free(data);
        return rc;
    }

    return write_tree_level(index.entries, index.count, id_out);
}
