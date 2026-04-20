// pes.h — Core data structures and constants for PES-VCS
//
// This file is PROVIDED. Do not modify unless adding helper declarations
// for your own utility functions.

#ifndef PES_H
#define PES_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

// ─── Constants ───────────────────────────────────────────────────────────────

#define HASH_SIZE     32    // SHA-256 produces 32 bytes
#define HASH_HEX_SIZE 64    // 32 bytes = 64 hex characters

#define PES_DIR      ".pes"
#define OBJECTS_DIR  ".pes/objects"
#define REFS_DIR     ".pes/refs/heads"
#define INDEX_FILE   ".pes/index"
#define HEAD_FILE    ".pes/HEAD"

// ─── Object Types ────────────────────────────────────────────────────────────

typedef enum {
    OBJ_BLOB,    // File content
    OBJ_TREE,    // Directory listing
    OBJ_COMMIT   // Snapshot with metadata
} ObjectType;

// ─── Object Identifier ──────────────────────────────────────────────────────

typedef struct {
    uint8_t hash[HASH_SIZE];
} ObjectID;

// ─── Utility Functions (implement in object.c) ─────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out);
int  hex_to_hash(const char *hex, ObjectID *id_out);

// ─── Author Configuration ───────────────────────────────────────────────────

#define DEFAULT_AUTHOR "PES User <pes@localhost>"

static inline const char* pes_author(void) {
    const char *env = getenv("PES_AUTHOR");
    return (env && env[0]) ? env : DEFAULT_AUTHOR;
}

// ─── Command declarations ───────────────────────────────────────────────────
// Helper declarations added as permitted — implementations live in
// index.c (cmd_init, cmd_add, cmd_status) and commit.c (cmd_commit, cmd_log).

void cmd_init(void);
void cmd_add(int argc, char *argv[]);
void cmd_status(void);
void cmd_commit(int argc, char *argv[]);
void cmd_log(void);

// Phase 5 stubs (analysis-only — implementations in commit.c)
void branch_list(void);
int  branch_create(const char *name);
int  branch_delete(const char *name);
int  checkout(const char *target);

#endif // PES_H
