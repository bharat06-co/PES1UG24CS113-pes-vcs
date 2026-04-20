# PES-VCS Lab Report
**Name:** Bharat  
**SRN:** PES1UG24CS113  
**Repository:** https://github.com/bharat06-co/PES1UG24CS113-pes-vcs

---

## Phase 1: Object Storage — Screenshots

- **1A:** `./test_objects` — All Phase 1 tests passed (blob storage, deduplication, integrity check)
- **1B:** `find .pes/objects -type f` — Sharded directory structure under `.pes/objects/XX/`

---

## Phase 2: Tree Objects — Screenshots

- **2A:** `./test_tree` — All Phase 2 tests passed (serialize/parse roundtrip, deterministic serialization)
- **2B:** `xxd` of raw tree object showing binary format: `tree 49.100644 file1.txt...`

---

## Phase 3: Index (Staging Area) — Screenshots

- **3A:** `pes init` → `pes add file1.txt file2.txt` → `pes status` showing staged changes
- **3B:** `cat .pes/index` showing text format: `100644 <hash> <mtime> <size> <path>`

---

## Phase 4: Commits and History — Screenshots

- **4A:** `pes log` showing 3 commits with hashes, authors, timestamps, and messages
- **4B:** `find .pes -type f | sort` showing object store growth after 3 commits
- **4C:** `cat .pes/refs/heads/main` and `cat .pes/HEAD` showing the reference chain

---

## Phase 5: Branching and Checkout (Analysis)

### Q5.1 — How would you implement `pes checkout <branch>`?

A branch in PES-VCS is just a file at `.pes/refs/heads/<branch>` containing a commit hash.
To implement `pes checkout <branch>`, the following steps are needed:

**Files that need to change in `.pes/`:**
1. **`.pes/HEAD`** — Update it to point to the new branch: write `ref: refs/heads/<branch>\n`
2. **`.pes/index`** — Rebuild it to reflect the files in the target branch's tree snapshot
3. **Working directory** — All tracked files must be updated to match the target commit's tree

**Algorithm:**
1. Read the target branch file to get its commit hash
2. Read that commit object to get its tree hash
3. Recursively walk the tree object to get all file blobs
4. For each file in the target tree, write its blob content to the working directory
5. Delete any files tracked in the current index that are not present in the target tree
6. Rebuild the index from the target tree entries
7. Update HEAD to point to the new branch

**What makes this complex:**
- Files that exist in the current branch but not the target must be deleted
- Files modified in the working directory that differ from both branches create a conflict
- Nested directories (subtrees) must be handled recursively
- The operation must be atomic — a crash halfway through leaves the repo in a broken state
- Untracked files that would be overwritten by checkout need special handling

---

### Q5.2 — How would you detect a "dirty working directory" conflict?

When switching branches, if a file differs between the source and target branch AND has been modified in the working directory, checkout must refuse.

**Detection algorithm using only the index and object store:**

1. For each file in the **current index**:
   - Compute its current `stat()` metadata (mtime, size)
   - Compare with the stored `mtime_sec` and `size` in the index entry
   - If they differ → the file is **modified in the working directory** (dirty)

2. For each dirty file, check if the **target branch's tree** has a different blob hash for that file:
   - Read the target commit → get its tree → look up the file's hash in that tree
   - Compare with the current index entry's hash
   - If they differ → **conflict**: the file changed in both the working directory and the target branch

3. If any such conflict exists → print an error and abort checkout:
   ```
   error: your changes to 'src/main.c' would be overwritten by checkout
   please commit or stash your changes before switching branches
   ```

This approach uses only the index (for dirty detection via mtime/size) and the object store (for comparing blob hashes between branches) — no diff engine needed.

---

### Q5.3 — Detached HEAD and recovery

**What is detached HEAD?**
Normally, HEAD contains `ref: refs/heads/main` — a symbolic reference to a branch. In detached HEAD state, HEAD contains a raw commit hash directly, e.g., `a1b2c3d4...`. This happens when you checkout a specific commit instead of a branch name.

**What happens if you make commits in detached HEAD state?**
New commits are created normally — each commit points to its parent. However, no branch pointer is updated. The commits exist in the object store but are only reachable through HEAD. As soon as you checkout another branch, HEAD changes and those commits become **unreachable** — no branch, tag, or other reference points to them. They are effectively "lost" (though still physically present in the object store until garbage collection runs).

**How to recover those commits:**
1. **Before switching away** — create a branch pointing to the current HEAD commit:
   ```
   pes branch save-my-work   # creates .pes/refs/heads/save-my-work with current hash
   ```
2. **After switching away** — if you remember the commit hash (visible in terminal history):
   ```
   pes branch recover <hash>  # creates a new branch at that hash
   ```
3. **Using the reflog** (if implemented) — Git's reflog records every position HEAD has pointed to, making recovery easy even without remembering the hash.

Without a reflog, recovery requires manually searching the object store for the lost commit hash — which is why creating a branch before detaching is strongly recommended.

---

## Phase 6: Garbage Collection (Analysis)

### Q6.1 — Algorithm to find and delete unreachable objects

**Goal:** Delete all objects in `.pes/objects/` that are not reachable from any branch reference.

**Algorithm:**

**Step 1 — Collect all reachable objects (Mark phase):**
Use a hash set (e.g., a hash table or sorted array of ObjectIDs) to track reachable hashes.

```
reachable = empty hash set

for each file in .pes/refs/heads/:
    start_hash = read branch file
    walk_commit(start_hash, reachable)

walk_commit(hash, reachable):
    if hash already in reachable: return   // avoid cycles
    add hash to reachable
    commit = object_read(hash)
    add commit.tree to reachable
    walk_tree(commit.tree, reachable)
    if commit.has_parent:
        walk_commit(commit.parent, reachable)

walk_tree(hash, reachable):
    if hash already in reachable: return
    add hash to reachable
    tree = object_read(hash)
    for each entry in tree:
        add entry.hash to reachable
        if entry.mode == DIR:
            walk_tree(entry.hash, reachable)
```

**Step 2 — Sweep phase:**
```
for each file in .pes/objects/**/*:
    hash = filename (first 2 chars of dir + rest of filename)
    if hash NOT in reachable:
        delete the file
```

**Data structure:** A hash set of 32-byte ObjectIDs is ideal — O(1) lookup per object. A sorted array with binary search also works well for smaller repos.

**Estimate for 100,000 commits, 50 branches:**
- Average commit references 1 tree, each tree ~10 entries → roughly 10 blobs + 2 trees per commit
- Total objects ≈ 100,000 commits × 12 objects = ~1,200,000 objects to visit
- With 50 branches sharing history, many objects are visited once due to the "already in reachable" early return
- Realistic visit count: ~500,000–800,000 unique objects

---

### Q6.2 — Race condition between GC and concurrent commit

**The race condition:**

Consider this sequence of events with GC and a commit running concurrently:

```
Time  GC Thread                          Commit Thread
----  ---------                          -------------
T1    Scans all refs → builds reachable set
T2                                       Calls object_write(new_blob) → stored in object store
T3                                       Calls object_write(new_tree) → stored
T4    Sweeps object store                
T5    Sees new_blob — NOT in reachable   
T6    DELETES new_blob ← BUG!           
T7                                       Calls object_write(new_commit) → references new_tree
T8                                       head_update() → branch now points to new_commit
T9                                       new_commit → new_tree → new_blob (DELETED!) → CORRUPT
```

The GC built its reachable set at T1, before the new blob was referenced by any commit. The new blob exists in the object store but isn't reachable yet — GC deletes it. The commit then completes referencing the now-deleted blob, leaving the repository corrupt.

**How Git's real GC avoids this:**

1. **Grace period:** Git's GC never deletes objects newer than 2 weeks old (configurable via `gc.pruneExpire`). Since a commit operation completes in milliseconds, any object written recently is safe.

2. **Lock files:** Git uses lock files during critical operations. A commit in progress holds a lock that GC detects and respects.

3. **Write order:** Objects are always written bottom-up (blobs first, then trees, then commits) and the branch ref is updated last. GC always reads refs first before scanning objects. This means a fully published commit is always fully reachable.

4. **Conservative marking:** Git's GC also marks any object referenced in the index (staging area) as reachable, protecting in-progress work.

---

## Integration Test

`make test-integration` — All integration tests completed successfully:
- Repository initialization ✅
- Staging files ✅  
- First, second, third commits ✅
- Full history with `pes log` ✅
- Reference chain (HEAD → main → commit hash) ✅
- Object store showing 10 objects ✅
