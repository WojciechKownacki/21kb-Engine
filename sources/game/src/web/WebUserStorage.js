// Installed before the generated runtime; invoked after its filesystem exists.
Module['kbInitializeStorage'] = function() {
    const root = '/21kb-user-' + encodeURIComponent(new URL('.', location.href).pathname);
    const names = ['clean', 'dirty', 'pending', 'failed', 'unavailable'];
    const storage = Module['kbUserStorage'] = {
        root, startup:0, status:2, diagnostic:'', revision:0, clock:Date.now(), blocked:false,
        dirty:new Map(), known:new Set(), database:null,
        publish(status, diagnostic='') {
            this.status = status;
            this.diagnostic = diagnostic;
            document.documentElement.dataset.kbStorage = names[status];
        },
        changed(path) {
            ++this.revision;
            // IDBFS reconciles modification times. Preserve distinct writes
            // even within one browser clock tick or after a clock adjustment.
            this.clock = Math.max(Date.now(), this.clock + 1);
            if (path) {
                FS.utime(path, this.clock, this.clock);
                for (let current=path; current.startsWith(root+'/'); current=current.slice(0,current.lastIndexOf('/'))) {
                    this.known.add(current);
                    this.dirty.set(current, {revision:this.revision, removed:false});
                }
            } else {
                for (const current of this.known) {
                    if (!FS.analyzePath(current).exists) {
                        this.known.delete(current);
                        this.dirty.set(current, {revision:this.revision, removed:true});
                    }
                }
            }
            if (this.status !== 2 && !this.blocked) this.publish(1);
        },
        flush() {
            if (this.startup !== 1 || this.blocked) return false;
            if (this.status === 2 || this.status === 0) return true;
            const revision = this.revision;
            this.publish(2);
            const complete = error => {
                if (error) this.publish(3, String(error.name || 'StorageError') + ': ' + String(error.message || error));
                else this.publish(this.revision === revision ? 0 : 1);
            };
            this.sync(false, complete);
            return true;
        },
        sync(populate, callback) {
            let settled = false;
            const complete = error => {
                if (settled) return;
                settled = true;
                clearTimeout(timer);
                this.pendingCompletion = null;
                callback(error);
            };
            const timer = setTimeout(() => {
                // An unconfirmed operation may still be running. Do not
                // overlap a retry or acknowledge a late callback as success.
                this.blocked = true;
                complete(new DOMException('Storage synchronization timed out; reload before retrying', 'TimeoutError'));
            }, 15000);
            this.pendingCompletion = complete;
            try {
                if (populate) FS.syncfs(true, complete);
                else this.storeChanges(complete);
            }
            catch (error) { complete(error); }
        },
        storeChanges(complete) {
            const changes = new Map(this.dirty);
            if (!changes.size) { complete(null); return; }
            const entries = [];
            for (const [path, change] of changes) {
                if (change.removed) { entries.push({path, removed:true}); continue; }
                // The mounted working filesystem uses synchronous MEMFS reads.
                let entry, failure;
                IDBFS.loadLocalEntry(path, (error, value) => { failure=error; entry=value; });
                if (failure || !entry) { complete(failure || new Error('Storage entry could not be captured')); return; }
                entries.push({path, entry, removed:false});
            }
            const transaction = this.database.transaction([IDBFS.DB_STORE_NAME], 'readwrite');
            const store = transaction.objectStore(IDBFS.DB_STORE_NAME);
            const fail = error => {
                // A failed request must abort the whole save/settings batch.
                // Handling the error alone can allow other requests to commit.
                try { transaction.abort(); } catch (_) { /* Already finished. */ }
                complete(error);
            };
            transaction.onerror = transaction.onabort = event => {
                event.preventDefault();
                complete(event.target.error || new DOMException('Browser storage transaction was aborted', 'AbortError'));
            };
            transaction.oncomplete = () => {
                for (const [path, change] of changes) {
                    if (this.dirty.get(path) === change) this.dirty.delete(path);
                }
                complete(null);
            };
            try {
                // Persist only this page's mutations. A full-tree reconcile
                // would remove files saved by another independent page.
                for (const item of entries) {
                    const callback = error => { if (error) fail(error); };
                    if (item.removed) IDBFS.removeRemoteEntry(store, item.path, callback);
                    else IDBFS.storeRemoteEntry(store, item.path, item.entry, callback);
                }
            } catch (error) { fail(error); }
        }
    };
    storage.publish(2);
    const fail = error => {
        storage.startup = -1;
        storage.publish(3, String(error.name || 'StorageError') + ': ' + String(error.message || error));
    };
    try {
        FS.mkdir(root);
        FS.mount(IDBFS, {}, root);
        // Observe the actual owned database as well as the SDK callback:
        // an explicit transaction.abort() can have a null transaction.error.
        IDBFS.getDB(root, (error, database) => {
            if (error) { fail(error); return; }
            storage.database = database;
            database.addEventListener('abort', event => {
                storage.pendingCompletion?.(event.target.error ||
                    new DOMException('Browser storage transaction was aborted', 'AbortError'));
            });
            storage.sync(true, error => {
                if (error) { fail(error); return; }
                try {
                    const visit = path => {
                        const info = FS.stat(path);
                        storage.clock = Math.max(storage.clock, Number(info.mtime));
                        if (path !== root) storage.known.add(path);
                        if (FS.isDir(info.mode)) {
                            for (const name of FS.readdir(path)) {
                                if (name !== '.' && name !== '..') visit(path + '/' + name);
                            }
                        }
                    };
                    visit(root);
                    storage.startup = 1;
                    storage.publish(0);
                } catch (error) { fail(error); }
            });
        });
    } catch (error) { fail(error); }
};
