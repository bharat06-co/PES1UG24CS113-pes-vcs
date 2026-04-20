// commit.c — Commit creation and history traversal
//
// PROVIDED functions: commit_parse, commit_serialize, commit_walk, head_read, head_update
// TODO functions:     commit_create
// CMD functions:      cmd_commit, cmd_log  (declared in pes.h)
// Phase 5 stubs:      branch_list, branch_create, branch_delete, checkout

#include "commit.h"
#include "index.h"
#include "tree.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

// Forward declarations (implemented in object.c)
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out);

// ─── PROVIDED ────────────────────────────────────────────────────────────────

int commit_parse(const void *data, size_t len, Commit *commit_out) {
    (void)len;
    const char *p = (const char *)data;
    char hex[HASH_HEX_SIZE + 1];

    if (sscanf(p, "tree %64s\n", hex) != 1) return -1;
    if (hex_to_hash(hex, &commit_out->tree) != 0) return -1;
    p = strchr(p, '\n') + 1;

    if (strncmp(p, "parent ", 7) == 0) {
        if (sscanf(p, "parent %64s\n", hex) != 1) return -1;
        if (hex_to_hash(hex, &commit_out->parent) != 0) return -1;
        commit_out->has_parent = 1;
        p = strchr(p, '\n') + 1;
    } else {
        commit_out->has_parent = 0;
    }

    char author_buf[256];
    if (sscanf(p, "author %255[^\n]\n", author_buf) != 1) return -1;
    char *last_space = strrchr(author_buf, ' ');
    if (!last_space) return -1;
    commit_out->timestamp = (uint64_t)strtoull(last_space + 1, NULL, 10);
    *last_space = '\0';
    strncpy(commit_out->author, author_buf, sizeof(commit_out->author) - 1);

    p = strchr(p, '\n') + 1; // skip author
    p = strchr(p, '\n') + 1; // skip committer
    p = strchr(p, '\n') + 1; // skip blank line

    strncpy(commit_out->message, p, sizeof(commit_out->message) - 1);
    return 0;
}

int commit_serialize(const Commit *commit, void **data_out, size_t *len_out) {
    char tree_hex[HASH_HEX_SIZE + 1];
    char parent_hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&commit->tree, tree_hex);

    char buf[8192];
    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - (size_t)n, "tree %s\n", tree_hex);
    if (commit->has_parent) {
        hash_to_hex(&commit->parent, parent_hex);
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "parent %s\n", parent_hex);
    }
    n += snprintf(buf + n, sizeof(buf) - (size_t)n,
                  "author %s %" PRIu64 "\n"
                  "committer %s %" PRIu64 "\n"
                  "\n"
                  "%s",
                  commit->author, commit->timestamp,
                  commit->author, commit->timestamp,
                  commit->message);

    *data_out = malloc((size_t)n + 1);
    if (!*data_out) return -1;
    memcpy(*data_out, buf, (size_t)n + 1);
    *len_out = (size_t)n;
    return 0;
}

int commit_walk(commit_walk_fn callback, void *ctx) {
    ObjectID id;
    if (head_read(&id) != 0) return -1;
    while (1) {
        ObjectType type;
        void *raw; size_t raw_len;
        if (object_read(&id, &type, &raw, &raw_len) != 0) return -1;
        Commit c;
        int rc = commit_parse(raw, raw_len, &c);
        free(raw);
        if (rc != 0) return -1;
        callback(&id, &c, ctx);
        if (!c.has_parent) break;
        id = c.parent;
    }
    return 0;
}

int head_read(ObjectID *id_out) {
    FILE *f = fopen(HEAD_FILE, "r");
    if (!f) return -1;
    char line[512];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    fclose(f);
    line[strcspn(line, "\r\n")] = '\0';

    if (strncmp(line, "ref: ", 5) == 0) {
        char ref_path[512];
        snprintf(ref_path, sizeof(ref_path), "%s/%s", PES_DIR, line + 5);
        f = fopen(ref_path, "r");
        if (!f) return -1;
        if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
        fclose(f);
        line[strcspn(line, "\r\n")] = '\0';
    }
    return hex_to_hash(line, id_out);
}

int head_update(const ObjectID *new_commit) {
    FILE *f = fopen(HEAD_FILE, "r");
    if (!f) return -1;
    char line[512];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    fclose(f);
    line[strcspn(line, "\r\n")] = '\0';

    char target[512];
    if (strncmp(line, "ref: ", 5) == 0)
        snprintf(target, sizeof(target), "%s/%s", PES_DIR, line + 5);
    else
        snprintf(target, sizeof(target), "%s", HEAD_FILE);

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", target);

    f = fopen(tmp, "w");
    if (!f) return -1;
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(new_commit, hex);
    fprintf(f, "%s\n", hex);
    fflush(f); fsync(fileno(f)); fclose(f);

    return rename(tmp, target);
}

// ─── IMPLEMENTATION ──────────────────────────────────────────────────────────

int commit_create(const char *message, ObjectID *commit_id_out) {
    // 1. Build tree from the staged index
    ObjectID tree_id;
    if (tree_from_index(&tree_id) != 0) return -1;

    // 2. Fill commit struct
    Commit c;
    memset(&c, 0, sizeof(c));
    c.tree      = tree_id;
    c.timestamp = (uint64_t)time(NULL);
    strncpy(c.author,  pes_author(), sizeof(c.author)  - 1);
    strncpy(c.message, message,     sizeof(c.message) - 1);

    // 3. Read current HEAD as parent (empty repo → no parent)
    c.has_parent = (head_read(&c.parent) == 0) ? 1 : 0;

    // 4. Serialise and store the commit object
    void *raw; size_t raw_len;
    if (commit_serialize(&c, &raw, &raw_len) != 0) return -1;

    ObjectID commit_id;
    if (object_write(OBJ_COMMIT, raw, raw_len, &commit_id) != 0) { free(raw); return -1; }
    free(raw);

    // 5. Move the branch pointer to the new commit
    if (head_update(&commit_id) != 0) return -1;

    if (commit_id_out) *commit_id_out = commit_id;
    return 0;
}

// ─── CLI Commands ────────────────────────────────────────────────────────────

static void print_commit_cb(const ObjectID *id, const Commit *c, void *ctx) {
    (void)ctx;
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);

    time_t ts = (time_t)c->timestamp;
    char tbuf[64];
    struct tm *tm = gmtime(&ts);
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S UTC", tm);

    printf("commit %s\n", hex);
    printf("Author: %s\n", c->author);
    printf("Date:   %s\n", tbuf);
    printf("\n    %s\n\n", c->message);
}

void cmd_commit(int argc, char *argv[]) {
    const char *message = NULL;
    for (int i = 2; i < argc - 1; i++)
        if (strcmp(argv[i], "-m") == 0) { message = argv[i + 1]; break; }

    if (!message) {
        fprintf(stderr, "error: commit requires a message (-m \"message\")\n");
        return;
    }

    ObjectID id;
    if (commit_create(message, &id) != 0) {
        fprintf(stderr, "error: commit failed\n");
        return;
    }

    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&id, hex);
    hex[12] = '\0';
    printf("Committed: %s... %s\n", hex, message);
}

void cmd_log(void) {
    if (commit_walk(print_commit_cb, NULL) != 0)
        fprintf(stderr, "error: no commits yet (or repository not initialised)\n");
}

// ─── Phase 5 Stubs (analysis-only — replace when implementing Phase 5) ──────

void branch_list(void)                { printf("(branch: not yet implemented)\n"); }
int  branch_create(const char *name)  { (void)name;   return -1; }
int  branch_delete(const char *name)  { (void)name;   return -1; }
int  checkout(const char *target)     { (void)target; return -1; }
